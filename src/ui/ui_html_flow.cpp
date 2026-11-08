/**
 * ui_html_flow.cpp — standard HTML elements for TelaOS (see ui_html_flow.h, docs/HTML_TAGS.md).
 *
 * Flow model:
 *   block elements  → column boxes, children stacked, gap 8 (HTML margins, simplified)
 *   inline content  → one text object per run of inline content:
 *                       plain text           → lv_label (wraps; {var} bindings stay live)
 *                       formatted text       → lv_spangroup (LV_USE_SPAN), live via updateFn
 *                       2+ links / widgets   → row-wrap of word labels (each link tappable)
 *   TelaOS tags met inside HTML content are built by parse_children() as before.
 *
 * Visual language = widget_markdown.h's colours; headings the sizes of a browser (h1 2em … h6
 * 0.67em), bold; b, strong → bold, i, em → italic, code → green. Fonts: UI::Font, twelve sizes in
 * four faces (utils/font.h).
 */
#include "lvgl.h"
#include "ui/ui_html_flow.h"

#include "core/core.h"
#include "ui/css_box.h"
#include "ui/css_media.h"
#include "ui/css_parser.h"
#include "ui/css_values.h"
#include "ui/dom.h"
#include "ui/ui_coords.h"
#include "ui/ui_html_internal.h"
#include "ui/ui_tag_reader.h"
#include "ui/xml_utils.h"
#include "utils/font.h"
#include "utils/log_config.h"
#include "widgets/widget_common.h"

#include <LittleFS.h>
#include <lvgl.h>
#if LV_USE_SPAN
#include "src/widgets/span/lv_span_private.h"  // a span's text, for textContent
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using namespace UI::XmlUtils;

static const char* TAG = "html_flow";

namespace HtmlFlow {
namespace {

// ============ Visual language (palette of widget_markdown.h) ============

constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kH2 = 0xDDDDDD;
constexpr uint32_t kAccent = 0x4FC3F7;   // links, dt
constexpr uint32_t kDim = 0xAAAAAA;      // small, sub, sup, markers, captions
constexpr uint32_t kCode = 0x81C784;     // code, kbd, samp
constexpr uint32_t kMark = 0xFFD54F;     // mark, meter next to its optimum
constexpr uint32_t kBad = 0xE57373;      // meter far from its optimum
constexpr uint32_t kRule = 0x555555;     // hr, borders, bar track
constexpr uint32_t kPanel = 0x222222;    // dialog, placeholders
constexpr uint32_t kInherit = 0xFF000000;

constexpr int kBlockIndent = 40;  // a browser's: blockquote, figure on both sides; dd on the side lines start
constexpr int kListPad = 40;   // a list's padding on the side its lines start: a browser's 40px
constexpr int kMarkerGap = 5;  // a marker and its item: the space after "1." of a browser's marker
constexpr int kBodyPad = 8;    // HTML body margin

constexpr uint8_t kUnderline = 1;
constexpr uint8_t kStrike = 2;

// Style::weight: inherited, or the face a run asks for (font-weight 1–500, 600–1000)
constexpr uint8_t kWeightNormal = 1;
constexpr uint8_t kWeightBold = 2;

// Style::slant: inherited, or upright (font-style: normal) or italic (italic, oblique)
constexpr uint8_t kSlantNormal = 1;
constexpr uint8_t kSlantItalic = 2;

// ============ Tag table ============

enum class Kind : uint8_t { Inline, Block, Text, List, Item, Hidden, Fallback, Special };

enum Look : uint8_t {
    L_PLAIN, L_STRONG, L_EM, L_CODE, L_LINK, L_MARK, L_DIM, L_UNDER, L_STRIKE, L_QUOTE, L_BIG, L_FONT,
    L_RT, L_SKIP, L_SUB, L_SUP
};

struct TagInfo {
    const char* name;
    Kind kind;
    uint8_t look;
};

const TagInfo kTags[] = {
    // Inline text semantics (+ obsolete aliases browsers still render)
    {"a", Kind::Inline, L_LINK},        {"abbr", Kind::Inline, L_PLAIN},   {"acronym", Kind::Inline, L_PLAIN},
    {"b", Kind::Inline, L_STRONG},      {"bdi", Kind::Inline, L_PLAIN},    {"bdo", Kind::Inline, L_PLAIN},
    {"big", Kind::Inline, L_BIG},       {"cite", Kind::Inline, L_EM},      {"code", Kind::Inline, L_CODE},
    {"data", Kind::Inline, L_PLAIN},    {"del", Kind::Inline, L_STRIKE},   {"dfn", Kind::Inline, L_EM},
    {"em", Kind::Inline, L_EM},         {"font", Kind::Inline, L_FONT},    {"i", Kind::Inline, L_EM},
    {"ins", Kind::Inline, L_UNDER},     {"kbd", Kind::Inline, L_CODE},     {"mark", Kind::Inline, L_MARK},
    {"nobr", Kind::Inline, L_PLAIN},    {"output", Kind::Inline, L_PLAIN}, {"q", Kind::Inline, L_QUOTE},
    {"rb", Kind::Inline, L_PLAIN},      {"rp", Kind::Inline, L_SKIP},      {"rt", Kind::Inline, L_RT},
    {"rtc", Kind::Inline, L_RT},        {"ruby", Kind::Inline, L_PLAIN},   {"s", Kind::Inline, L_STRIKE},
    {"samp", Kind::Inline, L_CODE},     {"small", Kind::Inline, L_DIM},    {"span", Kind::Inline, L_PLAIN},
    {"strike", Kind::Inline, L_STRIKE}, {"strong", Kind::Inline, L_STRONG}, {"sub", Kind::Inline, L_SUB},
    {"sup", Kind::Inline, L_SUP},       {"time", Kind::Inline, L_PLAIN},   {"tt", Kind::Inline, L_CODE},
    {"u", Kind::Inline, L_UNDER},       {"var", Kind::Inline, L_EM},
    // Shadow DOM placeholders and dropped elements: in a page, browsers just show their content
    {"content", Kind::Inline, L_PLAIN}, {"menuitem", Kind::Inline, L_PLAIN}, {"shadow", Kind::Inline, L_PLAIN},
    {"slot", Kind::Inline, L_PLAIN},
    // Blocks
    {"address", Kind::Block, 0},   {"article", Kind::Block, 0}, {"aside", Kind::Block, 0},
    {"blockquote", Kind::Block, 0}, {"body", Kind::Block, 0},   {"center", Kind::Block, 0},
    {"dd", Kind::Block, 0},        {"dl", Kind::Block, 0},      {"fieldset", Kind::Block, 0},
    {"figure", Kind::Block, 0},    {"footer", Kind::Block, 0},  {"form", Kind::Block, 0},
    {"header", Kind::Block, 0},    {"hgroup", Kind::Block, 0},  {"main", Kind::Block, 0},
    {"nav", Kind::Block, 0},       {"search", Kind::Block, 0},  {"section", Kind::Block, 0},
    // Text blocks
    {"caption", Kind::Text, 0}, {"dt", Kind::Text, 0}, {"figcaption", Kind::Text, 0}, {"h1", Kind::Text, 0},
    {"h2", Kind::Text, 0},      {"h3", Kind::Text, 0}, {"h4", Kind::Text, 0},         {"h5", Kind::Text, 0},
    {"h6", Kind::Text, 0},      {"legend", Kind::Text, 0}, {"p", Kind::Text, 0},      {"summary", Kind::Text, 0},
    // Lists
    {"dir", Kind::List, 0}, {"menu", Kind::List, 0}, {"ol", Kind::List, 0}, {"ul", Kind::List, 0},
    {"li", Kind::Item, 0},
    // Special handling
    {"br", Kind::Special, 0},        {"details", Kind::Special, 0}, {"dialog", Kind::Special, 0},
    {"hr", Kind::Special, 0},        {"html", Kind::Special, 0},    {"img", Kind::Special, 0},
    {"input", Kind::Special, 0},     {"listing", Kind::Special, 0}, {"marquee", Kind::Special, 0},
    {"meter", Kind::Special, 0},     {"picture", Kind::Special, 0}, {"plaintext", Kind::Special, 0},
    {"pre", Kind::Special, 0},       {"progress", Kind::Special, 0}, {"style", Kind::Special, 0},
    {"tbody", Kind::Special, 0},     {"textarea", Kind::Special, 0}, {"tfoot", Kind::Special, 0},
    {"th", Kind::Special, 0},        {"thead", Kind::Special, 0},   {"wbr", Kind::Special, 0},
    {"xmp", Kind::Special, 0},
    // Not rendered, as in a browser
    {"area", Kind::Hidden, 0},     {"base", Kind::Hidden, 0},     {"col", Kind::Hidden, 0},
    {"colgroup", Kind::Hidden, 0}, {"datalist", Kind::Hidden, 0}, {"frame", Kind::Hidden, 0},
    {"frameset", Kind::Hidden, 0}, {"head", Kind::Hidden, 0},     {"link", Kind::Hidden, 0},
    {"map", Kind::Hidden, 0},      {"meta", Kind::Hidden, 0},     {"noembed", Kind::Hidden, 0},
    {"noframes", Kind::Hidden, 0}, {"noscript", Kind::Hidden, 0}, {"optgroup", Kind::Hidden, 0},
    {"option", Kind::Hidden, 0},   {"param", Kind::Hidden, 0},    {"script", Kind::Hidden, 0},
    {"source", Kind::Hidden, 0},   {"template", Kind::Hidden, 0}, {"title", Kind::Hidden, 0},
    {"track", Kind::Hidden, 0},
    // Embedded content the watch cannot play: fallback content, else a placeholder box
    {"applet", Kind::Fallback, 0}, {"audio", Kind::Fallback, 0},  {"embed", Kind::Fallback, 0},
    {"fencedframe", Kind::Fallback, 0}, {"iframe", Kind::Fallback, 0}, {"math", Kind::Fallback, 0},
    {"object", Kind::Fallback, 0}, {"portal", Kind::Fallback, 0}, {"svg", Kind::Fallback, 0},
    {"video", Kind::Fallback, 0},
};

const TagInfo* info(std::string_view name) {
    for (const auto& t : kTags) {
        if (name == t.name) return &t;
    }
    return nullptr;
}

// Whether a tag name is b. Out of line, as the strcmp it replaces: a few hundred places ask, and
// the flash of the watch is short.
[[gnu::noinline]] bool is(std::string_view a, std::string_view b) { return a == b; }
[[gnu::noinline]] bool is(const char* a, const char* b) { return std::string_view(a) == std::string_view(b); }

// Whether n is one of names
[[gnu::noinline]] bool oneOf(std::string_view n, const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (n == names[i]) return true;
    }
    return false;
}
template <size_t N>
bool oneOf(std::string_view n, const char* const (&names)[N]) {
    return oneOf(n, names, N);
}

// ASCII letters in any case are the same: tag and attribute names
[[gnu::noinline]] bool sameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

bool isTelaTag(std::string_view n) {
    static constexpr const char* kTela[] = {"label", "button", "switch", "slider", "input", "canvas",
                                                 "image", "markdown", "qr", "tabs", "tab", "select", "list",
                                                 "item", "div", "table", "tr", "td", "page", "group"};
    return oneOf(n, kTela);
}

bool isVoidName(std::string_view n) {
    static constexpr const char* kVoid[] = {"area", "base", "br", "col", "embed", "hr", "img", "input",
                                                 "link", "meta", "param", "source", "track", "wbr", "frame"};
    return oneOf(n, kVoid);
}

// Content is raw text: no tags inside
bool isRawText(std::string_view n) {
    static constexpr const char* kRaw[] = {"script", "style", "textarea", "title", "xmp", "plaintext"};
    return oneOf(n, kRaw);
}

// Inline-level replaced elements and form controls: sit inside a line of text
bool isAtomName(std::string_view n) {
    static constexpr const char* kAtom[] = {"img", "input", "button", "select", "textarea", "label",
                                                 "progress", "meter", "image", "switch", "slider", "canvas"};
    return oneOf(n, kAtom);
}

// ============ Attributes ============

// Boolean attributes (`open`, `checked`, `hidden`) have no value: getAttr() cannot tell
// "absent" from "empty", so scan the attribute names.
bool hasAttr(const char* s, const char* e, std::string_view name) {
    const char* p = s;
    while (p < e) {
        while (p < e && (isspace(static_cast<unsigned char>(*p)) || *p == '/')) ++p;
        const char* a = p;
        while (p < e && *p != '=' && !isspace(static_cast<unsigned char>(*p)) && *p != '/') ++p;
        if (sameNoCase(std::string_view(a, static_cast<size_t>(p - a)), name)) return true;
        while (p < e && isspace(static_cast<unsigned char>(*p))) ++p;
        if (p < e && *p == '=') {
            ++p;
            while (p < e && isspace(static_cast<unsigned char>(*p))) ++p;
            if (p < e && (*p == '"' || *p == '\'')) {
                const char q = *p++;
                while (p < e && *p != q) ++p;
                if (p < e) ++p;
            } else {
                while (p < e && !isspace(static_cast<unsigned char>(*p))) ++p;
            }
        }
    }
    return false;
}

double attrNum(const char* s, const char* e, const char* name, double def) {
    P::String v = getAttr(s, e, name);
    if (v.empty()) return def;
    char* end = nullptr;
    const double d = strtod(v.c_str(), &end);
    return end == v.c_str() ? def : d;
}

P::String lower(const P::String& s) {
    P::String r = s;
    for (auto& c : r) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return r;
}

// ============ Element reference ============

// A tag name, lower-case, as the walkers read it from the page: at most 23 characters, a longer one
// is cut there. Compares and passes as a string_view.
class TagName {
public:
    TagName() = default;
    explicit TagName(std::string_view s)
        : len_(static_cast<uint8_t>(std::min(s.size(), kMax))) {
        std::copy_n(s.data(), len_, chars_.begin());
    }
    TagName(const char* s) {  // NOLINT(google-explicit-constructor)
        while (len_ < kMax && s[len_]) {
            chars_[len_] = s[len_];
            ++len_;
        }
    }

    // c at the end; false if the name is full
    bool push(char c) {
        if (len_ >= kMax) return false;
        chars_[len_++] = c;
        chars_[len_] = '\0';
        return true;
    }
    bool empty() const { return len_ == 0; }
    char operator[](size_t i) const { return i <= len_ ? chars_[i] : '\0'; }
    const char* c_str() const { return chars_.data(); }
    operator std::string_view() const { return {chars_.data(), len_}; }  // NOLINT(google-explicit-constructor)

private:
    static constexpr size_t kMax = 23;
    std::array<char, kMax + 1> chars_{};
    uint8_t len_ = 0;
};

// Whether a tag name is b: two pointers, as the strcmp it replaces
[[gnu::noinline]] bool is(const TagName& a, const char* b) { return std::string_view(a) == std::string_view(b); }

struct TagRef {
    TagName name;
    const char* src = nullptr;     // '<' (null when called from parse_children)
    const char* astart = nullptr;  // attributes
    const char* aend = nullptr;    // at '>'
    const char* cstart = nullptr;  // content
    const char* cend = nullptr;
    const char* next = nullptr;    // after the element
};

// "</name", any case
const char* findRawClose(const char* p, const char* end, std::string_view name) {
    const size_t n = name.size() + 2;
    for (; p + n <= end; ++p) {
        if (p[0] == '<' && p[1] == '/' && sameNoCase(std::string_view(p + 2, name.size()), name)) return p;
    }
    return nullptr;
}

// Name of the start tag at p ('<'), lower-case; false if p is not a start tag
bool peekName(const char* p, const char* end, TagName& name) {
    if (p + 1 >= end || *p != '<' || !isalpha(static_cast<unsigned char>(p[1]))) return false;
    name = TagName();
    for (const char* q = p + 1; q < end; ++q) {
        if (isspace(static_cast<unsigned char>(*q)) || *q == '>' || *q == '/') break;
        if (!name.push(static_cast<char>(tolower(static_cast<unsigned char>(*q))))) break;
    }
    return !name.empty();
}

// A comment starts at p: "<!--"
bool commentAt(const char* p, const char* end) { return end - p >= 4 && std::string_view(p, 4) == "<!--"; }

// p at "<!--": after the first "-->" that starts before end (it may run past end, as the page goes
// on: the text is 0-terminated, and 0 is no '-' or '>'); end without one
const char* skipComment(const char* p, const char* end) {
    for (const char* c = p + 4; c < end; ++c) {
        if (c[0] == '-' && c[1] == '-' && c[2] == '>') return c + 3;
    }
    return end;
}

// ---- Omitted end tags: <li>one<li>two, <p>one<p>two, <dt>term<dd>definition ----

bool closesP(std::string_view n) {  // a start tag that ends an open <p>
    static constexpr const char* kClose[] = {
        "address", "article", "aside", "blockquote", "details", "dialog", "div", "dl", "fieldset", "figcaption",
        "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu",
        "nav", "ol", "p", "pre", "search", "section", "table", "ul"};
    return oneOf(n, kClose);
}

bool optionalEnd(std::string_view n) {
    static constexpr const char* kOpt[] = {"li", "dt", "dd", "p", "rt", "rp", "option", "optgroup",
                                                "thead", "tbody", "tfoot", "tr", "td", "th"};
    return oneOf(n, kOpt);
}

// Does start tag `next` end the open element `open` (same level)?
bool impliedEnd(const char* open, const char* next) {
    auto any = [next](std::initializer_list<const char*> names) {
        for (const char* n : names) {
            if (is(n, next)) return true;
        }
        return false;
    };
    if (is(open, "li")) return any({"li"});
    if (is(open, "dt") || is(open, "dd")) return any({"dt", "dd"});
    if (is(open, "rt") || is(open, "rp")) return any({"rt", "rp"});
    if (is(open, "option")) return any({"option", "optgroup"});
    if (is(open, "optgroup")) return any({"optgroup"});
    if (is(open, "thead") || is(open, "tbody")) return any({"tbody", "tfoot"});
    if (is(open, "tr")) return any({"tr", "thead", "tbody", "tfoot"});
    if (is(open, "td") || is(open, "th")) return any({"td", "th", "tr", "thead", "tbody", "tfoot"});
    if (is(open, "p")) return closesP(next);
    return false;
}

bool readTag(const char* p, const char* end, TagRef& t);

// Where an element without its end tag ends: at a sibling that implies it, or at the parent's end
const char* impliedClose(const char* p, const char* end, const char* name) {
    while (p < end) {
        p = static_cast<const char*>(memchr(p, '<', end - p));
        if (!p) return end;
        if (commentAt(p, end)) {
            p = skipComment(p, end);
            continue;
        }
        if (p + 1 < end && p[1] == '/') return p;  // the parent's end tag
        TagName next;
        TagRef c;
        if (!peekName(p, end, next)) {
            ++p;
            continue;
        }
        if (impliedEnd(name, next.c_str())) return p;
        if (!readTag(p, end, c)) {
            ++p;
            continue;
        }
        p = c.next;
    }
    return end;
}

bool readTag(const char* p, const char* end, TagRef& t) {
    t = TagRef{};
    t.src = p;
    const char* q = p + 1;
    while (q < end && !isspace(static_cast<unsigned char>(*q)) && *q != '>' && *q != '/' &&
           t.name.push(static_cast<char>(tolower(static_cast<unsigned char>(*q))))) {
        ++q;
    }
    if (t.name.empty() || !isalpha(static_cast<unsigned char>(t.name[0]))) return false;
    t.astart = q;
    char quote = 0;
    for (; q < end; ++q) {
        if (quote) {
            if (*q == quote) quote = 0;
        } else if (*q == '"' || *q == '\'') {
            quote = *q;
        } else if (*q == '>') {
            break;
        }
    }
    if (q >= end) return false;
    t.aend = q;
    const bool selfClose = q > t.astart && *(q - 1) == '/';
    t.cstart = t.cend = t.next = q + 1;
    if (selfClose || isVoidName(t.name)) return true;
    const char* close = isRawText(t.name) ? findRawClose(q + 1, end, t.name) : findTagClose(q + 1, t.name.c_str());
    if (optionalEnd(t.name)) {
        // <li>, <p>, <dd> may omit the end tag: then the next sibling that implies it ends them
        // (<li>a<li>b, <p>a<div>), not a later </li> of another list
        const char* implied = impliedClose(q + 1, end, t.name.c_str());
        if (!close || close > end || implied < close) {
            t.cend = t.next = implied;
            return true;
        }
    } else if (!close || close > end) {
        return true;  // no end tag: closes right here
    }
    t.cend = close;
    const char* gt = static_cast<const char*>(memchr(close, '>', end - close));
    t.next = gt ? gt + 1 : end;
    return true;
}

TagRef refFrom(const char* tag, const char* astart, const char* aend, const char* content) {
    TagRef t;
    t.name = tag;
    t.astart = astart;
    t.aend = aend;
    t.cstart = content ? content : "";
    t.cend = t.cstart + strlen(t.cstart);
    t.next = t.cend;
    return t;
}

P::String attr(const TagRef& t, const char* name) { return getAttr(t.astart, t.aend, name); }
bool has(const TagRef& t, const char* name) { return hasAttr(t.astart, t.aend, name); }

// Raw attribute text of a start tag, for CSS. It points into the page even when empty: the
// place tells one element from another (UI::CssScope)
std::string_view rawAttrs(const TagRef& t) {
    if (!t.astart) return {};
    return std::string_view(t.astart, t.aend > t.astart ? static_cast<size_t>(t.aend - t.astart) : 0);
}

// While its content is built, t is an ancestor for CSS selectors (nav a, ul > li)
struct Inside : UI::CssScope {
    explicit Inside(const TagRef& t)
        : UI::CssScope(t.name, rawAttrs(t),
                       t.cstart ? std::string_view(t.cstart, t.cend > t.cstart ? static_cast<size_t>(t.cend - t.cstart) : 0)
                                : std::string_view()) {}
};

// ============ Entities, whitespace ============

void appendUtf8(P::String& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Entity {
    const char* name;
    uint32_t cp;
};
const Entity kEntities[] = {
    {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},     {"apos", '\''},
    {"nbsp", ' '},     {"copy", 0xA9},    {"reg", 0xAE},     {"trade", 0x2122}, {"deg", 0xB0},
    {"plusmn", 0xB1},  {"times", 0xD7},   {"divide", 0xF7},  {"middot", 0xB7},  {"sect", 0xA7},
    {"para", 0xB6},    {"laquo", 0xAB},   {"raquo", 0xBB},   {"ndash", 0x2013}, {"mdash", 0x2014},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
    {"bdquo", 0x201E}, {"bull", 0x2022},  {"hellip", 0x2026}, {"prime", 0x2032}, {"euro", 0x20AC},
    {"pound", 0xA3},   {"yen", 0xA5},     {"cent", 0xA2},    {"lsaquo", 0x2039}, {"rsaquo", 0x203A},
};

// p at '&': decode one entity into out, return the position after it
const char* decodeEntity(const char* p, const char* end, P::String& out) {
    const char* semi = static_cast<const char*>(memchr(p, ';', end - p));
    if (!semi || semi - p > 10) {
        out += '&';
        return p + 1;
    }
    if (p[1] == '#') {
        const bool hex = (p[2] == 'x' || p[2] == 'X');
        const uint32_t cp = static_cast<uint32_t>(strtoul(p + (hex ? 3 : 2), nullptr, hex ? 16 : 10));
        if (cp) appendUtf8(out, cp == 0xA0 ? ' ' : cp);
        return semi + 1;
    }
    const std::string_view name(p + 1, static_cast<size_t>(semi - p - 1));
    for (const auto& en : kEntities) {
        if (name == en.name) {
            appendUtf8(out, en.cp);
            return semi + 1;
        }
    }
    out.append(p, semi + 1 - p);  // unknown entity: keep it
    return semi + 1;
}

// ============ Look and styles ============

struct Style {  // inline run style
    uint32_t color = kInherit;
    uint8_t opa = LV_OPA_COVER;  // the color's alpha: rgba(), #RRGGBBAA
    int font = 0;
    uint8_t weight = 0;  // 0: as the text around, kWeightNormal, kWeightBold
    uint8_t slant = 0;   // 0: as the text around, kSlantNormal, kSlantItalic
    uint8_t decor = 0;
    int8_t shift = 0;  // how far the baseline moves, px: sup up (< 0), sub down (> 0)
    P::String href;
    P::String onclick;
    bool same(const Style& o) const {
        return color == o.color && opa == o.opa && font == o.font && weight == o.weight && slant == o.slant &&
               decor == o.decor && shift == o.shift && href == o.href && onclick == o.onclick;
    }
    bool plain() const {
        return color == kInherit && opa == LV_OPA_COVER && !font && !weight && !slant && !decor && !shift &&
               href.empty() && onclick.empty();
    }
    bool link() const { return !href.empty() || !onclick.empty(); }
};

struct TextLook {  // block text look
    int font = 0;
    bool bold = false;
    uint32_t color = kInherit;
    int align = -1;
};

// h1 … h6 of a browser's style sheet: their font-size in em of the text around them
constexpr float kHeadingEm[] = {2.0f, 1.5f, 1.17f, 1.0f, 0.83f, 0.67f};

// around: the size of the text the element is in, px
TextLook lookFor(std::string_view n, int around) {
    TextLook l;
    // Headings are bold and as large as in a browser. Down to a whole pixel first: h3 is 18.72px,
    // drawn at 18; h5, 13.28, at 14; h6, 10.72, at 12, the smallest there is
    if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6') {
        l.font = static_cast<int>(static_cast<float>(around) * kHeadingEm[n[1] - '1']);
        l.color = n[1] == '1' ? kText : kH2;
        l.bold = true;
    } else if (is(n, "dt")) l.color = kAccent;
    else if (is(n, "caption")) l.color = kDim, l.align = LV_TEXT_ALIGN_CENTER;
    else if (is(n, "figcaption") || is(n, "legend")) l.color = kDim;
    return l;
}

void applyLook(lv_obj_t* o, const TextLook& l) {
    if (l.font) lv_obj_set_style_text_font(o, Font::get(l.font, l.bold), LV_PART_MAIN);
    if (l.color != kInherit) lv_obj_set_style_text_color(o, lv_color_hex(l.color), LV_PART_MAIN);
    if (l.align >= 0) lv_obj_set_style_text_align(o, static_cast<lv_text_align_t>(l.align), LV_PART_MAIN);
}

// The font of a run: its own size, weight and slant, or those of the text around it (around: the
// font its object inherits); nullptr when it has none and simply takes that font
const lv_font_t* runFont(const Style& st, const lv_font_t* around) {
    if (!st.font && !st.weight && !st.slant) return nullptr;
    const int size = st.font ? st.font : UI::Font::sizeOf(around);
    const bool bold = st.weight ? st.weight == kWeightBold : UI::Font::isBold(around);
    const bool italic = st.slant ? st.slant == kSlantItalic : UI::Font::isItalic(around);
    return Font::get(size, bold, italic);
}

// <font size="1..7">, "+1", "-1" (of 3): the sizes a browser gives them, x-small to xxx-large
int fontForHtmlSize(const P::String& size) {
    if (size.empty()) return 0;
    static constexpr int kPx[] = {10, 13, 16, 18, 24, 32, 48};
    int v = atoi(size.c_str());
    if (size[0] == '+' || size[0] == '-') v = 3 + v;  // atoi keeps the sign
    return kPx[std::min(std::max(v, 1), 7) - 1];
}

// blockFont: the font size of the text the run is in (h1: 48), 0 for the default.
// uaFont: the size the element has when no rule of the page sizes it (an icon: 24), 0 for none.
Style derive(const Style& base, const TagRef& t, uint8_t look, int blockFont, int uaFont = 0) {
    Style s = base;
    // em and % in font-size, and <big>: of the font the run is in
    const float parentPx = static_cast<float>(base.font > 0 ? base.font : blockFont > 0 ? blockFont : UI::Font::SMALL);
    switch (look) {
        case L_STRONG: s.weight = kWeightBold; break;  // b, strong: the bold face, as in a browser
        case L_EM: s.slant = kSlantItalic; break;  // i, em, cite, var, dfn: the italic face, as in a browser
        case L_DIM: case L_RT: s.color = kDim; break;
        case L_SUB:  // font-size: smaller, the baseline down a fifth of the text around and 1px, as Chromium
        case L_SUP: {  // ... up a third and 1px
            s.color = kDim;
            s.font = static_cast<int>(parentPx / 1.2f + 0.5f);
            const float by = look == L_SUB ? parentPx / 5 + 1 : -(parentPx / 3 + 1);
            s.shift = static_cast<int8_t>(by < 0 ? by - 0.5f : by + 0.5f);
            break;
        }
        case L_CODE: s.color = kCode; break;
        case L_MARK: s.color = kMark; break;
        case L_UNDER: s.decor |= kUnderline; break;
        case L_STRIKE: s.decor |= kStrike; break;
        case L_BIG: s.font = static_cast<int>(parentPx * 1.2f + 0.5f); break;  // font-size: larger, as Chromium
        case L_LINK: {
            s.href = attr(t, "href");
            s.onclick = attr(t, "onclick");
            if (s.link()) {
                s.color = kAccent;
                s.decor |= kUnderline;
            }
            break;
        }
        case L_FONT: {
            P::String c = attr(t, "color");
            if (is_color(c.c_str())) s.color = parse_color(c.c_str());  // an invalid color is ignored
            if (int f = fontForHtmlSize(attr(t, "size"))) s.font = f;
            break;
        }
        default: break;
    }
    // Style sheet rules for this element (nav a, .menu a, a.btn) and its style="". A run of text has
    // no object of its own, so it takes what a run can show: color, font size and weight, decoration.
    UI::CssElement el;
    el.tag = t.name;
    el.attrs = rawAttrs(t);
    UI::Css::findAttr(el.attrs, "id", el.id);
    UI::Css::findAttr(el.attrs, "class", el.classes);
    const P::String style = attr(t, "style");
    bool sized = false;
    for (const UI::CssDecl& d : UI::Css::instance().cascade(el, style)) {
        if (d.name == "color") {
            UI::CssColor c;
            if (UI::cssColor(d.value, c)) {  // an invalid color is ignored
                s.color = c.rgb;
                s.opa = c.alpha;
            }
        } else if (d.name == "font-size") {
            if (const int px = UI::cssFontSize(d.value, parentPx); px > 0) s.font = px, sized = true;
        } else if (d.name == "font-weight") {
            if (const int w = UI::cssFontWeight(d.value)) s.weight = w >= 600 ? kWeightBold : kWeightNormal;
        } else if (d.name == "font-style") {
            if (const int f = UI::cssFontStyle(d.value)) s.slant = f == UI::kCssItalic ? kSlantItalic : kSlantNormal;
        } else if (d.name == "text-decoration" || d.name == "text-decoration-line") {
            const P::String v = lower(d.value);
            if (v.find("none") != P::String::npos) s.decor = 0;
            if (v.find("underline") != P::String::npos) s.decor |= kUnderline;
            if (v.find("line-through") != P::String::npos) s.decor |= kStrike;
        }
    }
    if (uaFont && !sized) s.font = uaFont;
    return s;
}

// ============ Icons ============

// The classes of Google's icon markup. Rounded and Sharp are drawn as Outlined, the one set the
// watch has (data/system/fonts/, scripts/make_fonts.py)
bool isIconClass(std::string_view classes) {
    static constexpr std::string_view kIconClasses[] = {
        "material-symbols-outlined", "material-symbols-rounded", "material-symbols-sharp",
        "material-icons", "material-icons-outlined", "material-icons-round", "material-icons-sharp"};
    constexpr std::string_view kSpace = " \t\r\n\f";
    for (size_t i = classes.find_first_not_of(kSpace); i != std::string_view::npos;
         i = classes.find_first_not_of(kSpace, i)) {
        const std::string_view c = classes.substr(i, classes.find_first_of(kSpace, i) - i);
        if (std::find(std::begin(kIconClasses), std::end(kIconClasses), c) != std::end(kIconClasses)) return true;
        i += c.size();
    }
    return false;
}

constexpr int kIconPx = 24;  // Google's style sheet sizes the icon classes so

// ============ Registry: containers built by this layer ============

std::unordered_set<lv_obj_t*> s_flow;                // every flow box
std::unordered_map<lv_obj_t*, lv_obj_t*> s_rootOf;   // page/TelaOS container → its implicit flow root
int s_autoId = 0;

// The decimal digits of n: "42", "-7" (std::to_string: the firmware has it already)
P::String decimal(int n) { return std::to_string(n).c_str(); }

// An id for an element the page gave none: _html_0, _html_1…
P::String autoId() { return "_html_" + decimal(s_autoId++); }

void forgetMargins(const lv_obj_t* box);
void onFlowLayout(lv_event_t* e);

void onFlowDelete(lv_event_t* e) {
    lv_obj_t* o = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    s_flow.erase(o);
    forgetMargins(o);
    for (auto it = s_rootOf.begin(); it != s_rootOf.end(); ++it) {
        if (it->second == o) {
            s_rootOf.erase(it);
            break;
        }
    }
}

void track(lv_obj_t* box) {
    s_flow.insert(box);
    lv_obj_add_event_cb(box, onFlowDelete, LV_EVENT_DELETE, box);
    lv_obj_add_event_cb(box, onFlowLayout, LV_EVENT_LAYOUT_CHANGED, box);
}

// Element object → the box that holds its content (<li> row → body, <dialog> backdrop → window)
std::unordered_map<lv_obj_t*, lv_obj_t*> s_contentOf;

void onContentOwnerDelete(lv_event_t* e) { s_contentOf.erase(static_cast<lv_obj_t*>(lv_event_get_user_data(e))); }

// ============ Margins: a browser's, collapsing ============
//
// A block has the margins a browser gives it above and below (its style sheet: <p> 1em, <h2>
// 0.83em of its own font…), or those of the page's CSS. Between two blocks there is the larger of
// the two: margins collapse. The first child's margin above comes out of its parent and joins the
// parent's when nothing (a padding, a border) is between them, the last child's below too; the
// body's 8px joins its first and last child's. LVGL's flex adds margins up: the margins written
// into the objects are the used ones, so that the sums are the collapsed margins. They are worked
// out again each time flex lays a box out: a child shown, hidden, added or gone changes them.

enum Side { kAbove = 0, kBelow = 1 };
constexpr lv_style_prop_t kMarginProp[] = {LV_STYLE_MARGIN_TOP, LV_STYLE_MARGIN_BOTTOM};

struct Margin {
    int16_t own[2] = {0, 0};  // above, below: the page's CSS, or else a browser's
    uint8_t ua[2] = {0, 0};   // a browser's, in hundredths of the font size...
    float em = 0;             // ...a heading's as a browser works it out; 0: the object's font
    bool keep = false;        // not a block: a line of text, a TelaOS widget (not a <div> of text: a
                              // flow box). Its margins stay as CSS gave them, flex adds them up;
                              // margins around it do not join
};

struct BoxMargins {
    P::Map<const lv_obj_t*, Margin> kids;  // its children's own margins
    int16_t in[2] = {0, 0};        // its first child's margin above, its last child's below...
    bool out[2] = {false, false};  // ...come out of it: no padding or border in their way
    bool list = false;             // a list's box: a list in it has no margins
};

// flow box → its children's margins (onFlowDelete). Never destroyed, as UI::Dom's tree: the
// objects that go at the simulator's exit, after the statics, still find it
P::Map<const lv_obj_t*, BoxMargins>& s_margins = *P::create<P::Map<const lv_obj_t*, BoxMargins>>().release();

void forgetMargins(const lv_obj_t* box) { s_margins.erase(box); }

// While the flow builds a control in its lines: browserSized()
int s_browserSized = 0;
struct BrowserSized {
    BrowserSized() { ++s_browserSized; }
    ~BrowserSized() { --s_browserSized; }
    BrowserSized(const BrowserSized&) = delete;
    BrowserSized& operator=(const BrowserSized&) = delete;
};

// CSS: the largest of the positive margins plus the most negative of the negative ones
int collapsed(int a, int b) { return std::max({a, b, 0}) + std::min({a, b, 0}); }


void setContentBox(lv_obj_t* outer, lv_obj_t* inner) {
    s_contentOf[outer] = inner;
    lv_obj_add_event_cb(outer, onContentOwnerDelete, LV_EVENT_DELETE, outer);
}

lv_obj_t* plainObj(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// A flow box: its children one under another, nothing between them but their margins
lv_obj_t* newBox(lv_obj_t* parent) {
    lv_obj_t* b = plainObj(parent);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    UI::letOverflow(b);  // what sticks out of an HTML block shows, as in a browser; overflow: hidden clips
    track(b);
    return b;
}

// HTML content makes a content-sized TelaOS div a block: full width, like <div> in a browser
// (a 100%-wide child of a content-sized parent would have nothing to measure against)
void fillWidth(lv_obj_t* o) {
#if !defined(LVGL_MOCK_ENABLED)
    if (lv_obj_get_style_width(o, LV_PART_MAIN) == LV_SIZE_CONTENT) lv_obj_set_width(o, lv_pct(100));
#else
    (void)o;
#endif
}

// Text defaults for a TelaOS container that now holds HTML text (keeps its own CSS)
void textDefaults(lv_obj_t* o) {
#if !defined(LVGL_MOCK_ENABLED)
    lv_style_value_t v;
    if (lv_obj_get_local_style_prop(o, LV_STYLE_TEXT_FONT, &v, LV_PART_MAIN) != LV_STYLE_RES_FOUND)
        lv_obj_set_style_text_font(o, Font::get(UI::Font::SMALL), LV_PART_MAIN);
    if (lv_obj_get_local_style_prop(o, LV_STYLE_TEXT_COLOR, &v, LV_PART_MAIN) != LV_STYLE_RES_FOUND)
        lv_obj_set_style_text_color(o, lv_color_hex(kText), LV_PART_MAIN);
#else
    lv_obj_set_style_text_font(o, Font::get(UI::Font::SMALL), LV_PART_MAIN);
#endif
    fillWidth(o);
}

// The root box of host's HTML is a body, with the body's 8px margin: host is a page, a tab, or a
// TelaOS container standing on its own (placed by TelaOS). A container in the flow of a page is a
// browser's <div>: the body is around it already, it has no margin of its own
bool hostsBody(lv_obj_t* host) {
    lv_obj_t* const around = host ? lv_obj_get_parent(host) : nullptr;
    return !(around && s_flow.count(around));
}

// HTML placed straight into a page or TelaOS container goes into its one implicit root box,
// which also takes the container's TelaOS widgets that have no x/y/align (see parse_children)
lv_obj_t* flowParent(lv_obj_t* parent) {
    if (s_flow.count(parent)) return parent;
    auto it = s_rootOf.find(parent);
    if (it != s_rootOf.end()) return it->second;
    fillWidth(parent);
    lv_obj_t* root = newBox(parent);
    lv_obj_set_style_pad_all(root, hostsBody(parent) ? kBodyPad : 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(root, Font::get(UI::Font::SMALL), LV_PART_MAIN);
    lv_obj_set_style_text_color(root, lv_color_hex(kText), LV_PART_MAIN);
    s_rootOf[parent] = root;
    // a long document scrolls, like a page in a browser
    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    return root;
}

// Nearest ancestor that is not a flow box: page or TelaOS container (for dialogs)
lv_obj_t* hostOf(lv_obj_t* box) {
    lv_obj_t* o = box;
    while (o && s_flow.count(o)) o = lv_obj_get_parent(o);
    return o ? o : box;
}

// ============ Clicks: links, onclick ============

struct Action {
    P::String href;
    P::String onclick;
};

void onActionClick(lv_event_t* e) {
    auto* a = static_cast<Action*>(lv_event_get_user_data(e));
    if (!a) return;
    if (!a->onclick.empty()) {
        if (g_onclick_handler) g_onclick_handler(a->onclick.c_str());
        return;
    }
    if (a->href.empty() || UI::Dom::defaultPrevented(e)) return;  // a listener kept the link from going
    if (a->href[0] == '#') {  // in-page anchor: scroll to the element with that id
        if (lv_obj_t* target = g_core.get(a->href.c_str() + 1)) lv_obj_scroll_to_view_recursive(target, LV_ANIM_ON);
        return;
    }
    if (a->href.find("://") != P::String::npos) {
        LOG_I(Log::UI, "html: external link not opened on the watch: %s", a->href.c_str());
        return;
    }
    navigate(a->href.c_str());
}

void onActionDelete(lv_event_t* e) { delete static_cast<Action*>(lv_event_get_user_data(e)); }

void makeClickable(lv_obj_t* o, const P::String& href, const P::String& onclick) {
    if (href.empty() && onclick.empty()) return;
    auto* a = new Action{href, onclick};
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, onActionClick, LV_EVENT_CLICKED, a);
    lv_obj_add_event_cb(o, onActionDelete, LV_EVENT_DELETE, a);
}

// ============ Global attributes: id, class, style, dir, hidden, visible, onclick ============

// The direction an element gets: its own dir= or the one it inherits from its parent
bool rtlIn(const TagRef& t, lv_obj_t* parent) {
    const P::String d = lower(attr(t, "dir"));
    if (d == "rtl" || d == "ltr") return d == "rtl";
    return lv_obj_get_style_base_dir(parent, LV_PART_MAIN) == LV_BASE_DIR_RTL;
}


P::String applyGlobal(const TagRef& t, lv_obj_t* o, const char* tplText = nullptr) {
    P::String id = attr(t, "id");
    P::String cls = attr(t, "class");
    P::String style = attr(t, "style");
    P::String vis = attr(t, "visible");
    if (t.name[0] != '#') {  // a run of text ("#text") is not an element: no selector reaches it, not even *
        Widget{o}.applyCss(t.name.c_str(), id.c_str(), cls.c_str(), rawAttrs(t), style);  // style sheets, then style=""
    }
    applyDir(o, attr(t, "dir").c_str());
    if (has(t, "hidden")) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (!is(t.name, "a")) makeClickable(o, "", attr(t, "onclick"));

    const bool dynVis = vis.find('{') != P::String::npos;
    const bool dynText = tplText && std::string_view(tplText).find('{') != std::string_view::npos;
    if (!id.empty() || dynVis || dynText) {
        if (id.empty()) id = autoId();
        ElementDesc ed;
        ed.id = id.c_str();
        ed.obj = o;
        ed.visibleBind = dynVis ? vis.c_str() : nullptr;
        ed.tpl = dynText ? tplText : nullptr;
        g_core.app().addElement(ed);
    }
    return id;
}

// ============ Inline segment: runs of text + atoms → one text object ============

struct Ctx {
    bool pre = false;
    int listDepth = 0;
};

// h1 … h6: a browser's margin above and below, in hundredths of the heading's own font
constexpr uint8_t kHeadingMargin[] = {67, 83, 100, 133, 167, 233};

// A browser's margin above and below an element (its style sheet), in hundredths of its font
uint8_t uaMargin(std::string_view n, bool inList) {
    static constexpr const char* kOneEm[] = {"p", "blockquote", "figure", "pre", "xmp", "listing", "plaintext"};
    static constexpr const char* kLists[] = {"ul", "ol", "menu", "dir", "dl"};
    if (oneOf(n, kOneEm)) return 100;
    if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6') return kHeadingMargin[n[1] - '1'];
    if (oneOf(n, kLists)) return inList ? 0 : 100;  // a list in a list has none
    if (is(n, "hr")) return 50;
    return 0;
}

// A heading's font size as a browser works it out (h6: 10.72px in 16px text), if o still has the
// font the watch draws that size with (12px): CSS gave it no other; else 0
float headingEm(lv_obj_t* o, float em) {
    const int drawn = UI::Font::sizeOf(lv_obj_get_style_text_font(o, LV_PART_MAIN));
    return em > 0 && drawn == UI::Font::sizeOf(Font::get(static_cast<int>(em), true)) ? em : 0;
}

// o's own margins: those the page's CSS gave it (CSS writes them before this layer sees the
// element), or else a browser's
void ownMargins(lv_obj_t* o, Margin& m) {
    m.em = headingEm(o, m.em);
    const float font = m.em > 0 ? m.em : static_cast<float>(UI::Font::sizeOf(lv_obj_get_style_text_font(o, LV_PART_MAIN)));
    for (int s : {kAbove, kBelow}) {
        lv_style_value_t v;
        const bool css = lv_obj_get_local_style_prop(o, kMarginProp[s], &v, LV_PART_MAIN) == LV_STYLE_RES_FOUND;
        m.own[s] = static_cast<int16_t>(css ? v.num : static_cast<int>(m.ua[s] * font / 100 + 0.5f));
    }
}

// o, new in box: its margins (under its address: what was there before is gone)
void noteObj(lv_obj_t* box, lv_obj_t* o, uint8_t above, uint8_t below, float em = 0, bool keep = false) {
    Margin m;
    m.ua[kAbove] = above;
    m.ua[kBelow] = below;
    m.em = em;
    m.keep = keep;
    ownMargins(o, m);
    s_margins[box].kids[o] = m;
}

// The objects an element made in box, from child `from` on: the first takes a browser's margin
// above, the last the one below. <body>, <html>: their content went into the box itself
void noteBlock(const TagRef& t, lv_obj_t* box, uint32_t from, const Ctx& ctx) {
    if (is(t.name, "body") || is(t.name, "html")) return;
    const uint32_t n = lv_obj_get_child_count(box);
    const uint8_t ua = uaMargin(t.name, ctx.listDepth > 0);
    const std::string_view name = t.name;
    const bool heading = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
    const float em = heading ? static_cast<float>(UI::Font::sizeOf(lv_obj_get_style_text_font(box, LV_PART_MAIN))) *
                                   kHeadingEm[name[1] - '1']
                             : 0;
    const bool tela = !info(t.name);  // a TelaOS widget in HTML: as TelaOS lays it out
    for (uint32_t i = from; i < n; ++i) {
        lv_obj_t* const o = lv_obj_get_child(box, static_cast<int32_t>(i));
        noteObj(box, o, i == from ? ua : 0, i + 1 == n ? ua : 0, em, tela && !s_flow.count(o));
    }
}

// Inside a list: a list there has no margins
bool inList(lv_obj_t* box) {
    for (lv_obj_t* o = box; o; o = lv_obj_get_parent(o)) {
        const auto it = s_margins.find(o);
        if (it != s_margins.end() && it->second.list) return true;
    }
    return false;
}

// The implicit root box of a page or TelaOS container: the body, or the inside of a <div> in the flow
bool isRoot(const lv_obj_t* box) {
    for (const auto& kv : s_rootOf) {
        if (kv.second == box) return true;
    }
    return false;
}

// A box whose children stack as blocks do: a flow box, flex in a column (not one CSS made a row
// or a grid: there each child keeps its margins)
bool stacks(lv_obj_t* o) {
    return s_flow.count(o) && lv_obj_get_style_layout(o, LV_PART_MAIN) == LV_LAYOUT_FLEX &&
           lv_obj_get_style_flex_flow(o, LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN;
}

// What stands for box among its parent's children: the row of an <li> or a <summary> that box is
// the content box of, else box itself
lv_obj_t* standIn(lv_obj_t* box) {
    lv_obj_t* const p = lv_obj_get_parent(box);
    const auto it = p ? s_contentOf.find(p) : s_contentOf.end();
    return it != s_contentOf.end() && it->second == box ? p : box;
}

// A child's content box: an <li>'s body, else the child
const lv_obj_t* contentBox(const lv_obj_t* o) {
    const auto it = s_contentOf.find(const_cast<lv_obj_t*>(o));
    return it != s_contentOf.end() ? it->second : o;
}

// The flow box that what comes out of box goes into; nullptr: none (the body, a cell, a dialog)
lv_obj_t* outerOf(lv_obj_t* box) {
    if (isRoot(box)) return nullptr;
    lv_obj_t* const p = lv_obj_get_parent(standIn(box));
    return p && stacks(p) ? p : nullptr;
}

// Nothing between o's edge on side s and what is in it: no padding, no border; below, no height
// of its own either
bool openAt(lv_obj_t* o, int s) {
    const int pad = s == kAbove ? lv_obj_get_style_pad_top(o, LV_PART_MAIN) : lv_obj_get_style_pad_bottom(o, LV_PART_MAIN);
    const lv_border_side_t side = s == kAbove ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM;
    const bool border = (lv_obj_get_style_border_side(o, LV_PART_MAIN) & side) && lv_obj_get_style_border_width(o, LV_PART_MAIN) > 0;
    return pad == 0 && !border && (s == kAbove || lv_obj_get_style_height(o, LV_PART_MAIN) == LV_SIZE_CONTENT);
}

// A child's margin on side s, joined with what comes out of its content there
int marginOf(const BoxMargins& bm, lv_obj_t* c, int s) {
    const auto k = bm.kids.find(c);
    const int own = k != bm.kids.end() ? k->second.own[s] : 0;
    const auto in = s_margins.find(contentBox(c));
    return in != s_margins.end() && in->second.out[s] ? collapsed(own, in->second.in[s]) : own;
}

// A style written only when it changes: each write lays the box out again, and a 0 that is not
// there needs no room for it either (margins and paddings are not inherited: the object's own)
void setPx(lv_obj_t* o, lv_style_prop_t prop, int v) {
    if (lv_obj_get_style_prop(o, LV_PART_MAIN, prop).num == v) return;
    lv_style_value_t val;
    val.num = v;
    lv_obj_set_local_style_prop(o, prop, val, LV_PART_MAIN);
}

// The used margins of box's children, written into them: flex adds a child's margin below to the
// next one's above, and that sum is the collapsed margin. The body's 8px margin joins its first
// and last child's: its padding takes them. true: what comes out of the box changed, the box
// around it needs its margins again
bool collapse(lv_obj_t* box) {
    BoxMargins& bm = s_margins[box];
    const bool root = isRoot(box);
    const bool block = stacks(box);
    lv_obj_t* const el = standIn(box);
    // the first child's margin above and the last one's below come out of the box only into a
    // block around it, through no padding or border, and not out of a box that scrolls or clips,
    // or one that grows: the box around keeps their margins off it (collapse below)
    const bool may = block && outerOf(box) && lv_obj_has_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE) &&
                     !lv_obj_has_flag(box, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_style_flex_grow(el, LV_PART_MAIN) == 0;
    bool out[2];
    for (int s : {kAbove, kBelow}) out[s] = may && openAt(box, s) && (el == box || openAt(el, s));
    int16_t in[2] = {0, 0};
    const uint32_t n = lv_obj_get_child_count(box);
    lv_obj_t* last = nullptr;  // the block laid out last so far, its margin below not written yet
    bool first = true;
    int below = 0;             // that margin: it joins the next one's above
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* const c = lv_obj_get_child(box, static_cast<int32_t>(i));
        const auto r = bm.kids.emplace(c, Margin{});
        if (r.second) {  // one this layer did not make: a TelaOS widget (a <div> of text is a block)
            r.first->second.keep = !s_flow.count(c);
            ownMargins(c, r.first->second);
        }
        const Margin& m = r.first->second;
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT) ||
            lv_obj_has_flag(c, LV_OBJ_FLAG_FLOATING)) {
            continue;
        }
        // flex items in a row, a grid, a TelaOS widget: each keeps its own. One that grows too: flex
        // leaves the margins of a growing item out of the room it shares, so a margin put on it would
        // make a content-sized box taller each time it is laid out
        if (!block || m.keep || lv_obj_get_style_flex_grow(c, LV_PART_MAIN) > 0) {
            if (last) setPx(last, LV_STYLE_MARGIN_BOTTOM, below);  // flex adds it to c's
            last = nullptr;
            first = false;
            below = 0;
            setPx(c, LV_STYLE_MARGIN_TOP, m.own[kAbove]);
            setPx(c, LV_STYLE_MARGIN_BOTTOM, m.own[kBelow]);
            continue;
        }
        const int above = marginOf(bm, c, kAbove);
        if (last) setPx(last, LV_STYLE_MARGIN_BOTTOM, 0);  // the one before: its margin is in c's
        if (first) in[kAbove] = static_cast<int16_t>(above);
        setPx(c, LV_STYLE_MARGIN_TOP, !first ? collapsed(below, above) : (out[kAbove] || root) ? 0 : above);
        below = marginOf(bm, c, kBelow);
        last = c;
        first = false;
    }
    if (last) {  // the last child a block: its margin below comes out, or the box holds it
        in[kBelow] = static_cast<int16_t>(below);
        setPx(last, LV_STYLE_MARGIN_BOTTOM, (out[kBelow] || root) ? 0 : below);
    }
    if (bm.kids.size() > n) {  // children gone: their margins with them
        P::Map<const lv_obj_t*, Margin> live;
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t* const c = lv_obj_get_child(box, static_cast<int32_t>(i));
            live[c] = bm.kids[c];
        }
        bm.kids.swap(live);
    }
    if (root) {  // the body's margin and its children's: one margin. A <div> in the flow has none of
                 // its own: its children's stay inside it
        const int own = hostsBody(lv_obj_get_parent(box)) ? kBodyPad : 0;
        setPx(box, LV_STYLE_PAD_TOP, collapsed(own, in[kAbove]));
        setPx(box, LV_STYLE_PAD_BOTTOM, collapsed(own, in[kBelow]));
    }
    const bool changed = in[0] != bm.in[0] || in[1] != bm.in[1] || out[0] != bm.out[0] || out[1] != bm.out[1];
    for (int s : {kAbove, kBelow}) {
        bm.in[s] = in[s];
        bm.out[s] = out[s];
    }
    return changed;
}

// box and the boxes around it, outward, as long as what comes out of one changes
void collapseOut(lv_obj_t* box) {
    while (box && collapse(box)) box = outerOf(box);
}

// Flex laid a flow box out: its children may have changed (one shown, hidden, added, gone)
void onFlowLayout(lv_event_t* e) {
    lv_obj_t* const box = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (!collapse(box)) return;
    if (lv_obj_t* const outer = outerOf(box)) lv_obj_mark_layout_as_dirty(outer);
}

class Seg;
void walk(const char* p, const char* end, Seg& seg, const Style& st, bool inlineOnly);
void renderBlock(const TagRef& t, lv_obj_t* box, const Ctx& ctx);
void renderAtom(const TagRef& t, lv_obj_t* parent);

class Seg {
public:
    struct Item {
        enum Type : uint8_t { TEXT, BREAK, ATOM } type;
        P::String text;
        Style st;
        TagRef atom;
    };

    // owner: the text element this segment is (p, h1...): its attributes go on the text object
    Seg(lv_obj_t* box, const Ctx& ctx, const TagRef* owner = nullptr, TextLook look = {})
        : box_(box), ctx_(ctx), owner_(owner), look_(look) {}

    lv_obj_t* box() const { return box_; }
    const Ctx& ctx() const { return ctx_; }
    const TextLook& look() const { return look_; }
    bool empty() const { return items_.empty(); }
    P::Array<Item>& items() { return items_; }

    void text(const char* s, const char* e, const Style& st) {
        P::String out;
        for (const char* p = s; p < e;) {
            if (*p == '&') {
                p = decodeEntity(p, e, out);
                lastSpace_ = false;
                continue;
            }
            if (!ctx_.pre && isspace(static_cast<unsigned char>(*p))) {
                if (!lastSpace_) out += ' ';
                lastSpace_ = true;
                ++p;
                continue;
            }
            if (ctx_.pre && *p == '\t') {
                out += "    ";
                ++p;
                continue;
            }
            out += *p++;
            lastSpace_ = ctx_.pre ? (out.back() == '\n') : false;
        }
        push(out, st);
    }

    // Text as it is shown, entities decoded and spaces collapsed (<bdo> puts its reversed text here)
    void shown(const P::String& out, const Style& st) {
        push(out, st);
        if (!out.empty() && !ctx_.pre) lastSpace_ = out.back() == ' ';
    }
    // A segment read on behalf of `o` (<bdo>): spaces collapse across the boundary as in `o`
    void continueAfter(const Seg& o) { lastSpace_ = o.lastSpace_; }

    void lineBreak() {
        trimTail();
        items_.push_back(Item{Item::BREAK, "\n", {}, {}});
        lastSpace_ = true;
    }

    void atom(const TagRef& t) {
        items_.push_back(Item{Item::ATOM, "", {}, t});
        lastSpace_ = false;
    }

    void flush();

private:
    void push(const P::String& out, const Style& st) {
        if (out.empty()) return;
        if (!items_.empty() && items_.back().type == Item::TEXT && items_.back().st.same(st)) {
            items_.back().text += out;
        } else {
            items_.push_back(Item{Item::TEXT, out, st, {}});
        }
    }
    void trimTail() {
        while (!items_.empty() && items_.back().type == Item::TEXT) {
            P::String& s = items_.back().text;
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (!s.empty()) break;
            items_.pop_back();
        }
    }
    P::String plainText() const {
        P::String s;
        for (const auto& it : items_) s += it.text;
        return s;
    }
    void decorate(lv_obj_t* o) {  // look + owner attributes on the created text object
        applyLook(o, look_);
        if (owner_) applyGlobal(*owner_, o);
    }
    void asLabel();
    void asSpans();
    void asWords();

    lv_obj_t* box_;
    Ctx ctx_;
    const TagRef* owner_;
    TextLook look_;
    P::Array<Item> items_;
    bool lastSpace_ = true;
};

lv_obj_t* newTextLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text);
    return l;
}

void Seg::flush() {
    trimTail();
    while (!items_.empty() && items_.front().type == Item::BREAK) items_.erase(items_.begin());
    if (items_.empty()) {
        lastSpace_ = true;
        return;
    }
    bool atoms = false, formatted = false, shifted = false;
    int links = 0;
    const Style* lastLink = nullptr;
    for (const auto& it : items_) {
        if (it.type == Item::ATOM) atoms = true;
        if (it.type != Item::TEXT) continue;
        if (!it.st.plain()) formatted = true;
        if (it.st.shift) shifted = true;
        if (it.st.link() && !(lastLink && lastLink->same(it.st))) ++links;
        lastLink = it.st.link() ? &it.st : nullptr;
    }
    const uint32_t from = box_ ? lv_obj_get_child_count(box_) : 0;
    // spans cannot move off the line: raised and lowered text (sup, sub) goes word by word too
    if (atoms || links >= 2 || shifted) asWords();
    else if (!formatted) asLabel();
    else asSpans();
    items_.clear();
    if (box_ && s_flow.count(box_)) {  // a line: no block (a <p> is, noteBlock says so)
        for (uint32_t i = from; i < lv_obj_get_child_count(box_); ++i) {
            noteObj(box_, lv_obj_get_child(box_, static_cast<int32_t>(i)), 0, 0, 0, true);
        }
    }
    lastSpace_ = true;
}

// Plain text: one wrapped label; {var} stays live through the TelaOS binding mechanism
void Seg::asLabel() {
    P::String text = plainText();
    const bool dyn = text.find('{') != P::String::npos;
    P::String shown = dyn ? render_template(text.c_str()) : text;
    lv_obj_t* l = newTextLabel(box_, shown.c_str());
    applyLook(l, look_);
    if (owner_) {
        applyGlobal(*owner_, l, dyn ? text.c_str() : nullptr);
    } else if (dyn) {
        TagRef none;  // anonymous text run: registered only to keep its {var} live
        none.name = "#text";
        none.astart = none.aend = "";
        applyGlobal(none, l, text.c_str());
    }
    for (const auto& it : items_) {  // mock LVGL falls back here from spans: keep the link
        if (it.type == Item::TEXT && it.st.link()) {
            makeClickable(l, it.st.href, it.st.onclick);
            break;
        }
    }
}

#if LV_USE_SPAN
// Room for what a glyph draws past its line — an accent above the first line, a descender below the
// last — as LVGL gives a label: a quarter of the tallest line of the spans. Lines are a browser's
// (scripts/make_fonts.py lines), and a browser does not cut the glyphs that stick out of them either
void glyphRoom(lv_event_t* e) {
    lv_obj_t* sg = lv_event_get_current_target_obj(e);
    int32_t line = lv_font_get_line_height(lv_obj_get_style_text_font(sg, LV_PART_MAIN));
    const uint32_t n = lv_spangroup_get_span_count(sg);
    for (uint32_t i = 0; i < n; ++i) {
        lv_span_t* span = lv_spangroup_get_child(sg, static_cast<int32_t>(i));
        lv_style_value_t v;
        if (span && lv_style_get_prop(lv_span_get_style(span), LV_STYLE_TEXT_FONT, &v) == LV_STYLE_RES_FOUND && v.ptr) {
            line = std::max(line, lv_font_get_line_height(static_cast<const lv_font_t*>(v.ptr)));
        }
    }
    lv_event_set_ext_draw_size(e, line / 4);
}

void fillSpans(lv_obj_t* sg, const P::Array<Seg::Item>& items) {
    while (lv_spangroup_get_span_count(sg) > 0) lv_spangroup_delete_span(sg, lv_spangroup_get_child(sg, 0));
    for (const auto& it : items) {
        if (it.type == Seg::Item::ATOM || it.text.empty()) continue;
        lv_span_t* span = lv_spangroup_new_span(sg);
        lv_span_set_text(span, it.text.c_str());
        lv_style_t* s = lv_span_get_style(span);
        if (it.st.color != kInherit) lv_style_set_text_color(s, lv_color_hex(it.st.color));
        if (it.st.opa < LV_OPA_COVER) lv_style_set_text_opa(s, it.st.opa);
        if (const lv_font_t* f = runFont(it.st, lv_obj_get_style_text_font(sg, LV_PART_MAIN))) {
            lv_style_set_text_font(s, f);
        }
        if (it.st.decor) {
            lv_style_set_text_decor(s, static_cast<lv_text_decor_t>(
                                           ((it.st.decor & kUnderline) ? LV_TEXT_DECOR_UNDERLINE : 0) |
                                           ((it.st.decor & kStrike) ? LV_TEXT_DECOR_STRIKETHROUGH : 0)));
        }
    }
    lv_spangroup_refr_mode(sg);
    lv_obj_refresh_ext_draw_size(sg);  // the fonts of the spans are new: glyphRoom
}
#endif

// Formatted text: spans in one spangroup (mock LVGL has no spans: plain label there)
void Seg::asSpans() {
#if LV_USE_SPAN
    lv_obj_t* sg = lv_spangroup_create(box_);
    lv_obj_add_event_cb(sg, glyphRoom, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    lv_obj_set_width(sg, lv_pct(100));
    lv_spangroup_set_mode(sg, LV_SPAN_MODE_BREAK);
    // The look and the element's CSS first: a run that is only bold (<b> in <h1>) takes the size of
    // the font the spangroup ends up with
    decorate(sg);
    // Live {var} inside formatted text: spans are rebuilt from the element source on change
    if (owner_ && memchr(owner_->cstart, '{', owner_->cend - owner_->cstart)) {
        const P::String tpl(owner_->cstart, owner_->cend - owner_->cstart);
        const Ctx ctx = ctx_;
        auto fill = [sg, ctx](const P::String& rendered) {
            UI::Dom::Pause again;  // its <b>, <a>... are recorded once, by the walk that read it first
            Seg tmp(nullptr, ctx);
            walk(rendered.c_str(), rendered.c_str() + rendered.size(), tmp, Style{}, true);
            fillSpans(sg, tmp.items());
        };
        fill(render_template(tpl.c_str()));  // in the page's build: the scopes of the paragraph are open
        auto elem = P::create<UI::Element>();
        elem->id = autoId();
        elem->w.handle = sg;
        elem->tpl = tpl;
        // later, when a {var} changes, the page is built: its rules see the paragraph's ancestors
        // again (#now span, .card b), as they did
        elem->updateFn = [sg, fill](const P::String& rendered) {
            UI::Dom::Pause again;  // the scopes opened again are elements already recorded
            UI::Dom::inScopesOf(sg, [&fill, &rendered] { fill(rendered); });
        };
        g_core.app().elements.push_back(std::move(elem));
    } else {
        fillSpans(sg, items_);
    }
    for (const auto& it : items_) {  // one link in the paragraph: the paragraph is the link
        if (it.type == Item::TEXT && it.st.link()) {
            makeClickable(sg, it.st.href, it.st.onclick);
            break;
        }
    }
#else
    asLabel();
#endif
}

// End of the word of `text` that starts at i: past its space. A {var} stays in one word: the
// word keeps its template and stays live, whatever the value holds.
size_t wordEnd(const P::String& text, size_t i) {
    while (i < text.size()) {
        const char c = text[i++];
        if (c == ' ') return i;  // keep the space
        if (c == '{') {
            const size_t close = text.find_first_of("} ", i);
            if (close != P::String::npos && text[close] == '}') i = close + 1;
        }
    }
    return text.size();
}

// Where a row of words puts its words, as text-align puts a browser's line box: at the start,
// the middle or the end. rtl: the row's line runs right to left, its start is at the right
void placeWords(lv_obj_t* row, bool rtl) {
    lv_flex_align_t place = rtl ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START;
    switch (lv_obj_get_style_text_align(row, LV_PART_MAIN)) {  // the element's or inherited, as in CSS
        case LV_TEXT_ALIGN_CENTER: place = LV_FLEX_ALIGN_CENTER; break;
        case LV_TEXT_ALIGN_RIGHT: place = LV_FLEX_ALIGN_END; break;
        case LV_TEXT_ALIGN_LEFT: place = LV_FLEX_ALIGN_START; break;
        default: break;
    }
    if (lv_obj_get_style_flex_main_place(row, LV_PART_MAIN) != place) {  // else: its own change calls back
        lv_obj_set_style_flex_main_place(row, place, LV_PART_MAIN);
    }
}

// text-align of the row or above it changed (CSS applied later, a script's el.style)
void onWordsStyle(lv_event_t* e) {
    placeWords(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), lv_event_get_user_data(e) != nullptr);
}

// An RTL line keeps its (left-to-right) words in their order and starts at the right; any line
// follows text-align, now and when it changes
void alignWordsRow(lv_obj_t* row) {
    const bool rtl = lv_obj_get_style_base_dir(row, LV_PART_MAIN) == LV_BASE_DIR_RTL;
    if (rtl)
        lv_obj_set_style_base_dir(row, LV_BASE_DIR_LTR, LV_PART_MAIN);
    placeWords(row, rtl);
    lv_obj_add_event_cb(row, onWordsStyle, LV_EVENT_STYLE_CHANGED, rtl ? row : nullptr);
}

// A word of a sup or sub run in a row of words: how far it moves from where the row puts it,
// centered on the line, for its baseline to be `shift` px from the baseline of the text around it
// (font: around), as a browser has it
int baselineMove(const lv_font_t* around, const lv_font_t* own, int shift) {
    if (!around || !own) return shift;
    const int line = lv_font_get_line_height(around), ownLine = lv_font_get_line_height(own);
    const int ascent = line - around->base_line, ownAscent = ownLine - own->base_line;
    return ascent - ownAscent - (line - ownLine) / 2 + shift;
}

// Text with widgets, several links or sup/sub: a row that wraps word by word, like an HTML line box
void Seg::asWords() {
    lv_obj_t* row = plainObj(box_);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);
    UI::letOverflow(row);  // a line box clips nothing either; before the element's CSS (overflow: hidden)
    decorate(row);
    alignWordsRow(row);
    int raise = 0;  // how far a sup goes above the line: the row makes room for it
    for (const auto& it : items_) {
        if (it.type == Item::ATOM) {
            renderAtom(it.atom, row);
            continue;
        }
        if (it.type == Item::BREAK) {
            lv_obj_t* br = plainObj(row);
            lv_obj_set_size(br, lv_pct(100), 0);
            continue;
        }
        const P::String& text = it.text;
        for (size_t i = 0; i < text.size();) {
            const size_t endw = wordEnd(text, i);
            const P::String word = text.substr(i, endw - i);
            i = endw;
            const bool live = word.find('{') != P::String::npos;
            lv_obj_t* w = lv_label_create(row);  // a lone " " too: the gap between two links
            lv_label_set_text(w, live ? render_template(word.c_str()).c_str() : word.c_str());
            if (it.st.color != kInherit) lv_obj_set_style_text_color(w, lv_color_hex(it.st.color), LV_PART_MAIN);
            if (it.st.opa < LV_OPA_COVER) lv_obj_set_style_text_opa(w, it.st.opa, LV_PART_MAIN);
            const lv_font_t* around = lv_obj_get_style_text_font(w, LV_PART_MAIN);  // the row's
            if (const lv_font_t* f = runFont(it.st, around)) lv_obj_set_style_text_font(w, f, LV_PART_MAIN);
#if !defined(LVGL_MOCK_ENABLED)
            if (it.st.decor) {
                lv_obj_set_style_text_decor(w, static_cast<lv_text_decor_t>(
                    ((it.st.decor & kUnderline) ? LV_TEXT_DECOR_UNDERLINE : 0) |
                    ((it.st.decor & kStrike) ? LV_TEXT_DECOR_STRIKETHROUGH : 0)), LV_PART_MAIN);
            }
#endif
            if (it.st.shift) {
                const lv_font_t* own = lv_obj_get_style_text_font(w, LV_PART_MAIN);
                const int dy = baselineMove(around, own, it.st.shift);
                lv_obj_set_style_translate_y(w, dy, LV_PART_MAIN);
                // Its top on its line, moved: above the line, the row makes room for it; below, LVGL
                // takes the moved word into the row's height itself. The line is as high as the
                // higher of the two fonts (a sup of <font size=6> makes it so)
                const int ownLine = lv_font_get_line_height(own);
                const int line = std::max(lv_font_get_line_height(around), ownLine);
                const int top = (line - ownLine) / 2 + dy;
                if (-top > raise) raise = -top;
            }
            makeClickable(w, it.st.href, it.st.onclick);
            if (live) {  // {var}: the TelaOS binding mechanism keeps the word up to date
                TagRef none;
                none.name = "#text";
                none.astart = none.aend = "";
                applyGlobal(none, w, word.c_str());
            }
        }
    }
    if (raise) lv_obj_set_style_pad_top(row, raise, LV_PART_MAIN);
}

// ============ Walking content ============

bool hasBlockOrAtom(const char* p, const char* end) {
    while (p < end) {
        p = static_cast<const char*>(memchr(p, '<', end - p));
        if (!p) return false;
        TagRef t;
        if (p + 1 < end && isalpha(static_cast<unsigned char>(p[1])) && readTag(p, end, t)) {
            p = t.next;
            if (is(t.name, "br") || is(t.name, "wbr")) continue;  // line breaks stay inline
            const TagInfo* ti = info(t.name);
            if (isAtomName(t.name) || (!ti && isTelaTag(t.name))) return true;
            if (ti && ti->kind != Kind::Inline && ti->kind != Kind::Hidden) return true;
            if (ti && ti->kind == Kind::Inline && hasBlockOrAtom(t.cstart, t.cend)) return true;
        } else {
            ++p;
        }
    }
    return false;
}

// Text with its characters in reverse order (UTF-8 aware). A {var} stays whole and live: its
// value is not known here.
P::String reversedText(const P::String& text) {
    P::String out;
    out.reserve(text.size());
    for (size_t end = text.size(); end > 0;) {
        size_t start = end - 1;
        const size_t open = text[start] == '}' ? text.rfind('{', start) : P::String::npos;
        if (open != P::String::npos && text.find(' ', open) > start) {
            start = open;
        } else {
            while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) --start;  // to the lead byte
        }
        out.append(text, start, end - start);
        end = start;
    }
    return out;
}

// <bdo dir="rtl">: its characters go right to left (a bidi override does just that)
void bdoRtl(const TagRef& t, Seg& seg, const Style& st) {
    Seg inner(nullptr, seg.ctx());
    inner.continueAfter(seg);
    walk(t.cstart, t.cend, inner, st, true);
    const auto& items = inner.items();
    for (size_t i = items.size(); i-- > 0;) {
        const auto& it = items[i];
        if (it.type == Seg::Item::TEXT) seg.shown(reversedText(it.text), it.st);
        else if (it.type == Seg::Item::BREAK) seg.lineBreak();
        else seg.atom(it.atom);
    }
}

// <span class="material-symbols-outlined">home</span>, <i class="material-icons">home</i>: in a
// browser the icon font turns the name into the icon; here the name is looked up. An unknown name
// stays text, as it does without the font. False: not an icon, t renders as usual.
bool iconTag(const TagRef& t, Seg& seg, const Style& st) {
    if (!t.cstart || t.cend <= t.cstart) return false;
    std::string_view classes;
    if (!UI::Css::findAttr(rawAttrs(t), "class", classes) || !isIconClass(classes)) return false;
    std::string_view name(t.cstart, static_cast<size_t>(t.cend - t.cstart));
    const size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    name = name.substr(first, name.find_last_not_of(" \t\r\n") + 1 - first);
    if (name.find_first_of("<&") != std::string_view::npos) return false;  // markup or an entity: text
    P::String rendered;  // {var}: the name it holds now (a live paragraph rebuilds on change)
    if (name.find('{') != std::string_view::npos) {
        rendered = render_template(P::String(name).c_str());
        name = rendered;
    }
    const uint32_t code = UI::Font::iconCode(name);
    if (!code) return false;
    P::String glyph;
    appendUtf8(glyph, code);
    seg.shown(glyph, derive(st, t, L_PLAIN, seg.look().font, kIconPx));  // <i> too: no em look
    return true;
}

void inlineTag(const TagRef& t, const TagInfo* ti, Seg& seg, const Style& st) {
    const uint8_t look = ti ? ti->look : L_PLAIN;
    if (look == L_SKIP) return;
    if (iconTag(t, seg, st)) return;
    Style s = derive(st, t, look, seg.look().font);
    Inside in(t);
    if (is(t.name, "bdo") && lower(attr(t, "dir")) == "rtl") {
        bdoRtl(t, seg, s);
        return;
    }
    if (look == L_QUOTE) seg.text("\xE2\x80\x9C", "\xE2\x80\x9C" + 3, s);  // “
    if (look == L_RT) seg.text("(", "(" + 1, s);
    walk(t.cstart, t.cend, seg, s, true);
    if (look == L_RT) seg.text(")", ")" + 1, s);
    if (look == L_QUOTE) seg.text("\xE2\x80\x9D", "\xE2\x80\x9D" + 3, s);  // ”
}

void parseStyleTag(const TagRef& t) {
    UI::Css::instance().parse(std::string_view(t.cstart, t.cend - t.cstart));
    LOG_D(Log::UI, "html: <style> in body parsed");
}

// rel="stylesheet": one of the words of rel. "alternate stylesheet" is off until chosen, as in a
// browser
bool linksStylesheet(std::string_view rel) {
    bool sheet = false;
    bool alternate = false;
    size_t i = 0;
    while (i < rel.size()) {
        while (i < rel.size() && isspace(static_cast<unsigned char>(rel[i])))
            ++i;
        const size_t from = i;
        while (i < rel.size() && !isspace(static_cast<unsigned char>(rel[i])))
            ++i;
        const std::string_view word = rel.substr(from, i - from);
        if (sameNoCase(word, "stylesheet"))
            sheet = true;
        else if (sameNoCase(word, "alternate"))
            alternate = true;
    }
    return sheet && !alternate;
}

// <link> on a page: its stylesheet counts from here on, as a <style> here would. It draws nothing
void linkTag(const TagRef& t) {
    linkStylesheet(attr(t, "rel"), attr(t, "href"), attr(t, "media"), has(t, "disabled"));
}

void handleTag(const TagRef& t, Seg& seg, const Style& st, bool inlineOnly) {
    const char* n = t.name.c_str();
    if (is(n, "br")) {
        seg.lineBreak();
        return;
    }
    if (is(n, "wbr")) return;
    if (is(n, "link")) {
        linkTag(t);
        return;
    }
    const TagInfo* ti = info(n);
    if (ti && ti->kind == Kind::Hidden) return;
    if (is(n, "style")) {
        parseStyleTag(t);
        return;
    }
    if (isAtomName(n)) {
        seg.atom(t);
        return;
    }
    if (ti && ti->kind == Kind::Inline) {
        // an inline element wrapping blocks or widgets (<a><img></a>) becomes a tappable box
        if (!inlineOnly && hasBlockOrAtom(t.cstart, t.cend)) {
            seg.flush();
            lv_obj_t* b = newBox(seg.box());
            Style s = derive(st, t, ti->look, seg.look().font);
            makeClickable(b, s.href, s.onclick);
            applyGlobal(t, b);
            Inside in(t);
            Seg inner(b, seg.ctx());
            walk(t.cstart, t.cend, inner, s, false);
            inner.flush();
            collapse(b);
            noteObj(seg.box(), b, 0, 0);
            return;
        }
        inlineTag(t, ti, seg, st);
        return;
    }
    if (!ti && !isTelaTag(n)) {  // unknown element: its content shows, as in a browser
        Inside in(t);
        walk(t.cstart, t.cend, seg, st, inlineOnly);
        return;
    }
    if (inlineOnly) {  // a block inside inline content: keep its text in the line
        Inside in(t);
        walk(t.cstart, t.cend, seg, st, true);
        return;
    }
    seg.flush();
    const uint32_t from = lv_obj_get_child_count(seg.box());
    renderBlock(t, seg.box(), seg.ctx());
    noteBlock(t, seg.box(), from, seg.ctx());
}

void walk(const char* p, const char* end, Seg& seg, const Style& st, bool inlineOnly) {
    while (p < end) {
        if (*p != '<') {
            const char* s = p;
            while (p < end && *p != '<') ++p;
            seg.text(s, p, st);
            continue;
        }
        if (commentAt(p, end)) {
            p = skipComment(p, end);
            continue;
        }
        if (p + 1 < end && (p[1] == '/' || p[1] == '!' || p[1] == '?')) {  // stray end tag, doctype
            while (p < end && *p != '>') ++p;
            if (p < end) ++p;
            continue;
        }
        TagRef t;
        if (!readTag(p, end, t)) {  // a lone '<' is text
            seg.text(p, p + 1, st);
            ++p;
            continue;
        }
        p = t.next;
        handleTag(t, seg, st, inlineOnly);
    }
}

void renderInto(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    Inside in(t);
    Seg seg(box, ctx);
    walk(t.cstart, t.cend, seg, Style{}, false);
    seg.flush();
    collapse(box);  // its children are all there: their margins
}

// ============ Elements ============

void delegate(const TagRef& t, lv_obj_t* box) {  // a TelaOS tag inside HTML content
    if (!t.src) return;
    parse_children(t.src, static_cast<int>(t.next - t.src), box);
    // inside HTML flow a TelaOS div/table is a block: full width unless it says otherwise
    // (a content-sized table would also leave its 100%-wide rows nothing to measure against)
    if ((is(t.name, "div") || is(t.name, "table")) && attr(t, "w").empty()) {
        if (lv_obj_t* d = lv_obj_get_child(box, -1)) lv_obj_set_width(d, lv_pct(100));
    }
}

void textElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const TextLook look = lookFor(t.name, UI::Font::sizeOf(lv_obj_get_style_text_font(box, LV_PART_MAIN)));
    if (!hasBlockOrAtom(t.cstart, t.cend)) {
        Inside in(t);  // its runs and widgets are inside it; its own style ignores this (see CssScope)
        Seg seg(box, ctx, &t, look);
        walk(t.cstart, t.cend, seg, Style{}, true);
        if (seg.empty() && !attr(t, "id").empty()) {  // empty but addressable: keep a label for ui.setText
            lv_obj_t* l = newTextLabel(box, "");
            applyLook(l, look);
            applyGlobal(t, l);
            return;
        }
        seg.flush();
        return;
    }
    lv_obj_t* b = newBox(box);
    applyLook(b, look);
    applyGlobal(t, b);
    renderInto(t, b, ctx);
}

void disableControls(lv_obj_t* o);

void blockElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const char* n = t.name.c_str();
    if (is(n, "body")) {  // the flow root already is the body
        applyDir(box, attr(t, "dir").c_str());
        renderInto(t, box, ctx);
        return;
    }
    lv_obj_t* b = newBox(box);
    // a browser's margins aside: LVGL takes them off the block's 100% width, as CSS does
    if (is(n, "blockquote") || is(n, "figure")) {
        lv_obj_set_style_margin_left(b, kBlockIndent, LV_PART_MAIN);
        lv_obj_set_style_margin_right(b, kBlockIndent, LV_PART_MAIN);
        if (is(n, "blockquote")) lv_obj_set_style_text_color(b, lv_color_hex(kH2), LV_PART_MAIN);
    } else if (is(n, "dd")) {  // on the side its lines start
        if (rtlIn(t, box)) lv_obj_set_style_margin_right(b, kBlockIndent, LV_PART_MAIN);
        else lv_obj_set_style_margin_left(b, kBlockIndent, LV_PART_MAIN);
    } else if (is(n, "fieldset")) {  // 2px aside, a 2px frame, 0.35em 0.75em 0.625em in it
        const int em = UI::Font::sizeOf(lv_obj_get_style_text_font(b, LV_PART_MAIN));
        lv_obj_set_style_margin_left(b, 2, LV_PART_MAIN);
        lv_obj_set_style_margin_right(b, 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(b, lv_color_hex(kRule), LV_PART_MAIN);
        lv_obj_set_style_pad_top(b, (em * 35 + 50) / 100, LV_PART_MAIN);
        lv_obj_set_style_pad_left(b, (em * 75 + 50) / 100, LV_PART_MAIN);
        lv_obj_set_style_pad_right(b, (em * 75 + 50) / 100, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(b, (em * 625 + 500) / 1000, LV_PART_MAIN);
    } else if (is(n, "address")) {  // italic, as in a browser
        const lv_font_t* f = lv_obj_get_style_text_font(b, LV_PART_MAIN);
        lv_obj_set_style_text_font(b, Font::get(UI::Font::sizeOf(f), UI::Font::isBold(f), true), LV_PART_MAIN);
    } else if (is(n, "center")) {
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    applyGlobal(t, b);
    renderInto(t, b, ctx);
    if (is(n, "fieldset") && has(t, "disabled")) disableControls(b);  // every control inside is off
}

// ---- Lists ----

P::String roman(int v, bool upper) {
    static const struct { int v; const char* s; } kR[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
                                                         {100, "c"},  {90, "xc"},  {50, "l"},  {40, "xl"},
                                                         {10, "x"},   {9, "ix"},   {5, "v"},   {4, "iv"},
                                                         {1, "i"}};
    P::String r;
    for (const auto& e : kR) {
        while (v >= e.v) {
            r += e.s;
            v -= e.v;
        }
    }
    if (upper) for (auto& c : r) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return r;
}

P::String olMarker(int n, char type) {
    if ((type == 'a' || type == 'A') && n > 0) {
        P::String s;
        for (int v = n; v > 0; v = (v - 1) / 26) s.insert(s.begin(), static_cast<char>((type == 'a' ? 'a' : 'A') + (v - 1) % 26));
        return s + ".";
    }
    if ((type == 'i' || type == 'I') && n > 0) return roman(n, type == 'I') + ".";
    return decimal(n) + ".";
}

// What an item draws out of its box: its marker, in the list's padding (LVGL draws the children of
// an object only so far out of it)
void markerRoom(lv_event_t* e) { lv_event_set_ext_draw_size(e, kListPad + kMarkerGap); }

// An item: its marker outside it, in the list's padding, as a browser's list-style-position:
// outside; the item is as wide as the list's content
void listItem(const TagRef& li, lv_obj_t* parent, const Ctx& ctx, const P::String& marker) {
    lv_obj_t* row = plainObj(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    UI::letOverflow(row);
    lv_obj_add_event_cb(row, markerRoom, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    lv_obj_refresh_ext_draw_size(row);
    lv_obj_t* m = lv_label_create(row);
    lv_label_set_text(m, marker.c_str());
    lv_obj_set_style_text_color(m, lv_color_hex(kDim), LV_PART_MAIN);
    lv_obj_add_flag(m, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(m, kListPad);  // the padding it sits in: a longer marker goes on out of it
    const bool rtl = rtlIn(li, parent);
    lv_obj_set_style_text_align(m, rtl ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(m, rtl ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, rtl ? kListPad + kMarkerGap : -(kListPad + kMarkerGap), 0);
    lv_obj_t* body = newBox(row);
    lv_obj_set_width(body, 0);
    lv_obj_set_flex_grow(body, 1);
    applyGlobal(li, row);
    setContentBox(row, body);
    if (s_flow.count(parent)) noteObj(parent, row, 0, 0);
    Ctx inner = ctx;
    inner.listDepth = ctx.listDepth + 1;
    renderInto(li, body, inner);
}

void listElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    lv_obj_t* lb = newBox(box);
    s_margins[lb].list = true;
    // 40px on the side its lines start, every list, nested ones too: the markers sit in it
    if (rtlIn(t, box)) lv_obj_set_style_pad_right(lb, kListPad, LV_PART_MAIN);
    else lv_obj_set_style_pad_left(lb, kListPad, LV_PART_MAIN);
    applyGlobal(t, lb);
    const bool ordered = is(t.name, "ol");
    P::String type = attr(t, "type");
    const char ty = type.empty() ? '1' : type[0];
    const bool reversed = has(t, "reversed");

    P::Array<TagRef> items;
    for (const char* p = t.cstart; p < t.cend;) {
        p = static_cast<const char*>(memchr(p, '<', t.cend - p));
        if (!p) break;
        TagRef c;
        if (p + 1 < t.cend && isalpha(static_cast<unsigned char>(p[1])) && readTag(p, t.cend, c)) {
            items.push_back(c);
            p = c.next;
        } else {
            ++p;
        }
    }
    int count = 0;
    for (const auto& c : items) count += is(c.name, "li");
    int n = static_cast<int>(attrNum(t.astart, t.aend, "start", reversed ? count : 1));
    // • – › : glyphs the Ubuntu fonts have (no ◦ or ▪ in flash)
    static const char* const kBullets[] = {"\xE2\x80\xA2", "\xE2\x80\x93", "\xE2\x80\xBA"};
    Inside in(t);
    Ctx inside = ctx;
    inside.listDepth = ctx.listDepth + 1;
    for (const auto& c : items) {
        if (!is(c.name, "li")) {  // a list nested straight in a list, or other content
            const uint32_t from = lv_obj_get_child_count(lb);
            renderBlock(c, lb, ctx);
            noteBlock(c, lb, from, inside);
            continue;
        }
        if (!attr(c, "value").empty()) n = atoi(attr(c, "value").c_str());
        const P::String marker = ordered ? olMarker(n, ty) : P::String(kBullets[ctx.listDepth % 3]);
        listItem(c, lb, ctx, marker);
        n += reversed ? -1 : 1;
    }
    collapse(lb);
}

// ---- Rules, images, preformatted ----

void hrElement(const TagRef& t, lv_obj_t* box) {
    lv_obj_t* hr = plainObj(box);
    lv_obj_set_size(hr, lv_pct(100), 1);
    lv_obj_set_style_bg_color(hr, lv_color_hex(kRule), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hr, LV_OPA_COVER, LV_PART_MAIN);
    applyGlobal(t, hr);
}

void imgElement(const TagRef& t, lv_obj_t* parent) {
    P::String src = attr(t, "src");
    if (src.empty()) {
        P::String alt = attr(t, "alt");
        if (!alt.empty()) {
            lv_obj_t* l = lv_label_create(parent);
            lv_label_set_text(l, alt.c_str());
            lv_obj_set_style_text_color(l, lv_color_hex(kDim), LV_PART_MAIN);
            applyGlobal(t, l);
        }
        return;
    }
    // HTML attributes → TelaOS <image>
    P::String a = " src=\"" + src + "\"";
    P::String w = attr(t, "width"), h = attr(t, "height"), oc = attr(t, "onclick"), id = attr(t, "id");
    if (w.empty()) w = attr(t, "w");
    if (h.empty()) h = attr(t, "h");
    if (!w.empty()) a += " w=\"" + w + "\"";
    if (!h.empty()) a += " h=\"" + h + "\"";
    if (!oc.empty()) a += " onclick=\"" + oc + "\"";
    if (const P::String alt = attr(t, "alt"); !alt.empty()) a += " alt=\"" + alt + "\"";  // shown if the file is not there
    if (!id.empty()) a += " id=\"" + id + "\"";
    UI::Dom::As as(t.name, rawAttrs(t));  // for scripts it stays the <img> it is
    create_image(a.c_str(), a.c_str() + a.size(), parent);
}

void preElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const char* s = t.cstart;
    if (s < t.cend && *s == '\r') ++s;
    if (s < t.cend && *s == '\n') ++s;  // a newline right after <pre> is not content
    if (is(t.name, "pre") || is(t.name, "listing")) {  // <listing> = old <pre>
        Ctx pre = ctx;
        pre.pre = true;
        Inside in(t);
        Seg seg(box, pre, &t, TextLook{});
        walk(s, t.cend, seg, Style{}, true);
        seg.flush();
        return;
    }
    P::String raw(s, t.cend - s);  // xmp, plaintext: literal, tags included
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == ' ')) raw.pop_back();
    lv_obj_t* l = newTextLabel(box, raw.c_str());
    applyGlobal(t, l);
}

void marqueeElement(const TagRef& t, lv_obj_t* box) {
    Seg tmp(nullptr, Ctx{});
    {
        Inside in(t);
        walk(t.cstart, t.cend, tmp, Style{}, true);
    }
    P::String text;
    for (const auto& it : tmp.items()) text += it.text;
    lv_obj_t* l = lv_label_create(box);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(l, text.c_str());
    applyGlobal(t, l);
}

// ---- Forms ----

struct Toggle {  // checkbox / radio
    P::String bind;
    P::String onchange;
    P::String name;  // radio group
    bool radio = false;
};
std::unordered_map<lv_obj_t*, Toggle> s_toggles;

void onToggleDelete(lv_event_t* e) { s_toggles.erase(static_cast<lv_obj_t*>(lv_event_get_user_data(e))); }

void onToggleChanged(lv_event_t* e) {
    if (g_core.app().isUpdatingFromBinding()) return;
    lv_obj_t* cb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    auto it = s_toggles.find(cb);
    if (it == s_toggles.end()) return;
    const Toggle tg = it->second;
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    if (tg.radio) {  // a radio turns itself on and its group off
        if (!checked) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
            checked = true;
        }
        for (auto& [o, other] : s_toggles) {
            if (o != cb && other.radio && other.name == tg.name) lv_obj_clear_state(o, LV_STATE_CHECKED);
        }
    }
    const char* val = checked ? "true" : "false";
    if (!tg.bind.empty()) {
        ui_update_bindings(tg.bind.c_str(), val);
        if (g_state_change_handler) g_state_change_handler(tg.bind.c_str(), val);
    }
    if (!tg.onchange.empty() && g_onclick_handler) g_onclick_handler(tg.onchange.c_str());
}

void checkboxElement(const TagRef& t, lv_obj_t* parent, bool radio) {
    lv_obj_t* cb = lv_checkbox_create(parent);
    const P::String label = attr(t, "label");
    lv_checkbox_set_text(cb, label.c_str());
    if (label.empty()) {
        // The input is its box, as in HTML: the gap LVGL keeps for the text moves outside it, so the
        // text after it stays where it was, and an outline or a border from CSS goes round the box
        lv_obj_set_style_margin_right(cb, lv_obj_get_style_pad_column(cb, LV_PART_MAIN), LV_PART_MAIN);
        lv_obj_set_style_pad_column(cb, 0, LV_PART_MAIN);
    }
    if (radio) lv_obj_set_style_radius(cb, LV_RADIUS_CIRCLE, LV_PART_MAIN);  // its ring is round too
    Toggle tg;
    tg.bind = attr(t, "bind");
    tg.onchange = attr(t, "onchange");
    tg.name = attr(t, "name");
    tg.radio = radio;
    bool checked = has(t, "checked");
    if (!tg.bind.empty()) {
        P::String v = g_core.store().getString(tg.bind);
        checked = (v == "true" || v == "1");
    }
    if (checked) lv_obj_add_state(cb, LV_STATE_CHECKED);
    if (has(t, "disabled")) lv_obj_add_state(cb, LV_STATE_DISABLED);
#if !defined(LVGL_MOCK_ENABLED)
    if (radio) lv_obj_set_style_radius(cb, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
#endif
    s_toggles[cb] = tg;
    lv_obj_add_event_cb(cb, onToggleChanged, LV_EVENT_VALUE_CHANGED, cb);
    lv_obj_add_event_cb(cb, onToggleDelete, LV_EVENT_DELETE, cb);
    applyGlobal(t, cb);
}

bool isButton(lv_obj_t* o) {
#if defined(LVGL_MOCK_ENABLED)
    return lv_obj_check_type(o, &lv_btn_class);
#else
    return lv_obj_check_type(o, &lv_button_class);
#endif
}

bool isControl(lv_obj_t* o) {
    return s_toggles.count(o) || isButton(o) || lv_obj_check_type(o, &lv_textarea_class) ||
           lv_obj_check_type(o, &lv_slider_class) || lv_obj_check_type(o, &lv_dropdown_class) ||
           lv_obj_check_type(o, &lv_switch_class);
}

void disableControls(lv_obj_t* o) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) {
        lv_obj_t* c = lv_obj_get_child(o, static_cast<int32_t>(i));
        if (isControl(c)) lv_obj_add_state(c, LV_STATE_DISABLED);
        disableControls(c);
    }
}

// What a tap on a <label for> does to its control: a checkbox or a switch toggles, a radio
// turns on, a text field takes the keyboard, a list opens, anything else is clicked
void activate(lv_obj_t* target) {
    if (!target || lv_obj_has_state(target, LV_STATE_DISABLED)) return;
    const auto toggle = s_toggles.find(target);
    if (toggle != s_toggles.end() || lv_obj_check_type(target, &lv_switch_class)) {
        const bool radio = toggle != s_toggles.end() && toggle->second.radio;
        if (radio || !lv_obj_has_state(target, LV_STATE_CHECKED)) lv_obj_add_state(target, LV_STATE_CHECKED);
        else lv_obj_remove_state(target, LV_STATE_CHECKED);
        lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, nullptr);
    } else if (lv_obj_check_type(target, &lv_textarea_class)) {
        lv_obj_send_event(target, LV_EVENT_FOCUSED, nullptr);
    } else if (lv_obj_check_type(target, &lv_dropdown_class)) {
        lv_dropdown_open(target);
    } else {
        lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
    }
}

std::unordered_map<lv_obj_t*, P::String> s_labelFor;        // label -> id of its control
std::unordered_map<lv_obj_t*, lv_obj_t*> s_labelControl;    // label -> the control inside it

void onLabelClick(lv_event_t* e) {
    if (UI::Dom::defaultPrevented(e)) return;  // a listener keeps the tap from its box
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const auto it = s_labelFor.find(label);
    if (it != s_labelFor.end()) {
        activate(g_core.app().findElement(it->second.c_str()));
        return;
    }
    const auto in = s_labelControl.find(label);
    if (in != s_labelControl.end()) activate(in->second);
}

void onLabelDelete(lv_event_t* e) {
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    s_labelFor.erase(label);
    s_labelControl.erase(label);
}

lv_obj_t* findControl(lv_obj_t* o) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) {
        lv_obj_t* c = lv_obj_get_child(o, static_cast<int32_t>(i));
        if (isControl(c)) return c;
        if (lv_obj_t* inner = findControl(c)) return inner;
    }
    return nullptr;
}

// A boolean attribute (`readonly`, `disabled`) passed on to a TelaOS builder
P::String flagFor(const TagRef& t, const char* name) {
    if (!has(t, name)) return "";
    const P::String v = attr(t, name);
    return P::String(" ") + name + "=\"" + (v.empty() ? P::String("true") : v) + "\"";
}

// Find the textarea a TelaOS input builder just created in parent
lv_obj_t* lastTextarea(lv_obj_t* parent) {
    lv_obj_t* last = lv_obj_get_child(parent, -1);
    if (!last) return nullptr;
    if (lv_obj_check_type(last, &lv_textarea_class)) return last;
    for (int i = static_cast<int>(lv_obj_get_child_count(last)) - 1; i >= 0; --i) {
        lv_obj_t* c = lv_obj_get_child(last, i);
        if (c && lv_obj_check_type(c, &lv_textarea_class)) return c;
    }
    return nullptr;
}

P::String keepAttrs(const TagRef& t, std::initializer_list<const char*> names) {
    P::String a;
    for (const char* n : names) {
        P::String v = attr(t, n);
        if (!v.empty()) a += P::String(" ") + n + "=\"" + v + "\"";
    }
    return a;
}

void inputElement(const TagRef& t, lv_obj_t* parent) {
    const P::String type = lower(attr(t, "type"));
    if (type == "hidden") return;
    if (type == "checkbox" || type == "radio") {
        checkboxElement(t, parent, type == "radio");
        return;
    }
    if (type == "range") {
        P::String a = keepAttrs(t, {"id", "class", "min", "max", "step", "bind", "onchange", "w", "h"}) +
                      flagFor(t, "disabled");
        if (attr(t, "min").empty()) a += " min=\"0\"";
        if (attr(t, "max").empty()) a += " max=\"100\"";
        {
            UI::Dom::As as(t.name, rawAttrs(t));
            create_slider(a.c_str(), a.c_str() + a.size(), parent);
        }
        lv_obj_t* s = lv_obj_get_child(parent, -1);
        if (s && attr(t, "bind").empty() && !attr(t, "value").empty()) {
            const int32_t value = atoi(attr(t, "value").c_str());
            lv_slider_set_value(s, snapToStep(value, atoi(attr(t, "min").c_str()), atoi(attr(t, "step").c_str())),
                                LV_ANIM_OFF);
        }
        return;
    }
    if (type == "button" || type == "submit" || type == "reset") {
        P::String label = attr(t, "value");
        if (label.empty()) label = type == "submit" ? "Submit" : type == "reset" ? "Reset" : "";
        P::String a = keepAttrs(t, {"id", "class", "onclick", "href", "w", "h", "bgcolor", "color"}) +
                      flagFor(t, "disabled");
        UI::Dom::As as(t.name, rawAttrs(t));
        create_button(a.c_str(), a.c_str() + a.size(), label.c_str(), parent);
        return;
    }
    if (type == "image") {
        imgElement(t, parent);
        return;
    }
    // text-like: text, password, email, search, tel, url, number, date, time, color, file...
    P::String a = keepAttrs(t, {"id", "class", "bind", "placeholder", "onchange", "oninput", "onenter", "onblur",
                                "w", "h", "bgcolor", "color", "maxlength", "size"}) +
                  flagFor(t, "readonly") + flagFor(t, "disabled");
    if (type == "password") a += " password=\"true\"";
    {
        UI::Dom::As as(t.name, rawAttrs(t));
        create_input(a.c_str(), a.c_str() + a.size(), "", parent);
    }
    if (attr(t, "bind").empty() && !attr(t, "value").empty()) {
        if (lv_obj_t* ta = lastTextarea(parent)) lv_textarea_set_text(ta, attr(t, "value").c_str());
    }
}

void textareaElement(const TagRef& t, lv_obj_t* parent) {
    // the size: a browser's, from cols= and rows= (create_input), unless w= or h= says
    P::String a = keepAttrs(t, {"id", "class", "bind", "placeholder", "onchange", "oninput", "onblur", "w", "h",
                                "bgcolor", "color", "maxlength", "cols", "rows"}) +
                  flagFor(t, "readonly") + flagFor(t, "disabled");
    a += " multiline=\"true\"";
    {
        UI::Dom::As as(t.name, rawAttrs(t));
        create_input(a.c_str(), a.c_str() + a.size(), "", parent);
    }
    if (attr(t, "bind").empty() && t.cend > t.cstart) {
        P::String text;
        const char* s = t.cstart;
        if (*s == '\n') ++s;
        for (const char* p = s; p < t.cend;) {
            if (*p == '&') p = decodeEntity(p, t.cend, text);
            else text += *p++;
        }
        if (lv_obj_t* ta = lastTextarea(parent)) lv_textarea_set_text(ta, text.c_str());
    }
}

void barElement(const TagRef& t, lv_obj_t* parent, bool meter) {
    lv_obj_t* b = lv_bar_create(parent);
    P::String w = attr(t, "width");
    if (w.empty()) w = attr(t, "w");
    // inline, browser default size: progress 10em, meter 5em
    lv_obj_set_size(b, w.empty() ? (meter ? 80 : 160) : parse_size(w.c_str()), 10);
    const double mn = meter ? attrNum(t.astart, t.aend, "min", 0) : 0;
    const double mx = attrNum(t.astart, t.aend, "max", meter ? 1 : 1);
    const double v = attrNum(t.astart, t.aend, "value", mn);
    const double span = (mx > mn) ? (mx - mn) : 1;
    lv_bar_set_range(b, 0, 1000);
    lv_bar_set_value(b, static_cast<int32_t>((v - mn) / span * 1000 + 0.5), LV_ANIM_OFF);
    uint32_t color = kAccent;
    if (meter) {
        // HTML: low and high cut [min, max] in three parts; the part with optimum (the middle by
        // default) is good, the next one not so good, the far one bad. A value on a boundary
        // counts in the part nearer the optimum, as browsers draw it.
        const auto clamp = [](double x, double a, double b) { return x < a ? a : x > b ? b : x; };
        const double top = mx > mn ? mx : mn;
        const double val = clamp(v, mn, top);
        const double lo = clamp(attrNum(t.astart, t.aend, "low", mn), mn, top);
        const double hi = clamp(attrNum(t.astart, t.aend, "high", top), lo, top);
        const double opt = clamp(attrNum(t.astart, t.aend, "optimum", (mn + top) / 2), mn, top);
        int far;  // parts from the optimum's to the value's
        if (opt < lo) far = val <= lo ? 0 : val <= hi ? 1 : 2;
        else if (opt > hi) far = val >= hi ? 0 : val >= lo ? 1 : 2;
        else far = (val >= lo && val <= hi) ? 0 : 1;
        color = far == 0 ? kCode : far == 1 ? kMark : kBad;
    }
    lv_obj_set_style_radius(b, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(kRule), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    applyGlobal(t, b);
}

// ---- Interactive: details/summary, dialog ----

struct Fold {
    lv_obj_t* body;
    lv_obj_t* marker;
};

void onFoldClick(lv_event_t* e) {
    if (UI::Dom::defaultPrevented(e)) return;  // a listener keeps <details> as it is
    auto* f = static_cast<Fold*>(lv_event_get_user_data(e));
    const bool open = lv_obj_has_flag(f->body, LV_OBJ_FLAG_HIDDEN);
    if (open) lv_obj_clear_flag(f->body, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(f->body, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(f->marker, open ? "\xE2\x80\x93" : "+");  // – / +
}

void onFoldDelete(lv_event_t* e) { delete static_cast<Fold*>(lv_event_get_user_data(e)); }

bool findChild(const TagRef& t, const char* name, TagRef& out) {
    for (const char* p = t.cstart; p < t.cend;) {
        p = static_cast<const char*>(memchr(p, '<', t.cend - p));
        if (!p) return false;
        if (p + 1 < t.cend && isalpha(static_cast<unsigned char>(p[1])) && readTag(p, t.cend, out)) {
            if (is(out.name, name)) return true;
            p = out.next;
        } else {
            ++p;
        }
    }
    return false;
}

// The marker of a <summary> inside it, before its text, as a browser's list-style: inside: the
// room it takes is a browser's disclosure marker and the space after it, 1.06em (16.94px in 16px)
void summaryMarker(lv_obj_t* marker, lv_obj_t* text, bool rtl) {
    if (!text) return;
    const lv_font_t* font = lv_obj_get_style_text_font(text, LV_PART_MAIN);
    lv_obj_set_style_text_font(marker, font, LV_PART_MAIN);
    const int room = (UI::Font::sizeOf(font) * 106 + 50) / 100;
    const int before = rtl ? lv_obj_get_style_pad_right(text, LV_PART_MAIN) : lv_obj_get_style_pad_left(text, LV_PART_MAIN);
    if (rtl) lv_obj_set_style_pad_right(text, before + room, LV_PART_MAIN);
    else lv_obj_set_style_pad_left(text, before + room, LV_PART_MAIN);
    lv_obj_align(marker, rtl ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, rtl ? -before : before,
                 lv_obj_get_style_pad_top(text, LV_PART_MAIN));
}

void detailsElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    lv_obj_t* d = newBox(box);
    applyGlobal(t, d);
    const bool open = has(t, "open");
    const bool rtl = rtlIn(t, box);

    // The summary's line: its text from the line's start, the marker over the room it leaves
    lv_obj_t* head = plainObj(d);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_COLUMN);
    UI::letOverflow(head);
    lv_obj_t* title = newBox(head);
    setContentBox(head, title);  // its text is the summary's, without the marker
    lv_obj_t* marker = lv_label_create(head);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_label_set_text(marker, open ? "\xE2\x80\x93" : "+");
    lv_obj_set_style_text_color(marker, lv_color_hex(kAccent), LV_PART_MAIN);

    Inside in(t);
    TagRef sum;
    const bool hasSummary = findChild(t, "summary", sum);
    if (hasSummary) {
        Inside inSummary(sum);
        Seg seg(title, ctx, &sum, TextLook{});
        walk(sum.cstart, sum.cend, seg, Style{}, true);
        seg.flush();
    } else {
        newTextLabel(title, "Details");
    }
    summaryMarker(marker, lv_obj_get_child(title, 0), rtl);

    lv_obj_t* body = newBox(d);  // no indent, as in a browser
    if (!open) lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    Seg seg(body, ctx);
    if (hasSummary) {
        walk(t.cstart, sum.src, seg, Style{}, false);
        walk(sum.next, t.cend, seg, Style{}, false);
    } else {
        walk(t.cstart, t.cend, seg, Style{}, false);
    }
    seg.flush();
    collapse(title);
    collapse(body);
    collapse(d);

    auto* fold = new Fold{body, marker};
    lv_obj_add_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(head, onFoldClick, LV_EVENT_CLICKED, fold);
    lv_obj_add_event_cb(head, onFoldDelete, LV_EVENT_DELETE, fold);
}

void onDialogClose(lv_event_t* e) {
    if (UI::Dom::defaultPrevented(e)) return;  // a listener keeps the dialog open
    lv_obj_add_flag(static_cast<lv_obj_t*>(lv_event_get_user_data(e)), LV_OBJ_FLAG_HIDDEN);
}

void closeButtons(lv_obj_t* o, lv_obj_t* back) {  // <form method="dialog">: any button closes
#if !defined(LVGL_MOCK_ENABLED)
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) {
        lv_obj_t* c = lv_obj_get_child(o, i);
        if (lv_obj_check_type(c, &lv_button_class)) lv_obj_add_event_cb(c, onDialogClose, LV_EVENT_CLICKED, back);
        closeButtons(c, back);
    }
#else
    (void)o;
    (void)back;
#endif
}

void dialogElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    lv_obj_t* host = hostOf(box);
    lv_obj_t* back = plainObj(host);
    lv_obj_set_size(back, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(back, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_flex_flow(back, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);  // modal: taps stay in the dialog
    lv_obj_t* win = newBox(back);
    lv_obj_set_width(win, lv_pct(80));
    lv_obj_set_style_bg_color(win, lv_color_hex(kPanel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(win, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 16, LV_PART_MAIN);
    lv_obj_set_style_text_font(win, Font::get(UI::Font::SMALL), LV_PART_MAIN);
    lv_obj_set_style_text_color(win, lv_color_hex(kText), LV_PART_MAIN);
    renderInto(t, win, ctx);
    const P::String body(t.cstart, t.cend - t.cstart);
    if (body.find("method=\"dialog\"") != P::String::npos) closeButtons(win, back);
    if (!has(t, "open")) lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
    applyGlobal(t, back);
    setContentBox(back, win);
    lv_obj_move_foreground(back);
}

// ---- Embedded content: fallback or placeholder ----

void placeholder(const TagRef& t, lv_obj_t* box) {
    lv_obj_t* p = plainObj(box);
    P::String w = attr(t, "width");
    lv_obj_set_width(p, w.empty() ? lv_pct(100) : parse_size(w.c_str()));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(p, lv_color_hex(kPanel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_hex(kRule), LV_PART_MAIN);
    lv_obj_set_style_radius(p, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(p, 8, LV_PART_MAIN);
    P::String h = attr(t, "height");
    if (!h.empty()) lv_obj_set_height(p, parse_size(h.c_str()));
    P::String text = P::String("[") + t.name.c_str() + "]";
    P::String title = attr(t, "title");
    if (title.empty()) title = attr(t, "alt");
    if (!title.empty()) text += " " + title;
    lv_obj_t* l = newTextLabel(p, text.c_str());
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);  // the box keeps the author's size
    lv_obj_set_style_text_color(l, lv_color_hex(kDim), LV_PART_MAIN);
    applyGlobal(t, p);
}

void fallbackElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    if (!is(t.name, "svg") && !is(t.name, "math")) {
        // fallback content = what a browser without the feature shows (sources/tracks excluded)
        Seg probe(nullptr, ctx);
        {
            UI::Dom::Pause dry;  // its elements are recorded when it is built, below
            walk(t.cstart, t.cend, probe, Style{}, true);
        }
        bool text = false;
        for (const auto& it : probe.items()) {
            for (char c : it.text) text = text || !isspace(static_cast<unsigned char>(c));
        }
        if (text) {
            lv_obj_t* b = newBox(box);
            applyGlobal(t, b);
            renderInto(t, b, ctx);
            return;
        }
    }
    placeholder(t, box);
}

// ---- Table cells TelaOS does not know ----

// Whether a rule of the page or style="" gives t a font weight
bool cssSetsWeight(const TagRef& t) {
    UI::CssElement el;
    el.tag = t.name;
    el.attrs = rawAttrs(t);
    UI::Css::findAttr(el.attrs, "id", el.id);
    UI::Css::findAttr(el.attrs, "class", el.classes);
    for (const UI::CssDecl& d : UI::Css::instance().cascade(el, attr(t, "style"))) {
        if (d.name == "font-weight" || d.name == "font") return true;
    }
    return false;
}

void thElement(const TagRef& t, lv_obj_t* parent) {
    // same as TelaOS <td>, header look: bold (unless the page gives a weight) and centered
    P::String s = "<td";
    s.append(t.astart, t.aend - t.astart);
    s += ">";
    s.append(t.cstart, t.cend - t.cstart);
    s += "</td>";
    {
        UI::Dom::As as(t.name, rawAttrs(t));
        parse_children(s.c_str(), static_cast<int>(s.size()), parent);
    }
    if (lv_obj_t* td = lv_obj_get_child(parent, -1)) {
        if (!cssSetsWeight(t)) {
            const lv_font_t* f = lv_obj_get_style_text_font(td, LV_PART_MAIN);
            lv_obj_set_style_text_font(td, Font::get(UI::Font::sizeOf(f), true, UI::Font::isItalic(f)), LV_PART_MAIN);
        }
        lv_obj_set_style_text_align(td, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

// ============ Dispatch ============

void renderAtom(const TagRef& t, lv_obj_t* parent) {
    const char* n = t.name.c_str();
    BrowserSized sized;
    if (is(n, "img")) imgElement(t, parent);
    else if (is(n, "input") && isHtmlInput(t.astart, t.aend)) inputElement(t, parent);
    else if (is(n, "textarea")) textareaElement(t, parent);
    else if (is(n, "progress")) barElement(t, parent, false);
    else if (is(n, "meter")) barElement(t, parent, true);
    else delegate(t, parent);
}

void renderBlock(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const char* n = t.name.c_str();
    const TagInfo* ti = info(n);
    if (!ti) {  // TelaOS tag inside HTML content
        delegate(t, box);
        return;
    }
    switch (ti->kind) {
        case Kind::Block: blockElement(t, box, ctx); return;
        case Kind::Text: textElement(t, box, ctx); return;
        case Kind::List: listElement(t, box, ctx); return;
        case Kind::Item: listItem(t, box, ctx, "\xE2\x80\xA2"); return;
        case Kind::Fallback: fallbackElement(t, box, ctx); return;
        case Kind::Hidden:
            if (is(n, "link"))
                linkTag(t);
            return;
        case Kind::Inline: {
            Seg seg(box, ctx);
            inlineTag(t, ti, seg, Style{});
            seg.flush();
            return;
        }
        case Kind::Special: break;
    }
    if (is(n, "hr")) hrElement(t, box);
    else if (is(n, "img") || is(n, "input") || is(n, "textarea") || is(n, "progress") || is(n, "meter")) {
        Seg seg(box, ctx);
        seg.atom(t);
        seg.flush();
    } else if (is(n, "pre") || is(n, "xmp") || is(n, "listing") || is(n, "plaintext")) preElement(t, box, ctx);
    else if (is(n, "details")) detailsElement(t, box, ctx);
    else if (is(n, "dialog")) dialogElement(t, box, ctx);
    else if (is(n, "marquee")) marqueeElement(t, box);
    else if (is(n, "picture")) {
        TagRef img;
        if (findChild(t, "img", img)) imgElement(img, box);
    } else if (is(n, "html")) {
        applyDir(box, attr(t, "dir").c_str());  // <html dir="rtl">: the whole document
        renderInto(t, box, ctx);
    }
    else if (is(n, "th")) textElement(t, box, ctx);  // outside a table row: plain text
    else if (is(n, "thead") || is(n, "tbody") || is(n, "tfoot")) {
        parse_children(t.cstart, static_cast<int>(t.cend - t.cstart), box);
    } else if (is(n, "style")) parseStyleTag(t);
    else if (is(n, "br")) {
        lv_obj_t* sp = plainObj(box);
        lv_obj_set_size(sp, lv_pct(100), 8);
    }
}

}  // namespace

// ============ Public API ============

bool handles(const char* tag) { return tag && info(tag) != nullptr; }

bool isVoid(const char* tag) { return tag && isVoidName(tag); }

bool isNestable(const char* tag) { return handles(tag) && !isVoidName(tag) && !isRawText(tag); }

bool isHtmlInput(const char* astart, const char* aend) { return !getAttr(astart, aend, "type").empty(); }

void setTextColor(lv_obj_t* host, uint32_t rgb, uint8_t opa) {
    const auto it = s_rootOf.find(host);
    if (it == s_rootOf.end())
        return;
    lv_obj_set_style_text_color(it->second, lv_color_hex(rgb), LV_PART_MAIN);
    if (opa < LV_OPA_COVER)
        lv_obj_set_style_text_opa(it->second, opa, LV_PART_MAIN);
}

void linkStylesheet(std::string_view rel, std::string_view href, std::string_view media, bool disabled) {
    if (disabled || href.empty() || !linksStylesheet(rel))
        return;
    if (!media.empty() && !UI::cssMediaMatches(media)) {  // another screen's
        LOG_D(Log::UI, "<link>: %.*s is for media %.*s", static_cast<int>(href.size()), href.data(),
              static_cast<int>(media.size()), media.data());
        return;
    }
    // A browser would fetch it; the watch reads only its own files
    const bool remote = href.find("://") != std::string_view::npos || href.substr(0, 2) == "//";
    if (remote) {
        LOG_W(Log::UI, "<link>: %.*s is not a file of the watch, left out", static_cast<int>(href.size()),
              href.data());
        return;
    }
    const P::String path = resolve_resource_path(P::String(href));
    if (!LittleFS.exists(path.c_str())) {  // a browser leaves out a stylesheet that does not load
        LOG_W(Log::UI, "<link>: no file %s, left out", path.c_str());
        return;
    }
    File f = LittleFS.open(path.c_str(), "r");
    P::String css(f.size(), '\0');
    css.resize(f.readBytes(&css[0], css.size()));
    f.close();
    UI::Css::instance().parse(css);
    LOG_I(Log::UI, "<link>: %s, %d bytes", path.c_str(), static_cast<int>(css.size()));
}

void create(const char* tag, const char* astart, const char* aend, const char* content, lv_obj_t* parent) {
    TagRef t = refFrom(tag, astart, aend, content);
    // Table context: these live inside TelaOS table/tr containers, not in a flow box
    if (is(tag, "thead") || is(tag, "tbody") || is(tag, "tfoot")) {
        parse_children(t.cstart, static_cast<int>(t.cend - t.cstart), parent);
        return;
    }
    if (is(tag, "th") && !s_flow.count(parent)) {  // in a TelaOS <tr>
        thElement(t, parent);
        return;
    }
    if (is(tag, "caption")) {
        textElement(t, parent, Ctx{});
        return;
    }
    if (is(tag, "link")) {
        linkTag(t);
        return;
    }
    const TagInfo* ti = info(tag);
    if (!ti || ti->kind == Kind::Hidden) return;
    if (is(tag, "style")) {
        parseStyleTag(t);
        return;
    }
    lv_obj_t* box = flowParent(parent);
    Seg seg(box, Ctx{});
    handleTag(t, seg, Style{}, false);
    seg.flush();
    collapseOut(box);  // the page's elements come one by one; or a script's, into a block
    LOG_D(Log::UI, "html: <%s>", tag);
}

namespace {

// Inline-level start tag at p: text formatting, <br>, HTML form controls and images.
// TelaOS widgets (label, button, input without type=) are not: they keep their own placement.
bool inlineLevelAt(const char* p, const char* end, TagRef& t) {
    TagName name;
    if (!peekName(p, end, name)) return false;
    const TagInfo* ti = info(name);
    const bool unknown = !ti && !isTelaTag(name);  // <my-widget>, <blink>: inline, content shown
    const bool candidate = (ti && ti->kind == Kind::Inline) || unknown || is(name, "br") || is(name, "wbr") ||
                           is(name, "img") || is(name, "textarea") || is(name, "progress") ||
                           is(name, "meter") || is(name, "input");
    if (!candidate || !readTag(p, end, t)) return false;
    return !is(name, "input") || isHtmlInput(t.astart, t.aend);
}

// End of the run of text and inline-level elements starting at p; any = it has content
const char* runEnd(const char* p, const char* end, bool& any) {
    any = false;
    while (p < end) {
        if (*p != '<') {
            if (!isspace(static_cast<unsigned char>(*p))) any = true;
            ++p;
            continue;
        }
        if (commentAt(p, end)) {
            p = skipComment(p, end);
            continue;
        }
        if (p + 1 < end && (p[1] == '/' || p[1] == '!' || p[1] == '?')) return p;
        if (p + 1 >= end || !isalpha(static_cast<unsigned char>(p[1]))) {  // a lone '<' is text
            any = true;
            ++p;
            continue;
        }
        TagRef t;
        if (!inlineLevelAt(p, end, t)) return p;
        any = true;
        p = t.next;
    }
    return end;
}

}  // namespace

bool hasFlowContent(const char* html, size_t len) {
    const char* p = html;
    const char* end = html + len;
    while (p < end) {
        if (*p == '<') {
            if (commentAt(p, end)) {
                p = skipComment(p, end);
                continue;
            }
            TagRef t;
            TagName name;
            if (peekName(p, end, name) && readTag(p, end, t)) {
                const TagInfo* ti = info(t.name);
                if ((ti && ti->kind == Kind::Inline) || is(t.name, "br")) return true;  // <b>, <a>, <br>
                p = t.next;
                continue;
            }
            while (p < end && *p != '>') ++p;
            if (p < end) ++p;
            continue;
        }
        if (!isspace(static_cast<unsigned char>(*p))) return true;
        ++p;
    }
    return false;
}

bool hasOptionalEnd(const char* tag) { return tag && optionalEnd(tag); }

const char* impliedEnd(const char* tag, const char* content, const char* end) {
    return impliedClose(content, end, tag);
}

bool hasHtml(const char* html, size_t len) {
    const char* p = html;
    const char* end = html + len;
    while (p < end) {
        if (*p != '<') {
            if (!isspace(static_cast<unsigned char>(*p))) return true;  // direct text
            ++p;
            continue;
        }
        if (commentAt(p, end)) {
            p = skipComment(p, end);
            continue;
        }
        TagName name;
        TagRef t;
        if (!peekName(p, end, name)) {  // end tag, doctype, lone '<'
            while (p < end && *p != '>') ++p;
            if (p < end) ++p;
            continue;
        }
        // a standard HTML element that renders; table parts (<th>, <thead>) stay inside the TelaOS
        // table, <style> and hidden elements render nothing
        const TagInfo* ti = info(name);
        const bool tablePart = is(name, "caption") || is(name, "thead") || is(name, "tbody") ||
                               is(name, "tfoot") || is(name, "th");
        if (ti && ti->kind != Kind::Hidden && !tablePart && !is(name, "style") && !is(name, "input")) return true;
        if (!readTag(p, end, t)) return true;  // a lone '<' is text
        if (is(name, "input") && isHtmlInput(t.astart, t.aend)) return true;
        p = t.next;
    }
    return false;
}

bool isPositioned(const char* tag, const char* astart, const char* aend) {
    if (!getAttr(astart, aend, "x").empty() || !getAttr(astart, aend, "y").empty()) return true;
    // a container's align= is flex alignment of its children, not its own position
    const bool container = is(tag, "div") || is(tag, "table") || is(tag, "tr") || is(tag, "td");
    return !container && (!getAttr(astart, aend, "align").empty() || !getAttr(astart, aend, "valign").empty());
}

lv_obj_t* flowBox(lv_obj_t* parent) { return flowParent(parent); }

bool inFlow(lv_obj_t* box) { return s_flow.count(box) != 0; }

bool browserSized() { return s_browserSized > 0; }

bool setText(lv_obj_t* obj, const char* text) {
    if (!obj) return false;
    const char* s = text ? text : "";
    lv_obj_t* const owner = obj;
    auto c = s_contentOf.find(obj);
    if (c != s_contentOf.end()) obj = c->second;
#if LV_USE_SPAN
    if (lv_obj_check_type(obj, &lv_spangroup_class)) {  // formatted paragraph: one plain span
        UI::Dom::contentReplaced(owner);  // its <b>, <a>... are gone
        while (lv_spangroup_get_span_count(obj) > 0) lv_spangroup_delete_span(obj, lv_spangroup_get_child(obj, 0));
        lv_span_set_text(lv_spangroup_new_span(obj), s);
        lv_spangroup_refr_mode(obj);
        return true;
    }
#endif
    if (s_toggles.count(obj)) {
        lv_checkbox_set_text(obj, s);
        return true;
    }
    if (s_flow.count(obj)) {  // a block: the text replaces its content, like DOM textContent
        UI::Dom::contentReplaced(owner);
        g_core.app().forgetInside(obj);  // {var} and ui.* no longer reach what is deleted here
        lv_obj_clean(obj);
        newTextLabel(obj, s);
        return true;
    }
    return false;
}

lv_obj_t* contentOf(lv_obj_t* obj) {
    const auto c = s_contentOf.find(obj);
    return c != s_contentOf.end() ? c->second : obj;
}

void marginsAside(lv_obj_t* obj) {
    lv_obj_t* const box = obj ? lv_obj_get_parent(obj) : nullptr;
    if (!box || !s_flow.count(box)) return;
    for (int side : {kAbove, kBelow}) lv_obj_remove_local_style_prop(obj, kMarginProp[side], LV_PART_MAIN);
}

void restyled(lv_obj_t* obj, const char* tag) {
    lv_obj_t* const box = obj ? lv_obj_get_parent(obj) : nullptr;
    if (!box || !s_flow.count(box)) return;
    const auto r = s_margins[box].kids.emplace(obj, Margin{});
    Margin& m = r.first->second;
    if (r.second) {  // it came here from another box: a browser's margins for its tag
        m.ua[kAbove] = m.ua[kBelow] = uaMargin(tag ? tag : "", inList(box));
        m.keep = !info(tag ? tag : "") && !s_flow.count(obj);
    }
    ownMargins(obj, m);
    lv_obj_mark_layout_as_dirty(box);  // flex lays the box out again, and its margins are worked out
}

P::String getText(lv_obj_t* obj) {
    if (!obj) return "";
    auto c = s_contentOf.find(obj);
    if (c != s_contentOf.end()) obj = c->second;
    if (lv_obj_check_type(obj, &lv_label_class)) return lv_label_get_text(obj);
#if LV_USE_SPAN
    if (lv_obj_check_type(obj, &lv_spangroup_class)) {
        P::String s;
        for (uint32_t i = 0; i < lv_spangroup_get_span_count(obj); ++i) {
            const lv_span_t* span = lv_spangroup_get_child(obj, static_cast<int32_t>(i));
            if (span && span->txt) s += span->txt;  // LVGL 9.2 has no getter for it
        }
        return s;
    }
#endif
    if (s_toggles.count(obj) || lv_obj_check_type(obj, &lv_textarea_class)) return "";  // its label is not text of the page
    P::String s;
    const uint32_t n = static_cast<uint32_t>(lv_obj_get_child_count(obj));
    for (uint32_t i = 0; i < n; ++i) s += getText(lv_obj_get_child(obj, static_cast<int32_t>(i)));
    return s;
}

bool setChecked(lv_obj_t* obj, bool on) {
    const auto it = s_toggles.find(obj);
    if (it == s_toggles.end() && !lv_obj_check_type(obj, &lv_switch_class)) return false;
    if (on) lv_obj_add_state(obj, LV_STATE_CHECKED);
    else lv_obj_clear_state(obj, LV_STATE_CHECKED);
    if (on && it != s_toggles.end() && it->second.radio) {
        for (auto& [o, other] : s_toggles) {
            if (o != obj && other.radio && other.name == it->second.name) lv_obj_clear_state(o, LV_STATE_CHECKED);
        }
    }
    return true;
}

void labelFor(lv_obj_t* label, const char* id) {
    if (!label || !id || !*id) return;
    s_labelFor[label] = id;
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, onLabelClick, LV_EVENT_CLICKED, label);
    lv_obj_add_event_cb(label, onLabelDelete, LV_EVENT_DELETE, label);
}

void labelControl(lv_obj_t* label, lv_obj_t* control) {
    if (!label || !control) return;
    s_labelControl[label] = control;
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, onLabelClick, LV_EVENT_CLICKED, label);
    lv_obj_add_event_cb(label, onLabelDelete, LV_EVENT_DELETE, label);
}

lv_obj_t* firstControl(lv_obj_t* o) { return o ? findControl(o) : nullptr; }

bool hasMarkup(const char* s, const char* e, bool controls) {
    if (!s || e <= s) return false;
    UI::TagReader reader(std::string_view(s, static_cast<size_t>(e - s)));
    UI::StartTag tag;
    bool any = false;
    for (UI::TagReader::Step step; (step = reader.next(tag)) != UI::TagReader::Step::End;) {
        if (step != UI::TagReader::Step::Tag) continue;
        const char* n = tag.name.c_str();
        const TagInfo* ti = info(n);
        const bool phrasing = (ti && ti->kind == Kind::Inline) || is(n, "br") || is(n, "wbr") || is(n, "img");
        const bool control = controls && (is(n, "input") || is(n, "textarea") || is(n, "meter") || is(n, "progress"));
        if (!phrasing && !control) return false;
        any = true;
    }
    return any;
}

lv_obj_t* richText(lv_obj_t* parent, const char* s, const char* e, bool fit) {
    if (!parent || !s || e <= s) return nullptr;
    TagRef owner;  // no attributes: no CSS or id of its own (the label or button has those); it keeps {var} live
    owner.name = "#text";
    owner.astart = owner.aend = "";
    owner.cstart = s;
    owner.cend = e;
    const uint32_t before = lv_obj_get_child_count(parent);
    Seg seg(parent, Ctx{}, &owner, TextLook{});
    walk(s, e, seg, Style{}, true);
    seg.flush();
    if (lv_obj_get_child_count(parent) == before) return nullptr;  // nothing to show
    lv_obj_t* o = lv_obj_get_child(parent, -1);
    if (fit) {  // a button's text: one line, as wide as it is
        lv_obj_set_width(o, LV_SIZE_CONTENT);
#if LV_USE_SPAN
        if (lv_obj_check_type(o, &lv_spangroup_class)) lv_spangroup_set_mode(o, LV_SPAN_MODE_EXPAND);
#endif
    }
    return o;
}

void reset() {
    s_labelFor.clear();
    s_labelControl.clear();
    s_flow.clear();
    s_rootOf.clear();
    s_contentOf.clear();
    s_margins.clear();
    s_toggles.clear();
}

const char* inlineRun(const char* p, const char* end, lv_obj_t* parent) {
    bool any = false;
    const char* e = runEnd(p, end, any);
    if (!any) return p;
    LOG_D(Log::UI, "html: inline run, %d bytes", static_cast<int>(e - p));
    Seg seg(flowParent(parent), Ctx{});
    walk(p, e, seg, Style{}, false);
    seg.flush();
    return e;
}

// dir="rtl", as far as the watch's fonts need it (they have no right-to-left letters): lines sit at
// the right; table columns, list markers and words in a row start from the right, as LVGL flex
// follows base_dir; base_dir and text_align are inherited, as dir is. dir="ltr" undoes it inside;
// dir="auto" is ltr for these letters.
void applyDir(lv_obj_t* obj, const char* dir) {
    if (!dir || !dir[0]) return;
    const P::String d = lower(P::String(dir));
    if (d != "rtl" && d != "ltr") return;
    const bool rtl = d == "rtl";
    lv_obj_set_style_base_dir(obj, rtl ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR, LV_PART_MAIN);
#if !defined(LVGL_MOCK_ENABLED)
    lv_style_value_t v;  // an alignment of its own (a caption is centred) stays
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_TEXT_ALIGN, &v, LV_PART_MAIN) == LV_STYLE_RES_FOUND) return;
#endif
    lv_obj_set_style_text_align(obj, rtl ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
}

void makeFlow(lv_obj_t* container) {
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, 0, LV_PART_MAIN);  // its blocks' margins are between them
}

void renderContent(const char* html, size_t len, lv_obj_t* container) {
    textDefaults(container);
    if (!s_flow.count(container)) track(container);
    Seg seg(container, Ctx{});
    walk(html, html + len, seg, Style{}, false);
    seg.flush();
    collapseOut(container);
}

}  // namespace HtmlFlow
