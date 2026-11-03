/**
 * dom.cpp — the elements of an app as its script sees them (see dom.h, docs/DOM.md).
 *
 * The tree is recorded while the pages are built. The walkers tell when an element's content
 * starts and ends (enter, leave) and when an element gets its object (element); the element is
 * known by where its attributes are in the text being built. That place is only looked at for the
 * element just entered, just left, or open: an object can come before its content (a box), inside
 * it (<p> and its text object) or after it (a <label> with HTML in it), and the same element can
 * be entered again (<tabs>, for each tab).
 *
 * CSS is applied again from the tree: its ancestors are opened as CSS scopes, outermost first,
 * with their children given for places among siblings. What the old rules set goes back first
 * (CssUndo).
 */
#include "ui/dom.h"

#include "core/core.h"
#include "ui/css_parser.h"
#include "ui/css_undo.h"
#include "ui/ui_html_flow.h"
#include "ui/ui_html_internal.h"
#include "utils/string_utils.h"
#include "widgets/widget_common.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace UI {
namespace Dom {
namespace {

// A handle: the build in the high bits, the element's index + 1 in the low ones
constexpr uint32_t kIndexBits = 20;
constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1;
constexpr uint32_t kBuildMask = (1u << (32 - kIndexBits)) - 1;

enum : uint8_t {
    kGone = 1,    // out of the page: text replaced its content, its object was deleted
    kStyled = 2,  // CSS was applied to its object
    kInline = 4,  // ...with its style=""
};

// An element. Links are indexes + 1 (0: none)
struct Entry {
    P::String tag;    // lower-case
    P::String attrs;  // the attribute text of its start tag, as written (and as scripts changed it)
    lv_obj_t* obj = nullptr;
    P::Ptr<CssUndo> undo;  // what its CSS changed, if anything
    uint32_t parent = 0, first = 0, last = 0, next = 0;
    uint8_t flags = 0;
};

struct Tree {
    P::Array<Entry> nodes;
    // While the pages are built: where each element's attributes are in the text being built
    // (that tells it apart), and the elements whose content is being built, outermost first
    P::Array<std::string_view> sources;
    P::Array<uint32_t> open;
    P::String asTag, asAttrs;  // Dom::As
    P::Array<std::pair<uint32_t, P::String>> styledAs;  // the tag CSS saw, where Dom::As changed it
};

// Never destroyed: the app is cleared from its destructor, which may run after statics are gone
Tree& tree() {
    static Tree* const t = P::create<Tree>().release();
    return *t;
}
P::Array<Entry>& nodes() { return tree().nodes; }
P::Array<uint32_t>& opened() { return tree().open; }

uint32_t s_build = 0;
bool s_building = false;
int s_paused = 0;

// The step before: an element got its object, or one was left
enum class Last : uint8_t { None, Element, Left };
Last s_last = Last::None;
uint32_t s_lastNode = 0;

bool s_asOn = false;  // Dom::As

constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
constexpr char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

P::String lower(std::string_view s) {
    P::String out(s);
    for (char& c : out) c = lowerAscii(c);
    return out;
}

Entry& node(uint32_t i) { return nodes()[i - 1]; }

Node handle(uint32_t i) { return i ? (s_build << kIndexBits) | i : 0; }

uint32_t indexOf(Node n) {
    if (!n || (n >> kIndexBits) != s_build) return 0;
    const uint32_t i = n & kIndexMask;
    return i <= nodes().size() ? i : 0;
}

bool recording() { return s_building && s_paused == 0; }

bool sameSource(uint32_t i, std::string_view attrs) {
    const P::Array<std::string_view>& src = tree().sources;
    if (!i || i > src.size()) return false;
    const std::string_view s = src[i - 1];
    return s.data() && s.data() == attrs.data() && s.size() == attrs.size();
}

bool isOpen(uint32_t i) { return std::find(opened().begin(), opened().end(), i) != opened().end(); }

uint32_t add(std::string_view tag, std::string_view attrs, lv_obj_t* obj) {
    if (nodes().size() >= kIndexMask) return 0;
    Entry e;
    e.tag = lower(tag);
    if (!attrs.empty()) e.attrs.assign(attrs.data(), attrs.size());
    e.obj = obj;
    e.parent = opened().empty() ? 0 : opened().back();
    nodes().push_back(std::move(e));
    tree().sources.push_back(attrs);
    const uint32_t i = static_cast<uint32_t>(nodes().size());
    if (const uint32_t p = node(i).parent) {
        Entry& pe = node(p);
        if (pe.last) node(pe.last).next = i;
        else pe.first = i;
        pe.last = i;
    }
    return i;
}

void dropTree(uint32_t i) {
    Entry& e = node(i);
    e.flags |= kGone;
    e.obj = nullptr;
    e.undo.reset();
    for (uint32_t c = e.first; c; c = node(c).next) dropTree(c);
}

// The elements in i are out of the page: their objects are deleted
void dropChildren(uint32_t i) {
    for (uint32_t c = node(i).first; c;) {
        const uint32_t next = node(c).next;
        node(c).parent = 0;
        node(c).next = 0;
        dropTree(c);
        c = next;
    }
    node(i).first = node(i).last = 0;
}

// ---- Attributes in the start tag's text ----

struct Attr {
    size_t start = 0, end = 0;  // where it is in the text, value and quotes included
    std::string_view name, value;
};

// The next attribute from i, read as UI::Css reads them: name, name=v, name="v", name='v'
bool nextAttr(std::string_view s, size_t& i, Attr& a) {
    const size_t n = s.size();
    while (i < n) {
        while (i < n && (isSpace(s[i]) || s[i] == '/')) ++i;
        if (i >= n) return false;
        a.start = i;
        while (i < n && !isSpace(s[i]) && s[i] != '=' && s[i] != '/' && s[i] != '>') ++i;
        a.name = s.substr(a.start, i - a.start);
        a.end = i;
        a.value = {};
        size_t j = i;
        while (j < n && isSpace(s[j])) ++j;
        if (j < n && s[j] == '=') {
            ++j;
            while (j < n && isSpace(s[j])) ++j;
            if (j < n && (s[j] == '"' || s[j] == '\'')) {
                const char q = s[j++];
                const size_t vs = j;
                while (j < n && s[j] != q) ++j;
                a.value = s.substr(vs, j - vs);
                if (j < n) ++j;
            } else {
                const size_t vs = j;
                while (j < n && !isSpace(s[j]) && s[j] != '>') ++j;
                a.value = s.substr(vs, j - vs);
            }
            i = a.end = j;
        }
        if (a.name.empty()) {  // a stray '=' or '>': step over it
            ++i;
            continue;
        }
        return true;
    }
    return false;
}

// The first one counts, as in HTML
bool findAttr(std::string_view attrs, std::string_view name, Attr& out) {
    size_t i = 0;
    Attr a;
    while (nextAttr(attrs, i, a)) {
        if (equalsNoCase(a.name, name)) {
            out = a;
            return true;
        }
    }
    return false;
}

P::String decoded(std::string_view v) {
    if (v.find('&') == std::string_view::npos) return P::String(v);
    return StringUtils::decodeEntities(P::String(v));
}

// name="value", the value escaped for the quotes
P::String written(std::string_view name, std::string_view value) {
    P::String out(name);
    out += "=\"";
    for (char c : value) {
        if (c == '&') out += "&amp;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    out += '"';
    return out;
}

// attrs with name set to *value in the place of the first one, or taken off (value null); a
// repeated one goes, as HTML drops it
P::String edited(std::string_view attrs, std::string_view name, const std::string_view* value) {
    P::String out;
    size_t copied = 0, i = 0;
    bool done = false;
    Attr a;
    while (nextAttr(attrs, i, a)) {
        if (!equalsNoCase(a.name, name)) continue;
        out.append(attrs.data() + copied, a.start - copied);
        if (value && !done) out += written(name, *value);
        copied = a.end;
        done = true;
    }
    out.append(attrs.data() + copied, attrs.size() - copied);
    if (value && !done) {
        out += ' ';
        out += written(name, *value);
    }
    return out;
}

// A name an attribute can have: no spaces, quotes, '=', '>' or '/', as in HTML
bool validName(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (isSpace(c) || c == '"' || c == '\'' || c == '=' || c == '>' || c == '/' || c == '<' ||
            static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

// ---- What the screen does with it ----

// The registered elements (ui.*, {var}) whose box is obj: a button's is the button, its text a label
template <typename F>
void forRegistered(lv_obj_t* obj, F f) {
    for (const auto& el : g_core.app().elements) {
        if (el && !el->is_page && (el->w.handle == obj || el->parentObj == obj)) f(*el);
    }
}

// A boolean attribute is on whatever its value, as in HTML, except false and 0, as in TelaOS
bool flagOn(std::string_view v) { return v != "false" && v != "0"; }

// Text in place of what obj shows, as textContent puts it in place of an element's content
bool showText(lv_obj_t* obj, const char* s) {
    if (HtmlFlow::setText(obj, s)) return true;  // spans, an HTML block
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_label_set_text(obj, s);
        return true;
    }
    if (!lv_obj_check_type(obj, &lv_obj_class)) return false;  // a field, a slider, a picture: no text in it
    contentReplaced(obj);
    g_core.app().forgetInside(obj);
    lv_obj_clean(obj);
    lv_label_set_text(lv_label_create(obj), s);
    return true;
}

bool isTelaAttr(std::string_view n) {
    return n == "bgcolor" || n == "color" || n == "visible" || n == "x" || n == "y" || n == "w" || n == "h" ||
           n == "z-index";
}

// What an attribute set (on) or taken off does to the element's object, as it does in the markup
void apply(const Entry& e, std::string_view name, std::string_view value, bool on) {
    lv_obj_t* const o = e.obj;
    if (!o) return;
    const bool set = on && flagOn(value);
    const P::String v = on && !value.empty() ? P::String(value) : P::String();
    if (name == "id") {
        if (!on) return;
        forRegistered(o, [&](UI::Element& el) { el.id = v; });  // ui.setAttr(id) finds it by the new id
        _unique_id(o, v.c_str());
    } else if (name == "hidden" || (name == "open" && e.tag == "dialog")) {
        const bool hide = name == "hidden" ? set : !set;  // a dialog shows while it is open
        if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else if (name == "disabled") {
        if (set) lv_obj_add_state(o, LV_STATE_DISABLED);
        else lv_obj_clear_state(o, LV_STATE_DISABLED);
    } else if (name == "checked") {
        HtmlFlow::setChecked(o, set);
    } else if (name == "value") {
        if (lv_obj_check_type(o, &lv_textarea_class)) lv_textarea_set_text(o, v.c_str());
        else if (on && lv_obj_check_type(o, &lv_slider_class)) lv_slider_set_value(o, atoi(v.c_str()), LV_ANIM_OFF);
    } else if (name == "placeholder") {
        if (lv_obj_check_type(o, &lv_textarea_class)) lv_textarea_set_placeholder_text(o, v.c_str());
    } else if (on && isTelaAttr(name)) {  // TelaOS attributes, as ui.setAttr() sets them
        bool done = false;
        forRegistered(o, [&](UI::Element& el) {
            if (!done) done = UI::setElementAttr(el, P::String(name).c_str(), v.c_str());
        });
        if (!done) {
            UI::Element bare;
            bare.w.handle = o;
            UI::setElementAttr(bare, P::String(name).c_str(), v.c_str());
        }
    }
}

// ---- CSS again ----

// The tag CSS saw for i: the builder's, where Dom::As kept the markup's for scripts
std::string_view cssTag(uint32_t i) {
    for (const auto& as : tree().styledAs) {
        if (as.first == i) return as.second;
    }
    return node(i).tag;
}

P::Array<CssChild> childrenOf(uint32_t i) {
    P::Array<CssChild> kids;
    for (uint32_t c = node(i).first; c; c = node(c).next) kids.push_back(CssChild{node(c).tag, node(c).attrs});
    return kids;
}

// f() with the CSS scopes of chain[k..] open, outermost first
template <typename F>
void inScopes(const P::Array<uint32_t>& chain, size_t k, F& f) {
    if (k == chain.size()) {
        f();
        return;
    }
    const uint32_t a = chain[k];
    const P::Array<CssChild> kids = childrenOf(a);
    CssScope scope(cssTag(a), node(a).attrs, kids);
    inScopes(chain, k + 1, f);
}

// CSS on i again: what the old rules set goes back, the rules that apply now are put on, and what
// something else set after CSS stays on top
void restyleOne(uint32_t i) {
    Entry& e = node(i);
    if (!(e.flags & kStyled) || (e.flags & kGone) || !e.obj) return;
    CssUndo kept;
    if (e.undo) e.undo->undo(e.obj, kept);
    CssElement el;
    el.tag = cssTag(i);
    el.attrs = e.attrs;
    Css::findAttr(e.attrs, "id", el.id);
    std::string_view cls;
    Css::findAttr(e.attrs, "class", cls);
    P::String rendered;
    if (cls.find('{') != std::string_view::npos) {  // class="{var}", as the builder rendered it
        rendered = render_template(P::String(cls).c_str());
        cls = rendered;
    }
    el.classes = cls;
    std::string_view inlineStyle;
    if (e.flags & kInline) Css::findAttr(e.attrs, "style", inlineStyle);
    CssUndo undo;
    Widget w{e.obj};
    Css::instance().applyMatching(w, el, inlineStyle, &undo);
    kept.restore(e.obj);
    if (undo.empty()) e.undo.reset();
    else e.undo = P::create<CssUndo>(std::move(undo));
    Attr hidden;  // hidden= still says what it says
    if (findAttr(e.attrs, "hidden", hidden) && flagOn(hidden.value)) lv_obj_add_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
}

void restyleTree(uint32_t i) {
    restyleOne(i);
    if (!node(i).first) return;
    const P::Array<CssChild> kids = childrenOf(i);
    CssScope scope(cssTag(i), node(i).attrs, kids);
    for (uint32_t c = node(i).first; c; c = node(c).next) restyleTree(c);
}

// i changed: what its rules may see changes for i, what is in it, the siblings after it
// (h2 + p, .a ~ li) and what is in them
void restyleFrom(uint32_t i) {
    P::Array<uint32_t> chain;
    for (uint32_t p = node(i).parent; p; p = node(p).parent) chain.push_back(p);
    std::reverse(chain.begin(), chain.end());
    auto all = [i] {
        for (uint32_t s = i; s; s = node(s).next) restyleTree(s);
    };
    inScopes(chain, 0, all);
}

// After attribute `name` of i changed
void attributeChanged(uint32_t i, std::string_view name) {
    if (name == "class" && node(i).obj) {  // class="{var}" is the script's now
        forRegistered(node(i).obj, [](UI::Element& el) { el.classTemplate.clear(); });
    }
    if ((name == "style" && (node(i).flags & kInline)) || Css::instance().looksAt(name)) restyleFrom(i);
}

// The words of class="": a class="{var}" as it is rendered now
P::Array<P::String> classWords(uint32_t i) {
    P::Array<P::String> words;
    Attr a;
    if (!findAttr(node(i).attrs, "class", a)) return words;
    P::String v = decoded(a.value);
    if (v.find('{') != P::String::npos) v = render_template(v.c_str());
    for (size_t k = 0; k < v.size();) {
        while (k < v.size() && isSpace(v[k])) ++k;
        const size_t start = k;
        while (k < v.size() && !isSpace(v[k])) ++k;
        if (k > start) {
            P::String w = v.substr(start, k - start);
            if (std::find(words.begin(), words.end(), w) == words.end()) words.push_back(std::move(w));
        }
    }
    return words;
}

bool validClass(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (isSpace(c)) return false;
    }
    return true;
}

// class="" set to the words, joined as a browser joins them, and CSS applied again
void setWords(Node n, const P::Array<P::String>& words) {
    P::String v;
    for (const P::String& w : words) {
        if (!v.empty()) v += ' ';
        v += w;
    }
    setAttribute(n, "class", v);
}

}  // namespace

// ============ Recording ============

void begin() {
    clear();
    s_building = true;
}

void end() {
    s_building = false;
    opened().clear();
    s_last = Last::None;
    tree().sources.clear();  // the text they pointed into is gone
    tree().sources.shrink_to_fit();
    nodes().shrink_to_fit();  // no more elements come: the room kept for them goes back
}

void clear() {
    nodes().clear();
    tree().sources.clear();
    tree().styledAs.clear();
    opened().clear();
    s_building = false;
    s_paused = 0;
    s_last = Last::None;
    s_lastNode = 0;
    s_asOn = false;
    s_build = (s_build + 1) & kBuildMask;
    if (!s_build) s_build = 1;
}

bool enter(std::string_view tag, std::string_view attrs) {
    if (!recording()) return false;
    uint32_t i = 0;
    // its object came just before its content (a box, then what is in it), or it is entered again
    if (s_last != Last::None && sameSource(s_lastNode, attrs) && !isOpen(s_lastNode)) i = s_lastNode;
    if (!i) i = add(tag, attrs, nullptr);
    opened().push_back(i);
    s_last = Last::None;
    return true;
}

void leave() {
    if (opened().empty()) return;
    s_lastNode = opened().back();
    opened().pop_back();
    s_last = s_lastNode ? Last::Left : Last::None;
}

bool element(lv_obj_t* obj, std::string_view tag, std::string_view attrs, bool styled, bool inlineStyle) {
    if (!recording() || !obj) return false;
    const uint32_t top = opened().empty() ? 0 : opened().back();
    uint32_t i = 0;
    if (top && !node(top).obj && sameSource(top, attrs)) {
        i = top;  // styled while its content is open: <p> and its text object
    } else if (s_last == Last::Left && !node(s_lastNode).obj && sameSource(s_lastNode, attrs)) {
        i = s_lastNode;  // styled after its content: a <label> with HTML in it
    }
    if (i) node(i).obj = obj;
    else if (!(i = add(tag, attrs, obj))) return false;
    if (styled) node(i).flags |= kStyled | (inlineStyle ? kInline : 0);
    if (s_asOn) {  // what the markup says, not what the builder was given
        if (styled) tree().styledAs.emplace_back(i, lower(tag));
        node(i).tag = lower(tree().asTag);
        node(i).attrs = std::move(tree().asAttrs);
        s_asOn = false;
    }
    s_last = Last::Element;
    s_lastNode = i;
    return true;
}

void keep(CssUndo&& undo) {
    if (!s_lastNode || s_last != Last::Element || undo.empty()) return;
    node(s_lastNode).undo = P::create<CssUndo>(std::move(undo));
}

void contentReplaced(lv_obj_t* obj) {
    if (!obj) return;
    for (uint32_t i = 1; i <= nodes().size(); ++i) {
        if (node(i).obj == obj) {
            dropChildren(i);
            return;
        }
    }
}

As::As(std::string_view tag, std::string_view attrs) {
    if (!recording()) return;
    s_asOn = true;
    tree().asTag = tag.empty() ? P::String() : P::String(tag);
    tree().asAttrs = attrs.empty() ? P::String() : P::String(attrs);
}

As::~As() { s_asOn = false; }

Pause::Pause() { ++s_paused; }
Pause::~Pause() { --s_paused; }

// ============ What a script does ============

Node getElementById(std::string_view id) {
    if (id.empty()) return 0;
    for (uint32_t i = 1; i <= nodes().size(); ++i) {
        const Entry& e = node(i);
        Attr a;
        if (!(e.flags & kGone) && findAttr(e.attrs, "id", a) && decoded(a.value) == id) return handle(i);
    }
    return 0;
}

P::String tagName(Node n) {
    const uint32_t i = indexOf(n);
    if (!i) return "";
    P::String t = node(i).tag;
    for (char& c : t) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return t;
}

P::String textContent(Node n) {
    const uint32_t i = indexOf(n);
    if (!i || !node(i).obj || HtmlFlow::isVoid(node(i).tag.c_str())) return "";  // <input>, <img>: no content
    return HtmlFlow::getText(node(i).obj);
}

bool setTextContent(Node n, std::string_view text) {
    const uint32_t i = indexOf(n);
    if (!i || !node(i).obj || HtmlFlow::isVoid(node(i).tag.c_str())) return false;
    lv_obj_t* const obj = node(i).obj;
    lv_obj_t* shown = obj;  // a button shows its text in a label of its own
    forRegistered(obj, [&](UI::Element& el) {
        if (el.parentObj == obj && el.obj()) shown = el.obj();
    });
    if (!showText(shown, P::String(text).c_str())) return false;
    dropChildren(i);
    forRegistered(obj, [](UI::Element& el) { el.tpl.clear(); });  // the {var} of the markup no longer write it
    return true;
}

bool getAttribute(Node n, std::string_view name, P::String& value) {
    const uint32_t i = indexOf(n);
    Attr a;
    if (!i || !findAttr(node(i).attrs, name, a)) return false;
    value = decoded(a.value);
    return true;
}

bool hasAttribute(Node n, std::string_view name) {
    const uint32_t i = indexOf(n);
    Attr a;
    return i && findAttr(node(i).attrs, name, a);
}

bool setAttribute(Node n, std::string_view name, std::string_view value) {
    if (!validName(name)) return false;
    const uint32_t i = indexOf(n);
    if (!i) return true;  // an element of an app gone: nothing to change
    const P::String key = lower(name);
    node(i).attrs = edited(node(i).attrs, key, &value);
    apply(node(i), key, value, true);
    attributeChanged(i, key);
    return true;
}

void removeAttribute(Node n, std::string_view name) {
    const uint32_t i = indexOf(n);
    if (!i || !hasAttribute(n, name)) return;
    const P::String key = lower(name);
    node(i).attrs = edited(node(i).attrs, key, nullptr);
    apply(node(i), key, {}, false);
    attributeChanged(i, key);
}

bool hasClass(Node n, std::string_view name) {
    const uint32_t i = indexOf(n);
    if (!i || !validClass(name)) return false;
    const P::Array<P::String> words = classWords(i);
    return std::find(words.begin(), words.end(), name) != words.end();
}

bool addClasses(Node n, const P::Array<std::string_view>& names) {
    for (std::string_view c : names) {
        if (!validClass(c)) return false;
    }
    const uint32_t i = indexOf(n);
    if (!i) return true;
    P::Array<P::String> words = classWords(i);
    for (std::string_view c : names) {
        if (std::find(words.begin(), words.end(), c) == words.end()) words.emplace_back(c);
    }
    setWords(n, words);
    return true;
}

bool removeClasses(Node n, const P::Array<std::string_view>& names) {
    for (std::string_view c : names) {
        if (!validClass(c)) return false;
    }
    const uint32_t i = indexOf(n);
    if (!i) return true;
    P::Array<P::String> words = classWords(i);
    words.erase(std::remove_if(words.begin(), words.end(),
                               [&](const P::String& w) {
                                   return std::find(names.begin(), names.end(), std::string_view(w)) != names.end();
                               }),
                words.end());
    setWords(n, words);
    return true;
}

bool toggleClass(Node n, std::string_view name, int force, bool& now) {
    now = false;
    if (!validClass(name)) return false;
    const uint32_t i = indexOf(n);
    if (!i) return true;
    P::Array<P::String> words = classWords(i);
    const auto at = std::find(words.begin(), words.end(), name);
    const bool has = at != words.end();
    now = force < 0 ? !has : force > 0;
    if (now == has) return true;  // as it is: nothing changes
    if (now) words.emplace_back(name);
    else words.erase(at);
    setWords(n, words);
    return true;
}

P::String className(Node n) {
    P::String v;
    getAttribute(n, "class", v);
    return v;
}

}  // namespace Dom
}  // namespace UI
