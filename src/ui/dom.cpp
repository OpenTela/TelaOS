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
 * (CssUndo). querySelector walks the tree in the page order with the scopes open the same way.
 *
 * Events: an element with listeners has LVGL callbacks on its object, and on those of the
 * elements in it for an event that goes up. The first runs before TelaOS's own callbacks
 * (LV_EVENT_PREPROCESS) and takes the event up the tree to the script; what TelaOS does next asks
 * defaultPrevented(), the last tells that the event is over on that object.
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
    kGone = 1,      // out of the page: text replaced its content, its object was deleted
    kStyled = 2,    // CSS was applied to its object, its style="" too
    kDetached = 4,  // out of the document, as createElement made it or removeChild took it out
};

// An element. Links are indexes + 1 (0: none)
struct Entry {
    P::String tag;    // lower-case
    P::String attrs;  // the attribute text of its start tag, as written (and as scripts changed it)
    lv_obj_t* obj = nullptr;
    P::Ptr<CssUndo> undo;  // what its CSS changed, if anything
    uint32_t parent = 0, first = 0, last = 0, next = 0;
    uint8_t flags = 0;
    uint8_t events = 0;   // the event types its object passes on to listeners (kClick...)
    uint8_t listens = 0;  // the event types it has listeners for: passed on again when it is built again
};

struct Tree {
    P::Array<Entry> nodes;
    // While the pages are built: where each element's attributes are in the text being built
    // (that tells it apart), and the elements whose content is being built, outermost first
    P::Array<std::string_view> sources;
    P::Array<uint32_t> open;
    P::String asTag, asAttrs;  // Dom::As
    P::Array<std::pair<uint32_t, P::String>> styledAs;  // the tag CSS saw, where Dom::As changed it
    P::Array<std::pair<uint32_t, P::String>> texts;     // the text of an element out of the document
    P::Array<uint32_t> known;  // while appendChild builds: the script's elements in the markup
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

// i into p's children, before `before` (0: the last)
void link(uint32_t p, uint32_t i, uint32_t before) {
    Entry& pe = node(p);
    node(i).parent = p;
    node(i).next = before;
    if (!before) {
        if (pe.last) node(pe.last).next = i;
        else pe.first = i;
        pe.last = i;
    } else if (pe.first == before) {
        pe.first = i;
    } else {
        for (uint32_t c = pe.first; c; c = node(c).next) {
            if (node(c).next == before) {
                node(c).next = i;
                break;
            }
        }
    }
}

void unlink(uint32_t i) {
    const uint32_t p = node(i).parent;
    if (!p) return;
    Entry& pe = node(p);
    uint32_t prev = 0;
    for (uint32_t c = pe.first; c && c != i; c = node(c).next) prev = c;
    if (prev) node(prev).next = node(i).next;
    else pe.first = node(i).next;
    if (pe.last == i) pe.last = prev;
    node(i).parent = node(i).next = 0;
}

// The markup appendChild builds marks each element of the script's with this attribute: the walkers
// may copy that text, the mark goes with it
constexpr std::string_view kMark = "data-tela-node";

// The script's element the attributes are of, in the markup appendChild builds; 0: none
uint32_t knownIn(std::string_view attrs) {
    if (tree().known.empty()) return 0;
    std::string_view v;
    if (!Css::findAttr(attrs, kMark, v) || v.empty()) return 0;
    uint32_t i = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return 0;
        i = i * 10 + static_cast<uint32_t>(c - '0');
    }
    const auto& k = tree().known;
    return std::find(k.begin(), k.end(), i) != k.end() ? i : 0;
}

uint32_t openTop() { return opened().empty() ? 0 : opened().back(); }

// marked: the attributes as the markup has them (Dom::As gives the builder others)
uint32_t add(std::string_view tag, std::string_view attrs, lv_obj_t* obj, std::string_view marked) {
    if (const uint32_t k = knownIn(marked)) {  // the script's element, built: it keeps its handle
        node(k).obj = obj;
        tree().sources[k - 1] = attrs;
        if (!node(k).parent && openTop()) link(openTop(), k, 0);
        return k;
    }
    if (nodes().size() >= kIndexMask) return 0;
    Entry e;
    e.tag = lower(tag);
    if (!attrs.empty()) e.attrs.assign(attrs.data(), attrs.size());
    e.obj = obj;
    nodes().push_back(std::move(e));
    tree().sources.push_back(attrs);
    const uint32_t i = static_cast<uint32_t>(nodes().size());
    if (openTop()) link(openTop(), i, 0);
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

// The tag CSS sees for i: the one CSS saw when the page was built (css), or the one scripts see
std::string_view tagFor(uint32_t i, bool css) { return css ? cssTag(i) : std::string_view(node(i).tag); }

P::Array<CssChild> childrenOf(uint32_t i) {
    P::Array<CssChild> kids;
    for (uint32_t c = node(i).first; c; c = node(c).next) kids.push_back(CssChild{node(c).tag, node(c).attrs});
    return kids;
}

P::Array<uint32_t> ancestorsOf(uint32_t i) {  // outermost first
    P::Array<uint32_t> chain;
    for (uint32_t p = node(i).parent; p; p = node(p).parent) chain.push_back(p);
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// f() with the CSS scopes of chain[k..] open, outermost first
template <typename F>
void inScopes(const P::Array<uint32_t>& chain, size_t k, bool css, F& f) {
    if (k == chain.size()) {
        f();
        return;
    }
    const uint32_t a = chain[k];
    const P::Array<CssChild> kids = childrenOf(a);
    CssScope scope(tagFor(a, css), node(a).attrs, kids);
    inScopes(chain, k + 1, css, f);
}

// i as a selector sees it. rendered: room for its class="{var}" as the builder rendered it
CssElement cssElement(uint32_t i, bool css, P::String& rendered) {
    const Entry& e = node(i);
    CssElement el;
    el.tag = tagFor(i, css);
    el.attrs = e.attrs;
    Css::findAttr(e.attrs, "id", el.id);
    Css::findAttr(e.attrs, "class", el.classes);
    if (el.classes.find('{') != std::string_view::npos) {
        rendered = render_template(P::String(el.classes).c_str());
        el.classes = rendered;
    }
    return el;
}

// CSS on i again: what the old rules set goes back, the rules that apply now are put on, and what
// something else set after CSS stays on top
void restyleOne(uint32_t i) {
    Entry& e = node(i);
    if (!(e.flags & kStyled) || (e.flags & kGone) || !e.obj) return;
    CssUndo kept;
    if (e.undo) e.undo->undo(e.obj, kept);
    P::String rendered;
    const CssElement el = cssElement(i, true, rendered);
    Attr style;  // style="", as a script may have changed it
    const P::String inlineStyle = findAttr(e.attrs, "style", style) ? decoded(style.value) : P::String();
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

// i changed: what its rules may see changes for i, what is in it and, with siblings, the siblings
// after it (h2 + p, .a ~ li) and what is in them
void restyleFrom(uint32_t i, bool siblings) {
    auto all = [i, siblings] {
        for (uint32_t s = i; s; s = siblings ? node(s).next : 0) restyleTree(s);
    };
    inScopes(ancestorsOf(i), 0, true, all);
}

// After attribute `name` of i changed
void attributeChanged(uint32_t i, std::string_view name) {
    if (name == "class" && node(i).obj) {  // class="{var}" is the script's now
        forRegistered(node(i).obj, [](UI::Element& el) { el.classTemplate.clear(); });
    }
    if (Css::instance().looksAt(name)) restyleFrom(i, true);
    else if (name == "style") restyleFrom(i, false);  // its own, and the --x what is in it inherits
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

// ---- style="" ----

// A property name as CSS writes one: background-color, --accent
bool validProperty(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '-' && c != '_' && u < 0x80) return false;
    }
    return true;
}

P::String propertyKey(std::string_view name) {  // a custom property keeps its case
    return name.size() > 2 && name[0] == '-' && name[1] == '-' ? P::String(name) : lower(name);
}

P::Array<CssDecl> styleOf(uint32_t i) {
    Attr a;
    return findAttr(node(i).attrs, "style", a) ? Css::declarations(decoded(a.value)) : P::Array<CssDecl>();
}

// As a browser writes them: "color: red; width: 10px !important;"
P::String styleText(const P::Array<CssDecl>& decls) {
    P::String out;
    for (const CssDecl& d : decls) {
        if (!out.empty()) out += ' ';
        out += d.name;
        out += ": ";
        out += d.value;
        if (d.important) out += " !important";
        out += ';';
    }
    return out;
}

// ---- Events ----

enum : uint8_t { kClick = 1, kInput = 2, kChange = 4, kFocus = 8, kBlur = 16 };

struct EventType {
    std::string_view name;
    uint8_t bit;
    bool bubbles;  // it goes up to the elements above the target, as in a browser
};

constexpr EventType kEventTypes[] = {
    {"click", kClick, true}, {"input", kInput, true}, {"change", kChange, true},
    {"focus", kFocus, false}, {"blur", kBlur, false},
};

const EventType* eventType(std::string_view name) {
    for (const EventType& t : kEventTypes) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

const EventType* eventType(uint8_t bit) {
    for (const EventType& t : kEventTypes) {
        if (t.bit == bit) return &t;
    }
    return nullptr;
}

// The LVGL event that is `bit` on obj. change is when the value is done, as in a browser: a
// slider let go, a text field's OK; a box or a list changes at once
lv_event_code_t codeOf(uint8_t bit, lv_obj_t* obj) {
    switch (bit) {
        case kClick: return LV_EVENT_CLICKED;
        case kInput: return LV_EVENT_VALUE_CHANGED;
        case kChange:
            if (lv_obj_check_type(obj, &lv_slider_class)) return LV_EVENT_RELEASED;
            if (lv_obj_check_type(obj, &lv_textarea_class)) return LV_EVENT_READY;
            return LV_EVENT_VALUE_CHANGED;
        case kFocus: return LV_EVENT_FOCUSED;
        default: return LV_EVENT_DEFOCUSED;
    }
}

Dispatch s_dispatch = nullptr;

// The LVGL event a listener prevented: what TelaOS does next on it asks defaultPrevented(), on the
// object it happened on and on those it goes up to (LV_OBJ_FLAG_EVENT_BUBBLE). The next event that
// comes to listeners forgets it
struct Prevented {
    lv_event_t* e = nullptr;
    lv_obj_t* target = nullptr;
    lv_event_code_t code{};
};
Prevented s_prevented;
lv_event_t* s_open = nullptr;  // the event the callbacks of an object are called for now

// The element obj is, or the nearest one above it: a button's text is the button
uint32_t elementOf(lv_obj_t* obj) {
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        for (uint32_t i = static_cast<uint32_t>(nodes().size()); i > 0; --i) {  // the innermost with it
            if (node(i).obj == o && !(node(i).flags & kGone)) return i;
        }
    }
    return 0;
}

// Before what TelaOS does on the object: the event goes to the listeners of its element and up
void onEvent(lv_event_t* e) {
    const auto bit = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const EventType* type = eventType(bit);
    lv_obj_t* const here = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* const target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const uint32_t t = elementOf(target);
    if (!type || !s_dispatch || !t || node(t).obj != here) return;  // once: where it happened
    if (s_open != e) {  // the first callback of this event: the one before is over
        s_open = e;
        s_prevented = {};
    }
    Event ev;
    ev.type = type->name;
    ev.target = handle(t);
    const uint32_t build = s_build;
    for (uint32_t p = t; p; p = type->bubbles ? node(p).parent : 0) {
        ev.currentTarget = handle(p);
        s_dispatch(ev);
        if (ev.stopped || s_build != build) break;  // stopPropagation(), or the app is gone
    }
    if (ev.defaultPrevented && s_build == build) s_prevented = {e, target, lv_event_get_code(e)};
}

// After TelaOS's own callbacks on the object: the next event is another one
void afterEvent(lv_event_t* e) {
    if (s_open == e) s_open = nullptr;
}

// The callbacks that pass `type` on, on i's object and, with inside, on those of the elements in
// i: an event that goes up comes to i from them
void passOn(uint32_t i, const EventType& type, bool inside) {
    Entry& e = node(i);
    if (e.obj && !(e.events & type.bit)) {
        e.events |= type.bit;
        const lv_event_code_t code = codeOf(type.bit, e.obj);
        void* const bit = reinterpret_cast<void*>(static_cast<uintptr_t>(type.bit));
        lv_obj_add_event_cb(e.obj, onEvent, static_cast<lv_event_code_t>(code | LV_EVENT_PREPROCESS), bit);
        lv_obj_add_event_cb(e.obj, afterEvent, code, bit);
    }
    if (!inside) return;
    for (uint32_t c = e.first; c; c = node(c).next) passOn(c, type, true);
}

// ---- Selectors ----

// The states a selector names (:active, :focus, :checked, :disabled) that obj is in now
uint16_t statesOf(lv_obj_t* obj) {
    uint16_t states = 0;
    if (!obj) return states;
    for (const uint16_t s : {LV_STATE_PRESSED, LV_STATE_FOCUSED, LV_STATE_CHECKED, LV_STATE_DISABLED}) {
        if (lv_obj_has_state(obj, s)) states = static_cast<uint16_t>(states | s);
    }
    return states;
}

bool matches(uint32_t i, const CssQuery& q) {
    P::String rendered;
    return q.matches(cssElement(i, false, rendered), statesOf(node(i).obj));
}

// What q matches in i, in the page order, until `limit` are found; the scopes above i are open
void collect(uint32_t i, const CssQuery& q, size_t limit, P::Array<Node>& found) {
    if (!node(i).first || found.size() >= limit) return;
    const P::Array<CssChild> kids = childrenOf(i);
    CssScope scope(node(i).tag, node(i).attrs, kids);
    for (uint32_t c = node(i).first; c && found.size() < limit; c = node(c).next) {
        if (matches(c, q)) found.push_back(handle(c));
        collect(c, q, limit, found);
    }
}

bool query(Node inside, std::string_view selectors, size_t limit, P::Array<Node>& found) {
    const CssQuery q(selectors);
    if (!q.ok()) return false;
    if (inside) {
        const uint32_t i = indexOf(inside);
        auto in = [&] { collect(i, q, limit, found); };
        if (i) inScopes(ancestorsOf(i), 0, false, in);  // its ancestors count: .menu a
        return true;
    }
    for (uint32_t i = 1; i <= nodes().size() && found.size() < limit; ++i) {
        if (node(i).parent || (node(i).flags & (kGone | kDetached))) continue;  // the pages, then what is in them
        if (matches(i, q)) found.push_back(handle(i));
        collect(i, q, limit, found);
    }
    return true;
}

// ---- The tree: elements into the document and out of it ----

void setDetached(uint32_t i, bool on) {
    Entry& e = node(i);
    e.flags = static_cast<uint8_t>(on ? e.flags | kDetached : e.flags & ~kDetached);
    for (uint32_t c = e.first; c; c = node(c).next) setDetached(c, on);
}

// The text an element that was never built shows when it is: its textContent
const P::String* keptText(uint32_t i) {
    for (const auto& t : tree().texts) {
        if (t.first == i) return &t.second;
    }
    return nullptr;
}

void keepText(uint32_t i, std::string_view text) {
    auto& texts = tree().texts;
    texts.erase(std::remove_if(texts.begin(), texts.end(), [i](const auto& t) { return t.first == i; }), texts.end());
    if (!text.empty()) texts.emplace_back(i, P::String(text));
}

// A text the walkers would take {var} in: it is put in after the build, as textContent puts it
bool bindable(const P::String& text) { return text.find('{') != P::String::npos; }

// i and what is in it as markup the page walkers build from, each element marked (kMark); what
// has objects already is left out: it is moved in after. own: where i's attributes start
void markupOf(uint32_t i, P::String& out, size_t* own = nullptr) {
    const Entry& e = node(i);
    out += '<';
    out += e.tag;
    if (own) *own = out.size();
    out += ' ';
    out += kMark;
    out += "=\"";
    out += std::to_string(i).c_str();
    out += '"';
    if (!e.attrs.empty() && !isSpace(e.attrs[0])) out += ' ';
    out += e.attrs;
    out += '>';
    if (HtmlFlow::isVoid(e.tag.c_str())) return;
    if (const P::String* t = keptText(i)) {
        if (bindable(*t) && !e.first) {
            out += '-';  // the text object to put it in
        } else {
            for (char c : *t) {
                if (c == '&') out += "&amp;";
                else if (c == '<') out += "&lt;";
                else if (c == '>') out += "&gt;";
                else out += c;
            }
        }
    }
    for (uint32_t c = e.first; c; c = node(c).next) {
        if (!node(c).obj) markupOf(c, out);
    }
    out += "</";
    out += e.tag;
    out += '>';
}

void markKnown(uint32_t i) {
    tree().known.push_back(i);
    for (uint32_t c = node(i).first; c; c = node(c).next) {
        if (!node(c).obj) markKnown(c);
    }
}

// The texts with {var} in them, put into what was built, as text
void textsAfter(uint32_t i) {
    const P::String* t = keptText(i);
    if (t && bindable(*t) && !node(i).first && node(i).obj) {
        const P::String text = *t;
        keepText(i, {});
        setTextContent(handle(i), text);
    }
    for (uint32_t c = node(i).first; c; c = node(c).next) textsAfter(c);
}

// Where the objects of an element out of the document wait, hidden: they come back as they were
lv_obj_t* s_limbo = nullptr;

void onLimboDelete(lv_event_t*) { s_limbo = nullptr; }

lv_obj_t* limbo() {
    if (!s_limbo) {
        s_limbo = lv_obj_create(lv_screen_active());
        lv_obj_add_flag(s_limbo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(s_limbo, onLimboDelete, LV_EVENT_DELETE, nullptr);
    }
    return s_limbo;
}

// i's objects and those of the elements in it go (or went)
void forgetObjects(uint32_t i) {
    Entry& e = node(i);
    e.obj = nullptr;
    e.undo.reset();
    e.events = 0;
    e.flags = static_cast<uint8_t>(e.flags & ~kStyled);
    for (uint32_t c = e.first; c; c = node(c).next) forgetObjects(c);
}

// The LVGL parent the objects of p's elements go into: where those of its other elements are; or
// for an HTML element its flow box, for a TelaOS one its box
lv_obj_t* hostIn(uint32_t p, uint32_t but, bool html) {
    for (uint32_t c = node(p).first; c; c = node(c).next) {
        if (c != but && node(c).obj && !(node(c).flags & kDetached)) return lv_obj_get_parent(node(c).obj);
    }
    lv_obj_t* const box = HtmlFlow::contentOf(node(p).obj);
    return html ? HtmlFlow::flowBox(box) : box;
}

bool isHtml(std::string_view tag) {
    const P::String t = "<" + P::String(tag) + ">";
    return HtmlFlow::hasHtml(t.c_str(), t.size());
}

// c's object where it goes among the objects of its parent's elements: before the next one's
void place(uint32_t c) {
    lv_obj_t* const o = node(c).obj;
    for (uint32_t n = node(c).next; n; n = node(n).next) {
        lv_obj_t* const next = node(n).obj;
        if (next && lv_obj_get_parent(next) == lv_obj_get_parent(o)) {
            int32_t to = lv_obj_get_index(next);
            if (lv_obj_get_index(o) < to) --to;  // it leaves its place first
            lv_obj_move_to_index(o, to);
            return;
        }
    }
    lv_obj_move_to_index(o, -1);  // the last
}

bool isElementObj(lv_obj_t* obj) {
    for (uint32_t i = 1; i <= nodes().size(); ++i) {
        if (node(i).obj == obj) return true;
    }
    return false;
}

// While it lives, what the walkers build is recorded in the tree, in p: the script's elements
// (tree().known) keep their handles
class Fragment {
public:
    explicit Fragment(uint32_t p) {
        s_building = true;
        opened().assign(1, p);
        s_last = Last::None;
        tree().sources.assign(nodes().size(), std::string_view());
    }
    ~Fragment() {
        s_building = false;
        opened().clear();
        s_last = Last::None;
        tree().sources.clear();
        tree().sources.shrink_to_fit();
        tree().known.clear();
    }
    Fragment(const Fragment&) = delete;
    Fragment& operator=(const Fragment&) = delete;
};

// c, a child of p never built, built where it is: in p's box, the CSS scopes of p's ancestors and
// p open, with p's children for places among siblings
void build(uint32_t p, uint32_t c) {
    P::String markup;
    size_t at = 0;
    markupOf(c, markup, &at);
    const std::string_view own(markup.data() + at, markup.find('>', at) - at);  // as the walkers see it
    P::Array<CssChild> kids;
    for (uint32_t k = node(p).first; k; k = node(k).next) {
        kids.push_back(CssChild{node(k).tag, k == c ? own : std::string_view(node(k).attrs)});
    }
    lv_obj_t* const box = HtmlFlow::contentOf(node(p).obj);
    nodes().reserve(nodes().size() + 16);  // kids' views stay where they point while it builds
    markKnown(c);
    auto walk = [&] {
        CssScope scope(node(p).tag, node(p).attrs, kids);
        Fragment fragment(p);
        parse_children(markup.c_str(), static_cast<int>(markup.size()), box);
    };
    inScopes(ancestorsOf(p), 0, false, walk);
    if (!node(c).obj && isHtml(node(c).tag)) {  // <span> in a block: a line of its own, its object
        lv_obj_t* const line = lv_obj_get_child(HtmlFlow::flowBox(box), -1);
        if (line && !isElementObj(line)) node(c).obj = line;
    }
    textsAfter(c);
}

// The elements in c that have objects of their own already, into c's box: elements a script took
// out, then put into an element it made
void moveIn(uint32_t c) {
    for (uint32_t k = node(c).first; k; k = node(k).next) {
        if (node(k).obj && lv_obj_get_parent(node(k).obj) == s_limbo) {
            lv_obj_set_parent(node(k).obj, hostIn(c, k, isHtml(node(k).tag)));
            place(k);
        } else if (!node(k).obj) {
            continue;
        }
        moveIn(k);
    }
}

// The listeners of the elements above c, for events that come up to them, and those of c and what
// is in it: their objects pass them on
void listenAgain(uint32_t c) {
    for (uint32_t a = node(c).parent; a; a = node(a).parent) {
        for (const EventType& t : kEventTypes) {
            if ((node(a).listens & t.bit) && t.bubbles) passOn(c, t, true);
        }
    }
    auto own = [](uint32_t i, auto& self) -> void {
        for (const EventType& t : kEventTypes) {
            if (!(node(i).listens & t.bit)) continue;
            if (t.bit == kClick && node(i).obj) lv_obj_add_flag(node(i).obj, LV_OBJ_FLAG_CLICKABLE);
            passOn(i, t, t.bubbles);
        }
        for (uint32_t k = node(i).first; k; k = node(k).next) self(k, self);
    };
    own(c, own);
}

// CSS on p's children again, and on what is in them: places among siblings (li:last-child) moved
void restyleChildren(uint32_t p) {
    if (!node(p).first) return;
    auto all = [p] {
        const P::Array<CssChild> kids = childrenOf(p);
        CssScope scope(cssTag(p), node(p).attrs, kids);
        for (uint32_t c = node(p).first; c; c = node(c).next) restyleTree(c);
    };
    inScopes(ancestorsOf(p), 0, true, all);
}

bool inDocument(uint32_t i) { return !(node(i).flags & (kDetached | kGone)); }

// i is p or above it: p cannot go into i
bool isAncestor(uint32_t i, uint32_t p) {
    for (uint32_t a = p; a; a = node(a).parent) {
        if (a == i) return true;
    }
    return false;
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
    tree().texts.clear();
    tree().known.clear();
    s_limbo = nullptr;  // it went with the app's objects
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
    if (!i) i = add(tag, attrs, nullptr, attrs);
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

bool element(lv_obj_t* obj, std::string_view tag, std::string_view attrs, bool styled) {
    if (!recording() || !obj) return false;
    const uint32_t top = opened().empty() ? 0 : opened().back();
    uint32_t i = 0;
    if (top && !node(top).obj && sameSource(top, attrs)) {
        i = top;  // styled while its content is open: <p> and its text object
    } else if (s_last == Last::Left && !node(s_lastNode).obj && sameSource(s_lastNode, attrs)) {
        i = s_lastNode;  // styled after its content: a <label> with HTML in it
    }
    if (i) node(i).obj = obj;
    else if (!(i = add(tag, attrs, obj, s_asOn ? std::string_view(tree().asAttrs) : attrs))) return false;
    if (styled) node(i).flags |= kStyled;
    if (s_asOn) {  // what the markup says, not what the builder was given
        if (styled) tree().styledAs.emplace_back(i, lower(tag));
        if (!knownIn(tree().asAttrs)) {  // the script's element keeps its own
            node(i).tag = lower(tree().asTag);
            node(i).attrs = std::move(tree().asAttrs);
        }
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
        if (!(e.flags & (kGone | kDetached)) && findAttr(e.attrs, "id", a) && decoded(a.value) == id) return handle(i);
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
    if (!i || HtmlFlow::isVoid(node(i).tag.c_str())) return "";  // <input>, <img>: no content
    if (!node(i).obj) {  // made by createElement: the text it is to show, then what is in it
        if (!(node(i).flags & kDetached)) return "";
        const P::String* t = keptText(i);
        P::String all = t ? *t : P::String();
        for (uint32_t c = node(i).first; c; c = node(c).next) all += textContent(handle(c));
        return all;
    }
    return HtmlFlow::getText(node(i).obj);
}

bool setTextContent(Node n, std::string_view text) {
    const uint32_t i = indexOf(n);
    if (!i || HtmlFlow::isVoid(node(i).tag.c_str())) return false;
    if (!node(i).obj && (node(i).flags & kDetached)) {  // never built: it shows the text when it is
        dropChildren(i);
        keepText(i, text);
        return true;
    }
    if (!node(i).obj) return false;
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

bool styleProperty(Node n, std::string_view name, P::String& value, bool& important) {
    const uint32_t i = indexOf(n);
    if (!i) return false;
    const P::String key = propertyKey(name);
    const P::Array<CssDecl> decls = styleOf(i);
    for (auto d = decls.rbegin(); d != decls.rend(); ++d) {  // the last one counts, as in CSS
        if (d->name != key) continue;
        value = d->value;
        important = d->important;
        return true;
    }
    return false;
}

void setStyleProperty(Node n, std::string_view name, std::string_view value, bool important) {
    const uint32_t i = indexOf(n);
    if (!i || !validProperty(name)) return;
    const P::String key = propertyKey(name);
    P::Array<CssDecl> one;
    if (!value.empty()) {
        P::String text = key + ": " + P::String(value) + (important ? " !important" : "");
        one = Css::declarations(text);
        // one declaration of this property, as a browser takes it: "red; width: 0" is not one
        if (one.size() != 1 || one[0].name != key || one[0].important != important) return;
    }
    P::Array<CssDecl> decls = styleOf(i);
    P::Array<CssDecl> out;
    bool had = false;
    for (CssDecl& d : decls) {
        if (d.name != key) {
            out.push_back(std::move(d));
        } else if (!had) {  // in the place of the first one
            had = true;
            if (!one.empty()) out.push_back(one[0]);
        }
    }
    if (!had && one.empty()) return;  // nothing to take off
    if (!had) out.push_back(one[0]);
    setAttribute(n, "style", styleText(out));
}

P::String cssText(Node n) {
    const uint32_t i = indexOf(n);
    return i ? styleText(styleOf(i)) : P::String();
}

void setCssText(Node n, std::string_view text) { setAttribute(n, "style", styleText(Css::declarations(text))); }

Node createElement(std::string_view tag) {
    if (!validName(tag) || nodes().size() >= kIndexMask) return 0;
    Entry e;
    e.tag = lower(tag);
    e.flags = kDetached;
    nodes().push_back(std::move(e));
    return handle(static_cast<uint32_t>(nodes().size()));
}

TreeChange insertBefore(Node parent, Node child, Node ref) {
    const uint32_t p = indexOf(parent), c = indexOf(child), r = ref ? indexOf(ref) : 0;
    if (!p || !c || (ref && !r)) return TreeChange::Done;  // an element of an app gone: nothing to change
    if (isAncestor(c, p)) return TreeChange::Loop;
    if (!node(c).parent && inDocument(c)) return TreeChange::Page;
    if (r && node(r).parent != p) return TreeChange::NotAChild;
    if (r == c) return TreeChange::Done;  // before itself: where it is
    const bool built = inDocument(p);
    if (built && !lv_obj_check_type(HtmlFlow::contentOf(node(p).obj), &lv_obj_class)) return TreeChange::NotABox;
    const uint32_t from = node(c).parent;
    unlink(c);
    link(p, c, r);
    if (!built) {  // into an element out of the document: its objects wait with it
        if (node(c).obj && inDocument(c)) lv_obj_set_parent(node(c).obj, limbo());
        setDetached(c, true);
    } else if (node(c).obj) {  // it was in the document, or taken out: its objects, as they are
        lv_obj_set_parent(node(c).obj, hostIn(p, c, isHtml(node(c).tag)));
        setDetached(c, false);
        place(c);
        moveIn(c);
    } else {  // made by createElement: built now
        setDetached(c, false);
        build(p, c);
        if (node(c).obj) place(c);
        moveIn(c);
    }
    if (built) {
        listenAgain(c);
        restyleChildren(p);  // c's place among them, and what CSS in this place gives it
    }
    if (from && from != p && inDocument(from)) restyleChildren(from);
    return TreeChange::Done;
}

TreeChange removeChild(Node parent, Node child) {
    const uint32_t p = indexOf(parent), c = indexOf(child);
    if (!p || !c) return TreeChange::Done;
    if (node(c).parent != p) return TreeChange::NotAChild;
    const bool built = inDocument(c);
    unlink(c);
    if (built && node(c).obj) lv_obj_set_parent(node(c).obj, limbo());  // hidden, as it is: it may come back
    setDetached(c, true);
    if (built) restyleChildren(p);
    return TreeChange::Done;
}

Node parentOf(Node n) {
    const uint32_t i = indexOf(n);
    return i ? handle(node(i).parent) : 0;
}

namespace {
// A page: in the document and inside no element
bool isPage(uint32_t i) { return !node(i).parent && !(node(i).flags & (kGone | kDetached)); }

Node pageFrom(uint32_t i) {
    for (; i <= nodes().size(); ++i) {
        if (isPage(i)) return handle(i);
    }
    return 0;
}
}  // namespace

Node firstChild(Node n) {
    if (!n) return pageFrom(1);
    const uint32_t i = indexOf(n);
    return i ? handle(node(i).first) : 0;
}

Node nextSibling(Node n) {
    const uint32_t i = indexOf(n);
    if (!i) return 0;
    if (node(i).parent) return handle(node(i).next);
    return isPage(i) ? pageFrom(i + 1) : 0;  // out of the document: alone
}

lv_obj_t* objectOf(Node n) {
    const uint32_t i = indexOf(n);
    return i ? node(i).obj : nullptr;
}

void release(Node n) {
    const uint32_t i = indexOf(n);
    if (!i || node(i).parent || !(node(i).flags & kDetached) || !node(i).obj) return;
    lv_obj_t* const o = node(i).obj;
    forgetObjects(i);
    g_core.app().forgetInside(o);
    forRegistered(o, [](UI::Element& el) {
        el.w.handle = nullptr;
        el.parentObj = nullptr;
    });
    lv_obj_delete_async(o);  // the collector may run anywhere, an LVGL event included
}

void setDispatch(Dispatch d) { s_dispatch = d; }

bool listen(Node n, std::string_view type) {
    const EventType* t = eventType(type);
    if (!t) return false;
    const uint32_t i = indexOf(n);
    if (!i) return true;  // an element of an app gone: nothing comes to it
    node(i).listens |= t->bit;  // built again (appendChild), it passes them on again
    if (t->bit == kClick && node(i).obj) {
        lv_obj_add_flag(node(i).obj, LV_OBJ_FLAG_CLICKABLE);  // any element takes a tap
    }
    passOn(i, *t, t->bubbles);
    return true;
}

bool defaultPrevented(lv_event_t* e) {
    return e && s_prevented.e == e && s_prevented.code == lv_event_get_code(e) &&
           s_prevented.target == static_cast<lv_obj_t*>(lv_event_get_target(e));
}

bool querySelector(Node inside, std::string_view selectors, Node& found) {
    P::Array<Node> one;
    const bool ok = query(inside, selectors, 1, one);
    found = one.empty() ? 0 : one[0];
    return ok;
}

bool querySelectorAll(Node inside, std::string_view selectors, P::Array<Node>& found) {
    found.clear();
    return query(inside, selectors, SIZE_MAX, found);
}

}  // namespace Dom
}  // namespace UI
