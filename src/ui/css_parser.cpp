/**
 * css_parser.cpp — see css_parser.h.
 *
 * A sheet is parsed once into rules (selectors + declarations); each element built asks for its
 * cascade: the rules whose selectors match it and its ancestors (CssScope), sorted as CSS sorts
 * them, one winning value per property. Inline HTML attributes (bgcolor=, color=...) are applied
 * by the widget builders after this, so they still win over CSS.
 */

#include "ui/css_parser.h"
#include "ui/css_colors.h"
#include "ui/ui_coords.h"
#include "ui/ui_html_internal.h"
#include "core/core.h"

#include "widgets/widget_common.h"
#include "utils/log_config.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

static const char* TAG = "CSS";

namespace UI {

using CssDetail::AttrTest;
using CssDetail::Compound;
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

// One compound selector at i: a type or *, then #id, .class and [attr] in any order
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
        } else {
            break;  // a combinator, the end, or something not supported (':' is checked by the caller)
        }
    }
    return i > start;
}

// One complex selector (no commas): compounds joined by ' ' (descendant) and '>' (child).
// false for anything not supported: :pseudo-classes, ::pseudo-elements, '+' and '~'.
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
        if (s[i] == '>') {
            comb = '>';
            ++i;
            skip();
        } else if (!space) {
            return false;  // ':', '+', '~', or a stray character
        }
        if (i >= n) return false;  // nothing after the combinator
        out.combinators += comb;
    }
    unsigned ids = 0, classes = 0, types = 0;
    for (const Compound& c : out.parts) {
        ids += !c.id.empty();
        classes += static_cast<unsigned>(c.classes.size());
        for (const AttrTest& t : c.attrs) {
            if (t.name == "id" && t.op == '=') ++ids;  // #a#b
            else ++classes;
        }
        types += !c.tag.empty();
    }
    out.specificity = (std::min(ids, 255u) << 16) | (std::min(classes, 255u) << 8) | std::min(types, 255u);
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

bool matchCompound(const Compound& c, const CssElement& el) {
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
    return true;
}

// parts[0..part] match the ancestors anc[0..top]: parts[part] is the nearest one to the right
bool matchAncestors(const Selector& s, int part, const P::Array<CssElement>& anc, int top) {
    if (part < 0) return true;
    if (s.combinators[static_cast<size_t>(part)] == '>') {  // the parent itself
        return top >= 0 && matchCompound(s.parts[static_cast<size_t>(part)], anc[static_cast<size_t>(top)]) &&
               matchAncestors(s, part - 1, anc, top - 1);
    }
    for (int k = top; k >= 0; --k) {  // any ancestor; a farther one may be the one that fits the rest
        if (matchCompound(s.parts[static_cast<size_t>(part)], anc[static_cast<size_t>(k)]) &&
            matchAncestors(s, part - 1, anc, k - 1)) {
            return true;
        }
    }
    return false;
}

// top: the nearest ancestor's index in anc
bool matchSelector(const Selector& s, const CssElement& el, const P::Array<CssElement>& anc, int top) {
    const int last = static_cast<int>(s.parts.size()) - 1;
    if (last < 0 || !matchCompound(s.parts[static_cast<size_t>(last)], el)) return false;
    return matchAncestors(s, last - 1, anc, top);
}

// The same element: the same tag and the same attribute text (views into the same page)
bool sameElement(const CssElement& a, const CssElement& b) {
    return a.tag == b.tag && a.attrs.data() == b.attrs.data() && a.attrs.size() == b.attrs.size();
}

// ============ Scopes ============

P::Array<CssElement>& scopeStack() {
    static P::Array<CssElement> s_stack;
    return s_stack;
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

CssScope::CssScope(std::string_view tag, std::string_view attrs) {
    CssElement el;
    el.tag = tag;
    el.attrs = attrs;
    Css::findAttr(attrs, "id", el.id);
    Css::findAttr(attrs, "class", el.classes);
    scopeStack().push_back(el);
}

CssScope::~CssScope() { scopeStack().pop_back(); }

const P::Array<CssElement>& CssScope::ancestors() { return scopeStack(); }

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
        if (!selector.empty() && selector[0] == '@') continue;  // @media, @keyframes...: not yet

        const size_t blockLen = braceCount == 0 ? pos - propsStart - 1 : pos - propsStart;
        P::Array<CssDecl> decls = parseDecls(css.substr(propsStart, blockLen));
        if (decls.empty() || selector.empty()) continue;

        Rule rule;
        // h1, h2: a list; a selector this engine cannot read is left out, the others stay
        size_t s = 0;
        const std::string_view sel(selector);
        while (s <= sel.size()) {
            size_t comma = sel.find(',', s);
            if (comma == std::string_view::npos) comma = sel.size();
            Selector one;
            if (parseSelector(sel.substr(s, comma - s), one)) rule.selectors.push_back(std::move(one));
            else LOG_D(Log::UI, "CSS: selector '%.*s' not supported, left out", (int)(comma - s), sel.data() + s);
            s = comma + 1;
        }
        if (rule.selectors.empty()) continue;
        rule.decls = std::move(decls);
        rule.order = m_nextOrder++;

        LOG_D(Log::UI, "CSS '%s' { %d props }", selector.c_str(), (int)rule.decls.size());
        m_rules.push_back(std::move(rule));
    }
}

// ============ Cascade ============

P::Array<CssDecl> Css::cascade(const CssElement& el, std::string_view inlineStyle) const {
    struct Hit {
        const CssDecl* decl;
        uint8_t layer;  // 0 sheets, 1 style="", 2 sheets !important, 3 style="" !important
        uint32_t specificity;
        uint32_t order;  // rule order, then the declaration's place in its rule
    };
    P::Array<Hit> hits;
    const P::Array<CssElement>& anc = CssScope::ancestors();
    // A paragraph's text is built inside its own scope (its widgets need it as their ancestor):
    // when it styles itself, it is not its own ancestor
    int top = static_cast<int>(anc.size()) - 1;
    if (top >= 0 && el.attrs.data() && sameElement(anc[static_cast<size_t>(top)], el)) --top;
    for (const Rule& r : m_rules) {
        bool any = false;
        uint32_t spec = 0;  // a list applies with its most specific matching selector
        for (const Selector& s : r.selectors) {
            if (matchSelector(s, el, anc, top)) {
                any = true;
                spec = std::max(spec, s.specificity);
            }
        }
        if (!any) continue;
        for (size_t k = 0; k < r.decls.size(); ++k) {
            const CssDecl& d = r.decls[k];
            hits.push_back(Hit{&d, static_cast<uint8_t>(d.important ? 2 : 0), spec,
                               (r.order << 8) | static_cast<uint32_t>(std::min<size_t>(k, 255))});
        }
    }
    const P::Array<CssDecl> inlineDecls = parseDecls(inlineStyle);
    for (size_t k = 0; k < inlineDecls.size(); ++k) {
        const CssDecl& d = inlineDecls[k];
        hits.push_back(Hit{&d, static_cast<uint8_t>(d.important ? 3 : 1), 0, static_cast<uint32_t>(k)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.specificity != b.specificity) return a.specificity < b.specificity;
        return a.order < b.order;
    });

    // One value per property: the last in cascade order; the result keeps that order
    P::Array<const CssDecl*> winners;
    for (const Hit& h : hits) {
        for (auto& w : winners) {
            if (w && w->name == h.decl->name) w = nullptr;
        }
        winners.push_back(h.decl);
    }
    P::Array<CssDecl> out;
    for (const CssDecl* w : winners) {
        if (w) out.push_back(*w);
    }
    return out;
}

void Css::applyMatching(Widget& w, const CssElement& el, std::string_view inlineStyle) const {
    if (!w.handle) return;
    if (m_rules.empty() && inlineStyle.empty()) return;
    applyProps(w, cascade(el, inlineStyle));
}

void Css::applyMatching(Widget& w, const char* tag, const char* id, const char* classNames) const {
    CssElement el;
    el.tag = tag ? tag : "";
    el.id = id ? id : "";
    el.classes = classNames ? classNames : "";
    applyMatching(w, el);
}

// ============ Apply properties to widget ============

void Css::applyProps(Widget& w, const P::Array<CssDecl>& decls) const {
    // Deferred text-valign — needs height to be set first
    P::String deferredTextValign;
    P::String deferredTextAlignV;  // vertical part of compound "center center"

    for (const CssDecl& d : decls) {
        const P::String& name = d.name;
        const P::String& value = d.value;
        if (name == "color") {
            w.applyColor(parseColorValue(value));
        } else if (name == "background" || name == "background-color" || name == "bgcolor") {
            w.applyBgColor(parseColorValue(value));
        } else if (name == "font-size" || name == "font") {
            w.applyFont(parseSizeValue(value));
        } else if (name == "border-radius" || name == "radius") {
            w.applyRadius(parseSizeValue(value));
        } else if (name == "z-index") {
            if (!w.handle) continue;
            g_core.app().setZIndex(w.handle, atoi(value.c_str()));
        } else if (name == "width" || name == "w") {
            int v = parse_size(value.c_str()); if (v != 0) lv_obj_set_width(w.handle, v);
        } else if (name == "height" || name == "h") {
            int v = parse_size(value.c_str()); if (v != 0) lv_obj_set_height(w.handle, v);
        } else if (name == "min-height") {
            int v = parse_size(value.c_str()); if (v != 0) lv_obj_set_style_min_height(w.handle, v, 0);
        } else if (name == "max-height") {
            int v = parse_size(value.c_str()); if (v != 0) lv_obj_set_style_max_height(w.handle, v, 0);
        } else if (name == "min-width") {
            int v = parse_size(value.c_str()); if (v != 0) lv_obj_set_style_min_width(w.handle, v, 0);
        } else if (name == "max-width") {
            int v = parse_size(value.c_str()); if (v != 0) lv_obj_set_style_max_width(w.handle, v, 0);
        } else if (name == "left") {
            int v = parse_coord_w(value.c_str()); if (v >= 0) lv_obj_set_x(w.handle, v);
        } else if (name == "top") {
            int v = parse_coord_h(value.c_str()); if (v >= 0) lv_obj_set_y(w.handle, v);
        } else if (name == "padding") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_all(w.handle, v, 0);
        } else if (name == "padding-left") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_left(w.handle, v, 0);
        } else if (name == "padding-right") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_right(w.handle, v, 0);
        } else if (name == "padding-top") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_top(w.handle, v, 0);
        } else if (name == "padding-bottom") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_bottom(w.handle, v, 0);
        } else if (name == "gap" || name == "border-spacing") {
            int v = parseSizeValue(value);
            if (v >= 0) {
                lv_obj_set_style_pad_row(w.handle, v, 0);
                lv_obj_set_style_pad_column(w.handle, v, 0);
            }
        } else if (name == "opacity") {
            int opa = (int)(std::atof(value.c_str()) * 255);
            if (opa < 0) opa = 0; if (opa > 255) opa = 255;
            lv_obj_set_style_opa(w.handle, opa, 0);
        } else if (name == "display") {
            if (equalsNoCase(value, "none")) lv_obj_add_flag(w.handle, LV_OBJ_FLAG_HIDDEN);
        } else if (name == "text-align") {
            P::String val(value.data(), value.size());
            auto space = val.find(' ');
            P::String hPart = (space != P::String::npos) ? val.substr(0, space) : val;
            if (hPart == "center")     lv_obj_set_style_text_align(w.handle, LV_TEXT_ALIGN_CENTER, 0);
            else if (hPart == "right") lv_obj_set_style_text_align(w.handle, LV_TEXT_ALIGN_RIGHT, 0);
            else                       lv_obj_set_style_text_align(w.handle, LV_TEXT_ALIGN_LEFT, 0);
            if (space != P::String::npos)
                deferredTextAlignV = val.substr(space + 1);
        } else if (name == "text-valign") {
            deferredTextValign.assign(value.data(), value.size());
        }
    }

    // Apply text-valign AFTER height/font are set
    if (!deferredTextValign.empty())
        applyTextValignCSS(w.handle, deferredTextValign);
    else if (!deferredTextAlignV.empty())
        applyTextValignCSS(w.handle, deferredTextAlignV);
}

// ============ Legacy class-only API ============

const Rule* Css::findClass(const P::String& className) const {
    for (const auto& rule : m_rules) {
        for (const Selector& s : rule.selectors) {
            if (s.parts.size() == 1 && s.parts[0].tag.empty() && s.parts[0].id.empty() &&
                s.parts[0].attrs.empty() && s.parts[0].classes.size() == 1 && s.parts[0].classes[0] == className) {
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
    if (color.empty()) return 0x000000;
    while (!color.empty() && std::isspace(static_cast<unsigned char>(color.front()))) color = color.substr(1);
    while (!color.empty() && std::isspace(static_cast<unsigned char>(color.back()))) color = color.substr(0, color.size() - 1);
    if (!color.empty() && color[0] == '#') color = color.substr(1);

    uint32_t named;
    if (cssNamedColor(color, named)) return named;  // all 148 CSS names (css_colors.h)
    if (color == "transparent") return 0x000000;

    char buf[8];
    if (color.size() == 6) {
        memcpy(buf, color.data(), 6); buf[6] = '\0';
        return static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
    }
    if (color.size() == 3) {
        buf[1] = '\0';
        buf[0] = color[0]; uint32_t r = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        buf[0] = color[1]; uint32_t g = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        buf[0] = color[2]; uint32_t b = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        return (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    }
    return 0x000000;
}

int Css::parseSizeValue(std::string_view size) {
    if (size.empty()) return 0;
    char buf[16];
    size_t i = 0;
    for (char c : size) {
        if (i >= 15) break;
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') buf[i++] = c;
        else if (c != ' ') break;
    }
    buf[i] = '\0';
    return i == 0 ? 0 : std::atoi(buf);
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
