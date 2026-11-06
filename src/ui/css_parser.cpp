/**
 * css_parser.cpp — see css_parser.h.
 *
 * A sheet is parsed once into rules (selectors + declarations); each element built asks for its
 * cascade: the rules whose selectors match it and its ancestors (CssScope), sorted as CSS sorts
 * them, one winning value per property. Inline HTML attributes (bgcolor=, color=...) are applied
 * by the widget builders after this, so they still win over CSS.
 */

#include "ui/css_parser.h"
#include "ui/css_box.h"
#include "ui/css_layout.h"
#include "ui/css_media.h"
#include "ui/css_text.h"
#include "ui/css_undo.h"
#include "ui/css_values.h"
#include "ui/dom.h"
#include "ui/ui_coords.h"
#include "ui/ui_html_internal.h"
#include "ui/ui_tag_reader.h"
#include "core/core.h"

#include "widgets/widget_common.h"
#include "utils/font.h"
#include "utils/log_config.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

static const char* TAG = "CSS";

namespace UI {

using CssDetail::AttrTest;
using CssDetail::Compound;
using CssDetail::Logical;
using CssDetail::NthTest;
using CssDetail::Pseudo;
using CssDetail::Rule;
using CssDetail::Selector;

const P::String Css::s_emptyStr;

namespace {

// The "C" locale's isspace and tolower: ASCII only, as the firmware runs
constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
constexpr char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

P::String lowered(std::string_view s) {
    P::String out(s.data(), s.size());
    for (char& c : out) c = lowerAscii(c);
    return out;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// A custom property: --accent
bool isCustom(std::string_view name) { return name.size() > 2 && name[0] == '-' && name[1] == '-'; }

// Name characters of CSS identifiers: letters, digits, '-', '_', and anything non-ASCII
bool isNameChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '-' || c == '_' || u >= 0x80;
}

// ============ Attributes in raw start-tag text ============

// Calls f(name, value) for each attribute of ` id="a" class='b c' checked type=text /` until f
// returns true; a valueless attribute has "" for its value
template <typename F>
void forEachAttr(std::string_view attrs, F f) {
    const size_t n = attrs.size();
    size_t i = 0;
    while (i < n) {
        while (i < n && (isSpace(attrs[i]) || attrs[i] == '/')) ++i;
        if (i >= n) break;
        const size_t ns = i;
        while (i < n && !isSpace(attrs[i]) && attrs[i] != '=' && attrs[i] != '/' && attrs[i] != '>') ++i;
        const std::string_view name = attrs.substr(ns, i - ns);
        while (i < n && isSpace(attrs[i])) ++i;
        std::string_view value;
        if (i < n && attrs[i] == '=') {
            ++i;
            while (i < n && isSpace(attrs[i])) ++i;
            if (i < n && (attrs[i] == '"' || attrs[i] == '\'')) {
                const char q = attrs[i++];
                const size_t vs = i;
                while (i < n && attrs[i] != q) ++i;
                value = attrs.substr(vs, i - vs);
                if (i < n) ++i;
            } else {
                const size_t vs = i;
                while (i < n && !isSpace(attrs[i]) && attrs[i] != '>') ++i;
                value = attrs.substr(vs, i - vs);
            }
        }
        if (name.empty()) {  // a stray '=' or '>': step over it
            ++i;
            continue;
        }
        if (f(name, value)) return;
    }
}

// `word` is one of the space-separated names in `list`
bool hasWord(std::string_view list, std::string_view word) {
    if (word.empty()) return false;
    size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && isSpace(list[i])) ++i;
        const size_t s = i;
        while (i < list.size() && !isSpace(list[i])) ++i;
        if (list.substr(s, i - s) == word) return true;
    }
    return false;
}

// ============ Declarations ============

// Declarations of a block or of style="": `color: red; width: 10px !important`. A ';' inside
// quotes or parentheses (url(...), "a;b") does not end a declaration.
P::Array<CssDecl> parseDecls(std::string_view block) {
    P::Array<CssDecl> decls;
    P::String clean;  // the block without /* comments */, if it has any
    if (block.find("/*") != std::string_view::npos) {
        char quote = 0;
        for (size_t i = 0; i < block.size(); ++i) {
            const char c = block[i];
            if (!quote && c == '/' && i + 1 < block.size() && block[i + 1] == '*') {
                const size_t close = block.find("*/", i + 2);
                i = close == std::string_view::npos ? block.size() : close + 1;
                continue;
            }
            if (quote && c == quote) quote = 0;
            else if (!quote && (c == '"' || c == '\'')) quote = c;
            clean += c;
        }
        block = clean;
    }
    size_t i = 0;
    const size_t n = block.size();
    while (i < n) {
        const size_t start = i;
        char quote = 0;
        int depth = 0;
        for (; i < n; ++i) {
            const char c = block[i];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth > 0) --depth;
            } else if (c == ';' && depth == 0) {
                break;
            }
        }
        const std::string_view decl = block.substr(start, i - start);
        if (i < n) ++i;  // past ';'
        const size_t colon = decl.find(':');
        if (colon == std::string_view::npos) continue;
        P::String name = Css::trim(decl.substr(0, colon));
        P::String value = Css::trim(decl.substr(colon + 1));
        if (name.empty()) continue;
        if (name.compare(0, 2, "--") != 0) name = lowered(name);  // custom properties keep their case
        bool important = false;
        const size_t bang = value.rfind('!');
        if (bang != P::String::npos) {
            const P::String flag = Css::trim(std::string_view(value).substr(bang + 1));
            if (equalsNoCase(flag, "important")) {
                important = true;
                value = Css::trim(std::string_view(value).substr(0, bang));
            }
        }
        if (value.empty()) continue;
        decls.push_back(CssDecl{std::move(name), std::move(value), important});
    }
    return decls;
}

// ============ Selectors ============

// An identifier at i (class, id, tag or attribute name); i moves past it
std::string_view readName(std::string_view s, size_t& i) {
    const size_t start = i;
    while (i < s.size()) {
        if (s[i] == '\\' && i + 1 < s.size()) {  // an escaped character is a name character
            i += 2;
        } else if (isNameChar(s[i])) {
            ++i;
        } else {
            break;
        }
    }
    return s.substr(start, i - start);
}

// [name], [name=value], [name~="value" i]: i is right after '['
bool parseAttrTest(std::string_view s, size_t& i, AttrTest& t) {
    const size_t n = s.size();
    auto skip = [&] { while (i < n && isSpace(s[i])) ++i; };
    skip();
    const std::string_view name = readName(s, i);
    if (name.empty()) return false;
    t.name = lowered(name);
    skip();
    if (i < n && s[i] == ']') {
        ++i;
        return true;
    }
    if (i < n && s[i] == '=') {
        t.op = '=';
        ++i;
    } else if (i + 1 < n && s[i + 1] == '=' && std::string_view("~|^$*").find(s[i]) != std::string_view::npos) {
        t.op = s[i];
        i += 2;
    } else {
        return false;
    }
    skip();
    if (i < n && (s[i] == '"' || s[i] == '\'')) {
        const char q = s[i++];
        const size_t vs = i;
        while (i < n && s[i] != q) ++i;
        if (i >= n) return false;
        t.value.assign(s.data() + vs, i - vs);
        ++i;
    } else {
        const std::string_view v = readName(s, i);
        if (v.empty()) return false;
        t.value.assign(v.data(), v.size());
    }
    skip();
    if (i < n && (s[i] == 'i' || s[i] == 'I' || s[i] == 's' || s[i] == 'S')) {
        t.caseless = (s[i] == 'i' || s[i] == 'I');
        ++i;
        skip();
    }
    if (i >= n || s[i] != ']') return false;
    ++i;
    return true;
}

// :active and the others: the LVGL state a widget is in while it matches; false for any other
bool pseudoState(std::string_view name, uint16_t& state) {
    if (equalsNoCase(name, "active")) state = LV_STATE_PRESSED;
    else if (equalsNoCase(name, "focus") || equalsNoCase(name, "focus-visible")) state = LV_STATE_FOCUSED;
    else if (equalsNoCase(name, "checked")) state = LV_STATE_CHECKED;
    else if (equalsNoCase(name, "disabled")) state = LV_STATE_DISABLED;
    else if (equalsNoCase(name, "hover")) state = LV_STATE_PRESSED;  // a touch screen has no hover: a tap, as on a phone
    else return false;
    return true;
}

// The argument of :nth-child(): odd, even, 3, 2n+1, -n + 3
bool parseNth(std::string_view arg, NthTest& t) {
    P::String text;  // without spaces: 2n + 1 is 2n+1
    for (char c : arg) {
        if (!isSpace(c)) text += lowerAscii(c);
    }
    const std::string_view v(text);
    auto integer = [](std::string_view d, int& out) {  // 3, +3, -3
        size_t i = (!d.empty() && (d[0] == '+' || d[0] == '-')) ? 1 : 0;
        if (i >= d.size()) return false;
        int x = 0;
        for (; i < d.size(); ++i) {
            if (d[i] < '0' || d[i] > '9' || x > 9999) return false;
            x = x * 10 + (d[i] - '0');
        }
        out = d[0] == '-' ? -x : x;
        return true;
    };
    int a = 0, b = 0;
    const size_t pn = v.find('n');
    if (v == "odd") {
        a = 2, b = 1;
    } else if (v == "even") {
        a = 2;
    } else if (pn == std::string_view::npos) {
        if (!integer(v, b)) return false;
    } else {
        const std::string_view as = v.substr(0, pn), bs = v.substr(pn + 1);
        if (as.empty() || as == "+") a = 1;
        else if (as == "-") a = -1;
        else if (!integer(as, a)) return false;
        if (!bs.empty() && (bs[0] != '+' && bs[0] != '-')) return false;
        if (!bs.empty() && !integer(bs, b)) return false;
    }
    t.a = static_cast<int16_t>(a);
    t.b = static_cast<int16_t>(b);
    return true;
}

// :nth-child( and the others that take an argument; false for any other name
bool nthFunction(std::string_view name, NthTest& t) {
    if (equalsNoCase(name, "nth-child")) return true;
    if (equalsNoCase(name, "nth-last-child")) return t.fromEnd = true;
    if (equalsNoCase(name, "nth-of-type")) return t.ofType = true;
    if (equalsNoCase(name, "nth-last-of-type")) return t.fromEnd = t.ofType = true;
    return false;
}

// :first-child, :last-child, :only-child and their -of-type forms; false for any other name
bool nthKeyword(std::string_view name, P::Array<NthTest>& out) {
    auto ends = [name](std::string_view tail) {
        return name.size() > tail.size() && equalsNoCase(name.substr(name.size() - tail.size()), tail);
    };
    const bool ofType = ends("-of-type");
    if (!ofType && !ends("-child")) return false;
    const std::string_view base = name.substr(0, name.size() - (ofType ? 8 : 6));
    NthTest first, last;
    first.ofType = last.ofType = ofType;
    last.fromEnd = true;
    if (equalsNoCase(base, "first")) out.push_back(first);
    else if (equalsNoCase(base, "last")) out.push_back(last);
    else if (equalsNoCase(base, "only")) out.push_back(first), out.push_back(last);
    else return false;
    return true;
}

// The ')' that closes the '(' at i, past nested parentheses, brackets and quotes; npos if none
size_t closingParen(std::string_view s, size_t i) {
    int depth = 0;
    char quote = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && --depth == 0) {
            return c == ')' ? i : std::string_view::npos;
        }
    }
    return std::string_view::npos;
}

// a, b:is(c, d), [e=","]: the items of a selector list, split at the commas outside (), [] and quotes
P::Array<std::string_view> selectorList(std::string_view s) {
    P::Array<std::string_view> items;
    int depth = 0;
    char quote = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        const char c = i < s.size() ? s[i] : ',';
        if (quote) {
            if (c == quote) quote = 0;
            if (i < s.size()) continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(' || c == '[') ++depth;
        else if ((c == ')' || c == ']') && depth > 0) --depth;
        else if (c == ',' && depth == 0) {
            items.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return items;
}

bool parseSelector(std::string_view s, Selector& out);

// The argument of :not(), :is(), :where(): selectors that read and that LVGL can match. A state
// (:checked) or a pseudo-element inside them is not: LVGL has no style for "not pressed"
bool logicalList(std::string_view arg, bool forgiving, Logical& l, uint32_t& specificity) {
    specificity = 0;
    for (std::string_view item : selectorList(arg)) {
        Selector one;
        bool ok = parseSelector(item, one);
        for (const Compound& c : one.parts) ok = ok && !c.states && c.pseudo == Pseudo::None;
        if (!ok) {
            if (!forgiving) return false;  // :not() is not forgiving: one that does not read, and it is out
            continue;                      // :is() and :where() leave that one out, as a browser does
        }
        specificity = std::max(specificity, one.specificity);
        l.list.push_back(std::move(one));
    }
    return forgiving || !l.list.empty();
}

// The compound's rare parts, made when the first one is parsed
CssDetail::CompoundExtra& extraOf(Compound& c) {
    if (!c.extra) c.extra = P::create<CssDetail::CompoundExtra>();
    return *c.extra;
}

// Specificities add up per component: ids, classes, types
uint32_t addSpecificity(uint32_t a, uint32_t b) {
    uint32_t out = 0;
    for (int shift : {16, 8, 0}) out |= std::min(((a >> shift) & 0xFF) + ((b >> shift) & 0xFF), 255u) << shift;
    return out;
}

// One compound selector at i: a type or *, then #id, .class, [attr] and pseudo-classes in any order
bool parseCompound(std::string_view s, size_t& i, Compound& c) {
    const size_t start = i;
    const size_t n = s.size();
    if (i < n && s[i] == '*') {
        ++i;
    } else if (i < n && isNameChar(s[i])) {
        c.tag = lowered(readName(s, i));
    }
    while (i < n) {
        const char ch = s[i];
        if (ch == '#' || ch == '.') {
            ++i;
            const std::string_view name = readName(s, i);
            if (name.empty()) return false;
            if (ch == '.') {
                c.classes.push_back(P::String(name.data(), name.size()));
            } else if (c.id.empty()) {
                c.id.assign(name.data(), name.size());
            } else {  // #a#b: both must hold
                AttrTest t;
                t.name = "id";
                t.op = '=';
                t.value.assign(name.data(), name.size());
                c.attrs.push_back(std::move(t));
            }
        } else if (ch == '[') {
            ++i;
            AttrTest t;
            if (!parseAttrTest(s, i, t)) return false;
            c.attrs.push_back(std::move(t));
        } else if (ch == ':' && i + 1 < n && s[i + 1] == ':') {  // ::placeholder; ::before and the others are not supported
            size_t j = i + 2;
            while (j < n && isNameChar(s[j])) ++j;
            if (!equalsNoCase(s.substr(i + 2, j - i - 2), "placeholder")) return false;
            c.pseudo = Pseudo::Placeholder;
            i = j;
            break;  // nothing may follow it in the compound
        } else if (ch == ':') {  // :root, the states, the places, :not() :is() :where(); anything else is not supported
            size_t j = i + 1;
            while (j < n && isNameChar(s[j])) ++j;
            const std::string_view pc = s.substr(i + 1, j - i - 1);
            uint16_t state = 0;
            NthTest t;
            const bool logical = equalsNoCase(pc, "not") || equalsNoCase(pc, "is") || equalsNoCase(pc, "where");
            if (equalsNoCase(pc, "root")) {
                c.root = true;
            } else if (pseudoState(pc, state)) {
                c.states = static_cast<uint16_t>(c.states | state);
            } else if (j < n && s[j] == '(' && nthFunction(pc, t)) {
                const size_t close = closingParen(s, j);
                if (close == std::string_view::npos || !parseNth(s.substr(j + 1, close - j - 1), t)) return false;
                extraOf(c).nths.push_back(t);
                j = close + 1;
            } else if (j < n && s[j] == '(' && logical) {
                const size_t close = closingParen(s, j);
                Logical l;
                l.negate = equalsNoCase(pc, "not");
                uint32_t spec = 0;
                if (close == std::string_view::npos || !logicalList(s.substr(j + 1, close - j - 1), !l.negate, l, spec)) {
                    return false;
                }
                CssDetail::CompoundExtra& x = extraOf(c);
                if (!equalsNoCase(pc, "where")) x.logicSpecificity = addSpecificity(x.logicSpecificity, spec);
                x.logic.push_back(std::move(l));
                i = close + 1;
                continue;  // its list's specificity counts, not the pseudo-class itself
            } else {
                P::Array<NthTest> keyword;
                if (!nthKeyword(pc, keyword)) break;
                for (const NthTest& k : keyword) extraOf(c).nths.push_back(k);
            }
            ++c.pseudos;
            i = j;
        } else {
            break;  // a combinator, the end, or something not supported (':' is checked by the caller)
        }
    }
    return i > start;
}

// One complex selector (no commas): compounds joined by ' ' (descendant), '>' (child), '+' (the
// next sibling) and '~' (a later sibling). false for anything not supported: :not(), ::before.
bool parseSelector(std::string_view s, Selector& out) {
    size_t i = 0;
    const size_t n = s.size();
    auto skip = [&] {
        const size_t before = i;
        while (i < n && isSpace(s[i])) ++i;
        return i > before;
    };
    skip();
    if (i >= n) return false;
    while (true) {
        Compound c;
        if (!parseCompound(s, i, c)) return false;
        out.parts.push_back(std::move(c));
        const bool space = skip();
        if (i >= n) break;
        char comb = ' ';
        if (s[i] == '>' || s[i] == '+' || s[i] == '~') {
            comb = s[i];
            ++i;
            skip();
        } else if (!space) {
            return false;  // ':' that did not read, or a stray character
        }
        if (i >= n) return false;  // nothing after the combinator
        out.combinators += comb;
    }
    for (size_t k = 0; k + 1 < out.parts.size(); ++k) {
        if (out.parts[k].states) return false;  // button:active label: an ancestor's state is not an LVGL style
        if (out.parts[k].pseudo != Pseudo::None) return false;  // a pseudo-element ends the selector
    }
    if (out.parts.back().pseudo != Pseudo::None && out.parts.back().states) return false;  // input:focus::placeholder
    unsigned ids = 0, classes = 0, types = 0;
    uint32_t logic = 0;
    for (const Compound& c : out.parts) {
        ids += !c.id.empty();
        classes += static_cast<unsigned>(c.classes.size()) + c.pseudos;  // pseudo-classes count as classes
        for (const AttrTest& t : c.attrs) {
            if (t.name == "id" && t.op == '=') ++ids;  // #a#b
            else ++classes;
        }
        types += !c.tag.empty() + (c.pseudo != Pseudo::None);  // a pseudo-element counts as a type
        if (c.extra) logic = addSpecificity(logic, c.extra->logicSpecificity);
    }
    out.specificity = addSpecificity(
        (std::min(ids, 255u) << 16) | (std::min(classes, 255u) << 8) | std::min(types, 255u), logic);
    return true;
}

// ============ Matching ============

bool matchAttr(const AttrTest& t, std::string_view attrs) {
    std::string_view v;
    if (!Css::findAttr(attrs, t.name, v)) return false;
    const std::string_view w(t.value);
    auto eq = [&](std::string_view a, std::string_view b) { return t.caseless ? equalsNoCase(a, b) : a == b; };
    switch (t.op) {
        case 0: return true;
        case '=': return eq(v, w);
        case '~': {  // one of the space-separated words
            size_t i = 0;
            while (i < v.size()) {
                while (i < v.size() && isSpace(v[i])) ++i;
                const size_t s = i;
                while (i < v.size() && !isSpace(v[i])) ++i;
                if (i > s && eq(v.substr(s, i - s), w)) return true;
            }
            return false;
        }
        case '|': return eq(v, w) || (v.size() > w.size() && v[w.size()] == '-' && eq(v.substr(0, w.size()), w));
        case '^': return !w.empty() && v.size() >= w.size() && eq(v.substr(0, w.size()), w);
        case '$': return !w.empty() && v.size() >= w.size() && eq(v.substr(v.size() - w.size()), w);
        case '*': {
            if (w.empty() || v.size() < w.size()) return false;
            for (size_t i = 0; i + w.size() <= v.size(); ++i) {
                if (eq(v.substr(i, w.size()), w)) return true;
            }
            return false;
        }
        default: return false;
    }
}

// ============ Places among siblings ============

// An element child of a scope's element, as the scope's content reads
struct Child {
    P::String tag;           // lower-case
    std::string_view attrs;  // into the page text: tells the child from the others
};

// One per scopeStack() entry: the element children of its content, read the first time a
// selector asks for a place (li:first-child, h2 + p)
struct Children {
    bool read = false;
    P::Array<Child> items;
};

P::Array<CssElement>& scopeStack();
P::Array<Children>& childStack();

// The element children of anc[k], read as the page walker reads them (TagReader); nullptr when
// anc is not the scope stack or k is no scope
const P::Array<Child>* childrenOf(const P::Array<CssElement>& anc, int k) {
    if (k < 0 || &anc != &scopeStack() || static_cast<size_t>(k) >= childStack().size()) return nullptr;
    Children& c = childStack()[static_cast<size_t>(k)];
    if (!c.read) {
        c.read = true;
        TagReader r(anc[static_cast<size_t>(k)].content);
        StartTag t;
        for (TagReader::Step step = r.next(t); step != TagReader::Step::End; step = r.next(t)) {
            if (step != TagReader::Step::Tag) continue;
            c.items.push_back(Child{t.name, t.attrs.text()});
            r.content(t);  // its own children are not ours
        }
    }
    return &c.items;
}

// el's index among kids, found by where its attributes are in the page; npos if it is not there
// (an element built from other text: a template, a widget an HTML input becomes)
size_t placeOf(const CssElement& el, const P::Array<Child>& kids) {
    for (size_t i = 0; i < kids.size(); ++i) {
        if (kids[i].attrs.data() == el.attrs.data() && el.attrs.data()) return i;
    }
    return std::string_view::npos;
}

bool nthMatches(const NthTest& t, int i) {  // i counts from 1
    if (t.a == 0) return i == t.b;
    const int d = i - t.b;
    return d % t.a == 0 && d / t.a >= 0;
}

// :first-child, :nth-child() and the others, for el, a child of anc[parent]
bool matchPlace(const Compound& c, const CssElement& el, int parent, const P::Array<CssElement>& anc) {
    const P::Array<Child>* kids = childrenOf(anc, parent);
    const size_t at = kids ? placeOf(el, *kids) : std::string_view::npos;
    if (at == std::string_view::npos) return false;
    for (const NthTest& t : c.extra->nths) {
        int index = 0, count = 0;
        for (size_t k = 0; k < kids->size(); ++k) {
            if (t.ofType && (*kids)[k].tag != (*kids)[at].tag) continue;
            ++count;
            if (k <= at) ++index;
        }
        if (!nthMatches(t, t.fromEnd ? count - index + 1 : index)) return false;
    }
    return true;
}

// A sibling as a selector sees it
CssElement elementOf(const Child& c) {
    CssElement e;
    e.tag = c.tag;
    e.attrs = c.attrs;
    Css::findAttr(c.attrs, "id", e.id);
    Css::findAttr(c.attrs, "class", e.classes);
    return e;
}

bool matchPart(const Selector& s, int part, const CssElement& el, int parent, const P::Array<CssElement>& anc);

// el, a child of anc[parent] (-1: no parent known)
bool matchCompound(const Compound& c, const CssElement& el, int parent, const P::Array<CssElement>& anc) {
    if (c.root && !el.root) return false;  // :root; the root itself (tag html) also takes html {} and * {}
    if (!c.tag.empty() && el.tag != std::string_view(c.tag)) return false;
    if (!c.id.empty() && el.id != std::string_view(c.id)) return false;
    for (const P::String& cls : c.classes) {
        if (!hasWord(el.classes, cls)) return false;
    }
    for (const AttrTest& t : c.attrs) {
        if (t.name == "id" && t.op == '=' && !t.caseless) {  // the element's id may be a generated one
            if (el.id != std::string_view(t.value)) return false;
        } else if (t.name == "class" && t.op == '~' && !t.caseless) {
            if (!hasWord(el.classes, t.value)) return false;
        } else if (!matchAttr(t, el.attrs)) {
            return false;
        }
    }
    if (!c.extra) return true;
    if (!c.extra->nths.empty() && !matchPlace(c, el, parent, anc)) return false;
    for (const Logical& l : c.extra->logic) {  // :not(), :is(), :where(): the list where el stands
        bool any = false;
        for (const Selector& s : l.list) {
            if (matchPart(s, static_cast<int>(s.parts.size()) - 1, el, parent, anc)) {
                any = true;
                break;
            }
        }
        if (any == l.negate) return false;
    }
    return true;
}

// parts[0..part] hold, parts[part] for el, a child of anc[parent]
bool matchPart(const Selector& s, int part, const CssElement& el, int parent, const P::Array<CssElement>& anc) {
    if (!matchCompound(s.parts[static_cast<size_t>(part)], el, parent, anc)) return false;
    if (part == 0) return true;
    const char comb = s.combinators[static_cast<size_t>(part - 1)];
    if (comb == '>') {  // the parent itself
        return parent >= 0 && matchPart(s, part - 1, anc[static_cast<size_t>(parent)], parent - 1, anc);
    }
    if (comb == '+' || comb == '~') {  // the sibling just before, or any before
        const P::Array<Child>* kids = childrenOf(anc, parent);
        const size_t at = kids ? placeOf(el, *kids) : std::string_view::npos;
        if (at == std::string_view::npos) return false;
        for (size_t k = at; k-- > 0;) {
            if (matchPart(s, part - 1, elementOf((*kids)[k]), parent, anc)) return true;
            if (comb == '+') break;
        }
        return false;
    }
    for (int k = parent; k >= 0; --k) {  // any ancestor; a farther one may be the one that fits the rest
        if (matchPart(s, part - 1, anc[static_cast<size_t>(k)], k - 1, anc)) return true;
    }
    return false;
}

// top: the nearest ancestor's index in anc
bool matchSelector(const Selector& s, const CssElement& el, const P::Array<CssElement>& anc, int top) {
    const int last = static_cast<int>(s.parts.size()) - 1;
    return last >= 0 && matchPart(s, last, el, top, anc);
}

// The same element: the same tag and the same attribute text (views into the same page)
bool sameElement(const CssElement& a, const CssElement& b) {
    return a.tag == b.tag && a.attrs.data() == b.attrs.data() && a.attrs.size() == b.attrs.size();
}

// A part of s looks at attribute `name` (lower-case): .x is class, #x is id
bool looksAtAttr(const Selector& s, std::string_view name) {
    for (const Compound& c : s.parts) {
        if ((name == "class" && !c.classes.empty()) || (name == "id" && !c.id.empty())) return true;
        for (const AttrTest& t : c.attrs) {
            if (std::string_view(t.name) == name) return true;
        }
        if (!c.extra) continue;
        for (const Logical& l : c.extra->logic) {
            for (const Selector& inner : l.list) {
                if (looksAtAttr(inner, name)) return true;
            }
        }
    }
    return false;
}

// ============ Scopes ============

P::Array<CssElement>& scopeStack() {
    static P::Array<CssElement> s_stack;
    return s_stack;
}

// The custom properties each scope's element declares, var()s put in: one entry per scopeStack() entry
P::Array<P::Array<CssDecl>>& customStack() {
    static P::Array<P::Array<CssDecl>> s_stack;
    return s_stack;
}

P::Array<Children>& childStack() {
    static P::Array<Children> s_stack;
    return s_stack;
}

// ============ var() ============

constexpr int kMaxVarDepth = 8;  // --a: var(--b) chains; a cycle ends here

// Where var( starts, at i or after it, in any case and not as the end of a longer name; npos if nowhere
size_t findVar(std::string_view v, size_t i) {
    for (; i + 4 <= v.size(); ++i) {
        if (v[i + 3] == '(' && equalsNoCase(v.substr(i, 3), "var") && (i == 0 || !isNameChar(v[i - 1]))) return i;
    }
    return std::string_view::npos;
}

bool hasVar(std::string_view v) { return findVar(v, 0) != std::string_view::npos; }

const P::String* findDecl(const P::Array<CssDecl>& decls, std::string_view name) {
    for (const CssDecl& d : decls) {
        if (std::string_view(d.name) == name) return &d.value;
    }
    return nullptr;
}

// Puts values into var(--name, fallback): the element's own custom property first, then its
// ancestors' (the nearest first), then :root's
struct VarResolver {
    P::Array<CssDecl>& own;                        // the element's winning declarations
    const P::Array<P::Array<CssDecl>>& inherited;  // per ancestor, the outermost first
    int top;                                       // the nearest ancestor's index in inherited
    const P::Array<CssDecl>* root;                 // nullptr while :root's own are worked out

    // The value of custom property `name`, its own var()s put in; nullptr if it has none
    const P::String* find(std::string_view name, int depth) {
        for (CssDecl& d : own) {
            if (std::string_view(d.name) != name) continue;
            if (hasVar(d.value)) {
                const P::String source = d.value;  // a copy: a cycle may reach d again further down
                P::String done;
                if (depth >= kMaxVarDepth || !substitute(source, done, depth + 1)) return nullptr;
                d.value = std::move(done);
            }
            return &d.value;
        }
        for (int k = top; k >= 0; --k) {
            if (const P::String* v = findDecl(inherited[static_cast<size_t>(k)], name)) return v;
        }
        return root ? findDecl(*root, name) : nullptr;
    }

    // value with each var() put in; false if one has neither a value nor a fallback
    bool substitute(std::string_view value, P::String& out, int depth) {
        out.clear();
        size_t i = 0;
        while (true) {
            const size_t at = findVar(value, i);
            if (at == std::string_view::npos) {
                out.append(value.data() + i, value.size() - i);
                return true;
            }
            out.append(value.data() + i, at - i);
            size_t close = at + 4;
            for (int open = 1; close < value.size(); ++close) {
                if (value[close] == '(') ++open;
                else if (value[close] == ')' && --open == 0) break;
            }
            if (close >= value.size()) return false;  // var( without its ')'
            const std::string_view args = value.substr(at + 4, close - at - 4);
            const size_t comma = args.find(',');
            const std::string_view name = trimmed(args.substr(0, comma));
            if (!isCustom(name)) return false;
            if (const P::String* v = find(name, depth)) {
                out += *v;
            } else if (comma != std::string_view::npos) {
                P::String fallback;
                if (depth >= kMaxVarDepth || !substitute(trimmed(args.substr(comma + 1)), fallback, depth + 1)) {
                    return false;
                }
                out += fallback;
            } else {
                return false;
            }
            i = close + 1;
        }
    }
};

// var() in an element's winning declarations. A declaration whose var() has no value is left out:
// it is invalid, as in CSS (it then works as if it were not there: the property is inherited or initial).
void resolveVars(P::Array<CssDecl>& decls, const P::Array<P::Array<CssDecl>>& inherited, int top,
                 const P::Array<CssDecl>* root) {
    VarResolver r{decls, inherited, top, root};
    for (CssDecl& d : decls) {
        if (!hasVar(d.value)) continue;
        if (isCustom(d.name)) {
            r.find(d.name, 0);  // puts its value in place, if it has one
            continue;
        }
        const P::String source = d.value;
        P::String done;
        if (r.substitute(source, done, 0)) d.value = std::move(done);
        else d.name.clear();
    }
    decls.erase(std::remove_if(decls.begin(), decls.end(),
                               [](const CssDecl& d) { return d.name.empty() || (isCustom(d.name) && hasVar(d.value)); }),
                decls.end());
}

// ============ Values of the properties ============

// The size of the font an object's text has (its own or its parent's): one of the firmware's
float fontPxOf(lv_obj_t* o) {
    return static_cast<float>(o ? UI::Font::sizeOf(lv_obj_get_style_text_font(o, LV_PART_MAIN)) : kCssRemPx);
}

// font: 32 (TelaOS) or font: bold 12px/1.5 Arial (CSS): its size in px, 0 if it has none. A bare
// number among several parts is a weight (700), not a size.
// The weight in a font shorthand, before its size: font: bold 16px, font: 600 1em; 0 if none
int fontShorthandWeight(std::string_view value) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.size() < 2) return 0;  // font: 16px, or a system font name
    for (std::string_view p : parts) {
        if (const int w = cssFontWeight(p)) return w;
        if (!p.empty() && (std::isdigit(static_cast<unsigned char>(p[0])) || p[0] == '.')) break;  // the size
    }
    return 0;
}

// The style in the font shorthand: font: italic bold 16px sans-serif; 0 for none
int fontShorthandStyle(std::string_view value) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.size() < 2) return 0;
    for (std::string_view p : parts) {
        if (equalsNoCase(p, "italic") || equalsNoCase(p, "oblique")) return kCssItalic;
        if (!p.empty() && (std::isdigit(static_cast<unsigned char>(p[0])) || p[0] == '.')) break;  // the size
    }
    return 0;
}

int fontShorthandSize(std::string_view value, float parentPx) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.size() == 1) return cssFontSize(parts[0], parentPx);
    for (std::string_view p : parts) {
        p = p.substr(0, p.find('/'));  // 12px/1.5: the size, then the line height
        if (p.empty() || std::isdigit(static_cast<unsigned char>(p.back())) || p.back() == '.') continue;
        if (const int px = cssFontSize(p, parentPx)) return px;
    }
    return 0;
}

// 1 to maxCount lengths, none negative: padding: 4px 8px. false if one does not read.
bool lengthList(std::string_view value, float em, size_t maxCount, std::array<int32_t, 4>& v, size_t& count) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.empty() || parts.size() > maxCount) return false;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (!cssLength(parts[k], em, v[k]) || v[k] < 0) return false;
    }
    count = parts.size();
    return true;
}

bool oneLength(std::string_view value, float em, int32_t& px) {
    std::array<int32_t, 4> v{};
    size_t n = 0;
    if (!lengthList(value, em, 1, v, n)) return false;
    px = v[0];
    return true;
}

// width: 50% (of the parent, at layout time), 10em, 120px
bool sizeValue(std::string_view value, float em, int32_t& out) {
    float v = 0;
    if (cssPercent(value, v)) {
        if (v < 0) return false;
        out = lv_pct(static_cast<int32_t>(std::lround(v)));
        return true;
    }
    return cssLength(value, em, out) && out >= 0;
}

// border-radius: 8px, 50% (a circle); LVGL has one radius for all corners, so the first value
bool radiusValue(std::string_view value, float em, int32_t& out) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.empty()) return false;
    const std::string_view first = parts[0].substr(0, parts[0].find('/'));  // 8px/4px: elliptical
    float v = 0;
    if (cssPercent(first, v)) {  // the box's size is not known yet: only a circle reads
        if (v < 50) return false;
        out = LV_RADIUS_CIRCLE;
        return true;
    }
    return cssLength(first, em, out) && out >= 0;
}

// opacity: 0.5 or 50%
bool opacityValue(std::string_view value, uint8_t& out) {
    value = trimmed(value);
    size_t i = 0;
    float a = 0;
    if (!cssNumber(value, i, a)) return false;
    if (value.substr(i) == "%") a /= 100;
    else if (i != value.size()) return false;
    out = static_cast<uint8_t>(std::lround(std::min(std::max(a, 0.0f), 1.0f) * 255));
    return true;
}

// background: the color in it (background: url(x.png) #222 → #222), none → transparent;
// background-color: a color
bool backgroundColor(const P::String& name, std::string_view value, CssColor& c) {
    if (name != "background") return cssColor(value, c);
    for (std::string_view p : cssParts(value)) {
        if (cssColor(p, c)) return true;
    }
    if (equalsNoCase(trimmed(value), "none")) {
        c = CssColor{0x000000, 0};
        return true;
    }
    return false;
}

// ============ Text vertical alignment via padding-top ============

void applyTextValignCSS(lv_obj_t* obj, const P::String& valign) {
    if (!obj || valign.empty() || valign == "top") return;
    int32_t objH = lv_obj_get_height(obj);
    if (objH <= 0) return;
    const lv_font_t* f = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    if (!f) f = lv_font_default();
    int lineH = lv_font_get_line_height(f);
    int padBottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    int available = objH - padBottom;
    if (valign == "center") {
        int pad = (available - lineH) / 2;
        if (pad > 0) lv_obj_set_style_pad_top(obj, pad, 0);
    } else if (valign == "bottom") {
        int pad = available - lineH;
        if (pad > 0) lv_obj_set_style_pad_top(obj, pad, 0);
    }
}

}  // namespace

// ============ Public helpers ============

Css& Css::instance() {
    static Css s_instance;
    return s_instance;
}

void Css::clear() {
    m_rules.clear();
    m_nextOrder = 0;
    m_hasCustom = false;
    m_hasPlaceholder = false;
    m_rootCustom.clear();
    m_rootReady = false;
}

P::String Css::trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return P::String(s.data() + start, end - start);
}

bool Css::findAttr(std::string_view attrs, std::string_view name, std::string_view& value) {
    bool found = false;
    forEachAttr(attrs, [&](std::string_view n, std::string_view v) {
        if (!equalsNoCase(n, name)) return false;
        value = v;
        found = true;
        return true;  // the first one counts, as in HTML
    });
    return found;
}

P::Array<CssDecl> Css::declarations(std::string_view block) { return parseDecls(block); }

CssScope::CssScope(std::string_view tag, std::string_view attrs, std::string_view content) {
    CssElement el;
    el.tag = tag;
    el.attrs = attrs;
    el.content = content;
    Css::findAttr(attrs, "id", el.id);
    Css::findAttr(attrs, "class", el.classes);
    customStack().push_back(Css::instance().customProperties(el));  // before el is its own ancestor
    scopeStack().push_back(el);
    childStack().push_back(Children{});
    dom_ = Dom::enter(tag, attrs);
}

CssScope::CssScope(std::string_view tag, std::string_view attrs, const P::Array<CssChild>& children)
    : CssScope(tag, attrs) {
    Children& kids = childStack().back();
    kids.read = true;
    for (const CssChild& c : children) kids.items.push_back(Child{P::String(c.tag), c.attrs});
}

CssScope::~CssScope() {
    if (dom_) Dom::leave();
    scopeStack().pop_back();
    customStack().pop_back();
    childStack().pop_back();
}

const P::Array<CssElement>& CssScope::ancestors() { return scopeStack(); }

CssQuery::CssQuery(std::string_view selectors) {
    for (std::string_view item : selectorList(selectors)) {
        Selector one;
        if (!parseSelector(item, one)) {  // "", a[, ::before: the whole list is wrong
            list_.clear();
            return;
        }
        list_.push_back(std::move(one));
    }
}

bool CssQuery::matches(const CssElement& el, uint16_t states) const {
    const P::Array<CssElement>& anc = scopeStack();
    for (const Selector& s : list_) {
        const Compound& last = s.parts.back();
        if (last.pseudo != Pseudo::None) continue;  // ::placeholder is no element
        if ((states & last.states) != last.states) continue;
        if (matchSelector(s, el, anc, static_cast<int>(anc.size()) - 1)) return true;
    }
    return false;
}

CssScopeText::CssScopeText(std::string_view original, std::string_view text) {
    P::Array<CssElement>& st = scopeStack();
    if (st.empty() || text.data() == original.data()) return;
    CssElement& top = st.back();
    if (top.content.data() != original.data() || top.content.size() != original.size()) return;
    saved_ = top.content;
    top.content = text;
    childStack().back() = Children{};  // read again, from text
    active_ = true;
}

CssScopeText::~CssScopeText() {
    if (!active_ || scopeStack().empty()) return;
    scopeStack().back().content = saved_;
    childStack().back() = Children{};  // its views were into text
}

// ============ Parse CSS ============

void Css::parse(std::string_view css) {
    size_t pos = 0;
    while (pos < css.size()) {
        while (pos < css.size() && std::isspace(static_cast<unsigned char>(css[pos]))) pos++;

        // Skip /* ... */ comments
        if (pos + 1 < css.size() && css[pos] == '/' && css[pos + 1] == '*') {
            const size_t close = css.find("*/", pos + 2);
            pos = close == std::string_view::npos ? css.size() : close + 2;
            continue;
        }
        if (pos >= css.size()) break;

        // Selector (or an at-rule's prelude): up to '{', or up to ';' for @import and the like
        size_t selStart = pos;
        while (pos < css.size() && css[pos] != '{' && !(css[selStart] == '@' && css[pos] == ';')) pos++;
        if (pos >= css.size()) break;
        if (css[pos] == ';') {  // @import url(x.css); @charset "utf-8";
            pos++;
            continue;
        }
        P::String selector = trim(css.substr(selStart, pos - selStart));
        pos++;

        // Block, with nested braces
        size_t propsStart = pos;
        int braceCount = 1;
        while (pos < css.size() && braceCount > 0) {
            if (css[pos] == '{') braceCount++;
            else if (css[pos] == '}') braceCount--;
            pos++;
        }
        const size_t blockLen = braceCount == 0 ? pos - propsStart - 1 : pos - propsStart;
        if (!selector.empty() && selector[0] == '@') {
            // @media: its rules count where they stand when the screen fits the query; @keyframes,
            // @supports and the others are left out
            const std::string_view at(selector);
            if (at.size() >= 6 && equalsNoCase(at.substr(0, 6), "@media") && (at.size() == 6 || !isNameChar(at[6])) &&
                cssMediaMatches(at.substr(6))) {
                parse(css.substr(propsStart, blockLen));
            }
            continue;
        }
        P::Array<CssDecl> decls = parseDecls(css.substr(propsStart, blockLen));
        if (decls.empty() || selector.empty()) continue;

        Rule rule;
        // h1, h2: a list; a selector this engine cannot read is left out, the others stay
        for (std::string_view item : selectorList(selector)) {
            Selector one;
            if (parseSelector(item, one)) {
                if (one.parts.back().pseudo == Pseudo::Placeholder) m_hasPlaceholder = true;
                rule.selectors.push_back(std::move(one));
            } else {
                LOG_D(Log::UI, "CSS: selector '%.*s' not supported, left out", (int)item.size(), item.data());
            }
        }
        if (rule.selectors.empty()) continue;
        for (const CssDecl& d : decls) {
            if (isCustom(d.name)) m_hasCustom = true;
        }
        m_rootReady = false;  // :root's custom properties may have changed
        rule.decls = std::move(decls);
        rule.order = m_nextOrder++;

        LOG_D(Log::UI, "CSS '%s' { %d props }", selector.c_str(), (int)rule.decls.size());
        m_rules.push_back(std::move(rule));
    }
}

// ============ Cascade ============

P::Array<CssStateDecls> Css::cascadeStates(const CssElement& el, std::string_view inlineStyle) const {
    const P::Array<CssElement>& anc = CssScope::ancestors();
    // A paragraph's text is built inside its own scope (its widgets need it as their ancestor):
    // when it styles itself, it is not its own ancestor
    int top = static_cast<int>(anc.size()) - 1;
    if (top >= 0 && el.attrs.data() && sameElement(anc[static_cast<size_t>(top)], el)) --top;
    return cascadeIn(el, inlineStyle, anc, top, customStack(), true);
}

P::Array<CssDecl> Css::cascade(const CssElement& el, std::string_view inlineStyle) const {
    P::Array<CssStateDecls> groups = cascadeStates(el, inlineStyle);
    if (groups.empty() || groups[0].state != 0) return {};
    return std::move(groups[0].decls);
}

P::Array<CssDecl> Css::customProperties(const CssElement& el) const {
    if (!m_hasCustom && el.attrs.find("--") == std::string_view::npos) return {};  // most pages
    std::string_view style;
    findAttr(el.attrs, "style", style);
    P::Array<CssDecl> own = cascade(el, style);
    own.erase(std::remove_if(own.begin(), own.end(), [](const CssDecl& d) { return !isCustom(d.name); }), own.end());
    return own;
}

const P::Array<CssDecl>& Css::rootCustom() const {
    if (!m_rootReady) {
        m_rootReady = true;
        m_rootCustom.clear();
        if (m_hasCustom) {
            CssElement root;
            root.tag = "html";
            root.root = true;
            static const P::Array<CssElement> kNoAncestors;
            static const P::Array<P::Array<CssDecl>> kNoCustom;
            P::Array<CssStateDecls> groups = cascadeIn(root, {}, kNoAncestors, -1, kNoCustom, false);
            if (!groups.empty() && groups[0].state == 0) {
                for (CssDecl& d : groups[0].decls) {
                    if (isCustom(d.name)) m_rootCustom.push_back(std::move(d));
                }
            }
        }
    }
    return m_rootCustom;
}

P::Array<CssStateDecls> Css::cascadeIn(const CssElement& el, std::string_view inlineStyle,
                                       const P::Array<CssElement>& anc, int top,
                                       const P::Array<P::Array<CssDecl>>& ancCustom, bool withRoot,
                                       Pseudo pseudo) const {
    struct Hit {
        const CssDecl* decl;
        uint16_t state;  // 0, or the LVGL state of :active and the like
        uint8_t layer;   // 0 sheets, 1 style="", 2 sheets !important, 3 style="" !important
        uint32_t specificity;
        uint32_t order;  // rule order, then the declaration's place in its rule
    };
    P::Array<Hit> hits;
    for (const Rule& r : m_rules) {
        // A list applies with its most specific matching selector, for each state it names:
        // .btn, .btn:active { } is a rule for the button as it is and for it pressed
        std::array<std::pair<uint16_t, uint32_t>, 8> matched{};
        size_t count = 0;
        for (const Selector& s : r.selectors) {
            if (s.parts.back().pseudo != pseudo || !matchSelector(s, el, anc, top)) continue;
            const uint16_t state = s.parts.back().states;
            size_t k = 0;
            while (k < count && matched[k].first != state) ++k;
            if (k < count) matched[k].second = std::max(matched[k].second, s.specificity);
            else if (count < matched.size()) matched[count++] = {state, s.specificity};
        }
        for (size_t m = 0; m < count; ++m) {
            for (size_t k = 0; k < r.decls.size(); ++k) {
                const CssDecl& d = r.decls[k];
                hits.push_back(Hit{&d, matched[m].first, static_cast<uint8_t>(d.important ? 2 : 0), matched[m].second,
                                   (r.order << 8) | static_cast<uint32_t>(std::min<size_t>(k, 255))});
            }
        }
    }
    // style="" is the element's: a pseudo-element has none
    const P::Array<CssDecl> inlineDecls = pseudo == Pseudo::None ? parseDecls(inlineStyle) : P::Array<CssDecl>{};
    for (size_t k = 0; k < inlineDecls.size(); ++k) {
        const CssDecl& d = inlineDecls[k];
        hits.push_back(Hit{&d, 0, static_cast<uint8_t>(d.important ? 3 : 1), 0, static_cast<uint32_t>(k)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.state != b.state) return a.state < b.state;
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.specificity != b.specificity) return a.specificity < b.specificity;
        return a.order < b.order;
    });

    // For each state, one value per property: the last in cascade order; the result keeps that order
    P::Array<CssStateDecls> groups;
    for (size_t i = 0; i < hits.size();) {
        const uint16_t state = hits[i].state;
        P::Array<const CssDecl*> winners;
        for (; i < hits.size() && hits[i].state == state; ++i) {
            for (auto& w : winners) {
                if (w && w->name == hits[i].decl->name) w = nullptr;
            }
            winners.push_back(hits[i].decl);
        }
        CssStateDecls g;
        g.state = state;
        for (const CssDecl* w : winners) {
            if (w) g.decls.push_back(*w);
        }
        groups.push_back(std::move(g));
    }

    // var(): a state sees the element's own custom properties too (.btn { --c } .btn:active { color: var(--c) })
    const P::Array<CssDecl>* root = nullptr;
    for (CssStateDecls& g : groups) {
        bool vars = false;
        for (const CssDecl& d : g.decls) vars = vars || hasVar(d.value);
        if (!vars) continue;
        if (!root && withRoot) root = &rootCustom();
        if (g.state != 0 && groups[0].state == 0) {
            for (const CssDecl& d : groups[0].decls) {
                if (isCustom(d.name) && !findDecl(g.decls, d.name)) g.decls.push_back(d);
            }
        }
        resolveVars(g.decls, ancCustom, top, root);
    }
    return groups;
}

bool Css::looksAt(std::string_view name) const {
    const P::String n = lowered(name);
    for (const Rule& r : m_rules) {
        for (const Selector& s : r.selectors) {
            if (looksAtAttr(s, n)) return true;
        }
    }
    return false;
}

void Css::applyMatching(Widget& w, const CssElement& el, std::string_view inlineStyle, CssUndo* undo) const {
    if (!w.handle) return;
    if (m_rules.empty() && inlineStyle.empty()) return;
    const P::Array<CssStateDecls> groups = cascadeStates(el, inlineStyle);
    if (undo) undo->begin(w.handle);  // what CSS changes from here on is kept
    for (const CssStateDecls& g : groups) applyProps(w, g.decls, g.state);  // the element as it is first
    applyCssTransitions(w.handle, groups);
    if (m_hasPlaceholder && lv_obj_check_type(w.handle, &lv_textarea_class)) {
        // ::placeholder: the hint an empty field shows, a part LVGL draws apart
        const P::Array<CssElement>& anc = CssScope::ancestors();
        int top = static_cast<int>(anc.size()) - 1;
        if (top >= 0 && el.attrs.data() && sameElement(anc[static_cast<size_t>(top)], el)) --top;
        const P::Array<CssStateDecls> hint = cascadeIn(el, {}, anc, top, customStack(), true, Pseudo::Placeholder);
        if (!hint.empty() && hint[0].state == 0) applyCssPlaceholder(w.handle, hint[0].decls, fontPxOf(w.handle));
    }
    if (undo) undo->end(w.handle);
}

void Css::applyMatching(Widget& w, const char* tag, const char* id, const char* classNames) const {
    CssElement el;
    el.tag = tag ? tag : "";
    el.id = id ? id : "";
    el.classes = classNames ? classNames : "";
    applyMatching(w, el);
}

// ============ Apply properties to widget ============

void Css::applyProps(Widget& w, const P::Array<CssDecl>& decls, uint16_t state) const {
    lv_obj_t* const o = w.handle;
    if (!o) return;
    // A state (:active...) gets what LVGL can switch per state: colors, opacity, radius, the
    // border, outline, shadow, transform, text lines; the element as it is (state 0) gets all
    const bool base = state == 0;
    const lv_style_selector_t sel = LV_PART_MAIN | state;
    // em is the element's font size: the one font-size gives it, or the one it has (font=, inherited);
    // in font-size itself, em is the parent's
    const float parentEm = fontPxOf(lv_obj_get_parent(o));
    int fontPx = 0;
    int weight = 0;  // font-weight, or the weight of font: bold 16px
    int style = 0;   // font-style, or the style of font: italic 16px
    for (const CssDecl& d : decls) {
        if (!base) break;
        int px = 0;
        int w = 0;
        int s = 0;
        if (d.name == "font-size") px = cssFontSize(d.value, parentEm);
        else if (d.name == "font-weight") w = cssFontWeight(d.value);
        else if (d.name == "font-style") s = cssFontStyle(d.value);
        else if (d.name == "font") {
            px = fontShorthandSize(d.value, parentEm), w = fontShorthandWeight(d.value);
            s = fontShorthandStyle(d.value);
        }
        if (px > 0) fontPx = px;  // the later one wins; one that does not read is left out
        if (w > 0) weight = w;
        if (s > 0) style = s;
    }
    const float em = fontPx > 0 ? static_cast<float>(UI::Font::nearest(fontPx)) : fontPxOf(o);

    // Deferred text-valign — needs height to be set first
    P::String deferredTextValign;
    P::String deferredTextAlignV;  // vertical part of compound "center center"

    for (const CssDecl& d : decls) {
        const P::String& name = d.name;
        const std::string_view value = d.value;
        int32_t v = 0;
        std::array<int32_t, 4> list{};
        size_t n = 0;
        if (name == "color") {
            CssColor c;
            if (cssColor(value, c)) {
                lv_obj_set_style_text_color(o, lv_color_hex(c.rgb), sel);
                if (c.alpha < LV_OPA_COVER || !base) lv_obj_set_style_text_opa(o, c.alpha, sel);
            }
        } else if (name == "background" || name == "background-color" || name == "bgcolor") {
            CssColor c;
            if (backgroundColor(name, value, c)) {
                lv_obj_set_style_bg_color(o, lv_color_hex(c.rgb), sel);
                lv_obj_set_style_bg_opa(o, c.alpha, sel);
            }
        } else if (name == "border-radius" || name == "radius") {
            if (radiusValue(value, em, v)) lv_obj_set_style_radius(o, v, sel);
        } else if (name == "opacity") {
            uint8_t opa = 0;
            if (opacityValue(value, opa)) lv_obj_set_style_opa(o, opa, sel);
        } else if (!base) {
            continue;  // the rest is the layout and the text flow of the element itself
        } else if (name == "z-index") {
            g_core.app().setZIndex(o, atoi(d.value.c_str()));
        } else if (name == "width" || name == "w") {
            if (sizeValue(value, em, v)) lv_obj_set_width(o, v);
        } else if (name == "height" || name == "h") {
            if (sizeValue(value, em, v)) lv_obj_set_height(o, v);
        } else if (name == "min-height") {
            if (sizeValue(value, em, v)) lv_obj_set_style_min_height(o, v, 0);
        } else if (name == "max-height") {
            if (sizeValue(value, em, v)) lv_obj_set_style_max_height(o, v, 0);
        } else if (name == "min-width") {
            if (sizeValue(value, em, v)) lv_obj_set_style_min_width(o, v, 0);
        } else if (name == "max-width") {
            if (sizeValue(value, em, v)) lv_obj_set_style_max_width(o, v, 0);
        } else if (name == "padding") {  // top right bottom left; a missing side takes the opposite one
            if (lengthList(value, em, 4, list, n)) {
                if (n == 1) {
                    lv_obj_set_style_pad_all(o, list[0], 0);
                } else {
                    lv_obj_set_style_pad_top(o, list[0], 0);
                    lv_obj_set_style_pad_right(o, list[1], 0);
                    lv_obj_set_style_pad_bottom(o, n > 2 ? list[2] : list[0], 0);
                    lv_obj_set_style_pad_left(o, n > 3 ? list[3] : list[1], 0);
                }
            }
        } else if (name == "padding-left") {
            if (oneLength(value, em, v)) lv_obj_set_style_pad_left(o, v, 0);
        } else if (name == "padding-right") {
            if (oneLength(value, em, v)) lv_obj_set_style_pad_right(o, v, 0);
        } else if (name == "padding-top") {
            if (oneLength(value, em, v)) lv_obj_set_style_pad_top(o, v, 0);
        } else if (name == "padding-bottom") {
            if (oneLength(value, em, v)) lv_obj_set_style_pad_bottom(o, v, 0);
        } else if (name == "gap" || name == "border-spacing") {
            // gap: rows, then columns; border-spacing: horizontal, then vertical
            if (lengthList(value, em, 2, list, n)) {
                const int32_t second = n > 1 ? list[1] : list[0];
                const bool gap = name == "gap";
                lv_obj_set_style_pad_row(o, gap ? list[0] : second, 0);
                lv_obj_set_style_pad_column(o, gap ? second : list[0], 0);
            }
        } else if (name == "row-gap") {
            if (oneLength(value, em, v)) lv_obj_set_style_pad_row(o, v, 0);
        } else if (name == "column-gap") {
            if (oneLength(value, em, v)) lv_obj_set_style_pad_column(o, v, 0);
        } else if (name == "display") {
            if (equalsNoCase(value, "none")) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else if (name == "text-align") {
            P::String val(value.data(), value.size());
            auto space = val.find(' ');
            P::String hPart = (space != P::String::npos) ? val.substr(0, space) : val;
            if (hPart == "center")     lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
            else if (hPart == "right") lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_RIGHT, 0);
            else                       lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_LEFT, 0);
            if (space != P::String::npos)
                deferredTextAlignV = val.substr(space + 1);
        } else if (name == "text-valign") {
            deferredTextValign.assign(value.data(), value.size());
        }
    }
    applyCssBox(o, decls, em, state);        // margin, border, outline, box-shadow, gradients, overflow
    if (base) applyCssLayout(o, decls, em);  // flex, position and left/top/right/bottom, visibility
    applyCssText(o, decls, em, state);       // text-decoration, letter-spacing, line-height, one line
    applyCssTransform(o, decls, em, state);  // translate, scale, rotate
    if (!base) return;
    if (fontPx > 0 || weight > 0 || style > 0) {
        // the face: the weight and style given, or those the element has (font-size keeps a
        // heading bold, <address> italic)
        const lv_font_t* now = lv_obj_get_style_text_font(o, LV_PART_MAIN);
        const bool bold = weight > 0 ? weight >= 600 : UI::Font::isBold(now);
        const bool italic = style > 0 ? style == kCssItalic : UI::Font::isItalic(now);
        w.applyFont(fontPx > 0 ? fontPx : UI::Font::sizeOf(now), bold, italic);
    }

    // Apply text-valign AFTER height/font are set
    if (!deferredTextValign.empty())
        applyTextValignCSS(o, deferredTextValign);
    else if (!deferredTextAlignV.empty())
        applyTextValignCSS(o, deferredTextAlignV);
}

// ============ Legacy class-only API ============

const Rule* Css::findClass(const P::String& className) const {
    for (const auto& rule : m_rules) {
        for (const Selector& s : rule.selectors) {
            if (s.parts.size() != 1) continue;
            const Compound& c = s.parts[0];
            // just .className: not .className:active, :first-child, :not(), ::placeholder
            if (c.tag.empty() && c.id.empty() && c.attrs.empty() && c.classes.size() == 1 && c.classes[0] == className &&
                !c.root && !c.states && !c.extra && c.pseudo == Pseudo::None) {
                return &rule;
            }
        }
    }
    return nullptr;
}

bool Css::has(const P::String& className) const {
    return findClass(className) != nullptr;
}

const P::String& Css::prop(const P::String& className, const P::String& property) const {
    const Rule* r = findClass(className);
    if (!r) return s_emptyStr;
    const P::String* found = &s_emptyStr;
    for (const CssDecl& d : r->decls) {
        if (d.name == property) found = &d.value;  // the last one in the rule
    }
    return *found;
}

int Css::getInt(const P::String& className, const P::String& property, int def) const {
    auto val = prop(className, property);
    if (val.empty()) return def;
    return parseSizeValue(val);
}

uint32_t Css::getColor(const P::String& className, const P::String& property, uint32_t def) const {
    auto val = prop(className, property);
    if (val.empty()) return def;
    return parseColorValue(val);
}

// ============ Color/size parsing ============

uint32_t Css::parseColorValue(std::string_view color) {
    CssColor c;
    return cssColor(color, c) ? c.rgb : 0x000000;
}

int Css::parseSizeValue(std::string_view size) {
    const P::Array<std::string_view> parts = cssParts(size);
    int32_t px = 0;
    return !parts.empty() && cssLength(parts[0], kCssRemPx, px) ? px : 0;
}

void Css::dump() const {
    LOG_D(Log::UI, "=== CSS Rules (%d) ===", (int)m_rules.size());
    for (const auto& rule : m_rules) {
        LOG_D(Log::UI, "rule #%u: %d selectors {", (unsigned)rule.order, (int)rule.selectors.size());
        for (const CssDecl& d : rule.decls) {
            LOG_D(Log::UI, "  %s: %s%s;", d.name.c_str(), d.value.c_str(), d.important ? " !important" : "");
        }
        LOG_D(Log::UI, "}");
    }
}

} // namespace UI
