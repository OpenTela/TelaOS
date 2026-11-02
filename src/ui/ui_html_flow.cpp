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
 * Visual language = widget_markdown.h: h1 48px, h2 32px, h3+ 16px; strong → accent,
 * em → dim, code → green. No extra fonts: the watch keeps its flash.
 */
#include "lvgl.h"
#include "ui/ui_html_flow.h"

#include "core/core.h"
#include "ui/css_parser.h"
#include "ui/ui_coords.h"
#include "ui/ui_html_internal.h"
#include "ui/xml_utils.h"
#include "utils/font.h"
#include "utils/log_config.h"
#include "widgets/widget_common.h"

#include <lvgl.h>

#include <strings.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

using namespace UI::XmlUtils;

static const char* TAG = "html_flow";

namespace HtmlFlow {
namespace {

// ============ Visual language (palette of widget_markdown.h) ============

constexpr uint32_t kText = 0xFFFFFF;
constexpr uint32_t kH2 = 0xDDDDDD;
constexpr uint32_t kAccent = 0x4FC3F7;   // strong, b, links, dt
constexpr uint32_t kDim = 0xAAAAAA;      // em, i, small, markers, captions
constexpr uint32_t kCode = 0x81C784;     // code, kbd, samp
constexpr uint32_t kMark = 0xFFD54F;     // mark, meter outside its optimum
constexpr uint32_t kRule = 0x555555;     // hr, borders, bar track
constexpr uint32_t kPanel = 0x222222;    // dialog, placeholders
constexpr uint32_t kInherit = 0xFF000000;

constexpr int kGap = 8;        // between blocks
constexpr int kGapList = 4;    // between list items
constexpr int kIndent = 20;    // dd, blockquote, nested lists
constexpr int kBodyPad = 8;    // HTML body margin

constexpr uint8_t kUnderline = 1;
constexpr uint8_t kStrike = 2;

// ============ Tag table ============

enum class Kind : uint8_t { Inline, Block, Text, List, Item, Hidden, Fallback, Special };

enum Look : uint8_t {
    L_PLAIN, L_STRONG, L_EM, L_CODE, L_LINK, L_MARK, L_DIM, L_UNDER, L_STRIKE, L_QUOTE, L_BIG, L_FONT,
    L_RT, L_SKIP
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
    {"strike", Kind::Inline, L_STRIKE}, {"strong", Kind::Inline, L_STRONG}, {"sub", Kind::Inline, L_DIM},
    {"sup", Kind::Inline, L_DIM},       {"time", Kind::Inline, L_PLAIN},   {"tt", Kind::Inline, L_CODE},
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

const TagInfo* info(const char* name) {
    for (const auto& t : kTags) {
        if (strcmp(t.name, name) == 0) return &t;
    }
    return nullptr;
}

bool is(const char* a, const char* b) { return strcmp(a, b) == 0; }

bool isTelaTag(const char* n) {
    static const char* const kTela[] = {"label", "button", "switch", "slider", "input", "canvas", "image",
                                        "markdown", "qr", "tabs", "tab", "select", "list", "item", "div",
                                        "table", "tr", "td", "page", "group"};
    for (const char* t : kTela) {
        if (strcmp(t, n) == 0) return true;
    }
    return false;
}

bool isVoidName(const char* n) {
    static const char* const kVoid[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
                                        "meta", "param", "source", "track", "wbr", "frame"};
    for (const char* t : kVoid) {
        if (strcmp(t, n) == 0) return true;
    }
    return false;
}

// Content is raw text: no tags inside
bool isRawText(const char* n) {
    return is(n, "script") || is(n, "style") || is(n, "textarea") || is(n, "title") || is(n, "xmp") ||
           is(n, "plaintext");
}

// Inline-level replaced elements and form controls: sit inside a line of text
bool isAtomName(const char* n) {
    static const char* const kAtom[] = {"img", "input", "button", "select", "textarea", "label", "progress",
                                        "meter", "image", "switch", "slider", "canvas"};
    for (const char* t : kAtom) {
        if (strcmp(t, n) == 0) return true;
    }
    return false;
}

// ============ Attributes ============

// Boolean attributes (`open`, `checked`, `hidden`) have no value: getAttr() cannot tell
// "absent" from "empty", so scan the attribute names.
bool hasAttr(const char* s, const char* e, const char* name) {
    const size_t n = strlen(name);
    const char* p = s;
    while (p < e) {
        while (p < e && (isspace(static_cast<unsigned char>(*p)) || *p == '/')) ++p;
        const char* a = p;
        while (p < e && *p != '=' && !isspace(static_cast<unsigned char>(*p)) && *p != '/') ++p;
        if (static_cast<size_t>(p - a) == n && strncasecmp(a, name, n) == 0) return true;
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

struct TagRef {
    char name[24] = {0};
    const char* src = nullptr;     // '<' (null when called from parse_children)
    const char* astart = nullptr;  // attributes
    const char* aend = nullptr;    // at '>'
    const char* cstart = nullptr;  // content
    const char* cend = nullptr;
    const char* next = nullptr;    // after the element
};

const char* findRawClose(const char* p, const char* end, const char* name) {
    char close[32];
    snprintf(close, sizeof(close), "</%s", name);
    const size_t n = strlen(close);
    for (; p + n <= end; ++p) {
        if (strncasecmp(p, close, n) == 0) return p;
    }
    return nullptr;
}

// Name of the start tag at p ('<'), lower-case; false if p is not a start tag
bool peekName(const char* p, const char* end, char* name, size_t cap) {
    if (p + 1 >= end || *p != '<' || !isalpha(static_cast<unsigned char>(p[1]))) return false;
    size_t n = 0;
    for (const char* q = p + 1; q < end && n < cap - 1; ++q) {
        if (isspace(static_cast<unsigned char>(*q)) || *q == '>' || *q == '/') break;
        name[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*q)));
    }
    name[n] = 0;
    return n > 0;
}

const char* skipComment(const char* p, const char* end) {  // p at "<!--"
    const char* c = strstr(p + 4, "-->");
    return (c && c < end) ? c + 3 : end;
}

// ---- Omitted end tags: <li>one<li>two, <p>one<p>two, <dt>term<dd>definition ----

bool closesP(const char* n) {  // a start tag that ends an open <p>
    static const char* const kClose[] = {"address", "article", "aside", "blockquote", "details", "dialog", "div",
                                         "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
                                         "h3", "h4", "h5", "h6", "header", "hgroup", "hr", "main", "menu", "nav",
                                         "ol", "p", "pre", "search", "section", "table", "ul"};
    for (const char* t : kClose) {
        if (strcmp(t, n) == 0) return true;
    }
    return false;
}

bool optionalEnd(const char* n) {
    static const char* const kOpt[] = {"li", "dt", "dd", "p", "rt", "rp", "option", "optgroup", "thead", "tbody",
                                       "tfoot", "tr", "td", "th"};
    for (const char* t : kOpt) {
        if (strcmp(t, n) == 0) return true;
    }
    return false;
}

// Does start tag `next` end the open element `open` (same level)?
bool impliedEnd(const char* open, const char* next) {
    auto any = [next](std::initializer_list<const char*> names) {
        for (const char* n : names) {
            if (strcmp(n, next) == 0) return true;
        }
        return false;
    };
    if (strcmp(open, "li") == 0) return any({"li"});
    if (strcmp(open, "dt") == 0 || strcmp(open, "dd") == 0) return any({"dt", "dd"});
    if (strcmp(open, "rt") == 0 || strcmp(open, "rp") == 0) return any({"rt", "rp"});
    if (strcmp(open, "option") == 0) return any({"option", "optgroup"});
    if (strcmp(open, "optgroup") == 0) return any({"optgroup"});
    if (strcmp(open, "thead") == 0 || strcmp(open, "tbody") == 0) return any({"tbody", "tfoot"});
    if (strcmp(open, "tr") == 0) return any({"tr", "thead", "tbody", "tfoot"});
    if (strcmp(open, "td") == 0 || strcmp(open, "th") == 0) return any({"td", "th", "tr", "thead", "tbody", "tfoot"});
    if (strcmp(open, "p") == 0) return closesP(next);
    return false;
}

bool readTag(const char* p, const char* end, TagRef& t);

// Where an element without its end tag ends: at a sibling that implies it, or at the parent's end
const char* impliedClose(const char* p, const char* end, const char* name) {
    while (p < end) {
        p = static_cast<const char*>(memchr(p, '<', end - p));
        if (!p) return end;
        if (end - p >= 4 && strncmp(p, "<!--", 4) == 0) {
            p = skipComment(p, end);
            continue;
        }
        if (p + 1 < end && p[1] == '/') return p;  // the parent's end tag
        char next[24];
        TagRef c;
        if (!peekName(p, end, next, sizeof(next))) {
            ++p;
            continue;
        }
        if (impliedEnd(name, next)) return p;
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
    size_t n = 0;
    while (q < end && !isspace(static_cast<unsigned char>(*q)) && *q != '>' && *q != '/' && n < sizeof(t.name) - 1) {
        t.name[n++] = static_cast<char>(tolower(static_cast<unsigned char>(*q++)));
    }
    if (!n || !isalpha(static_cast<unsigned char>(t.name[0]))) return false;
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
    const char* close = isRawText(t.name) ? findRawClose(q + 1, end, t.name) : findTagClose(q + 1, t.name);
    if (optionalEnd(t.name)) {
        // <li>, <p>, <dd> may omit the end tag: then the next sibling that implies it ends them
        // (<li>a<li>b, <p>a<div>), not a later </li> of another list
        const char* implied = impliedClose(q + 1, end, t.name);
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
    snprintf(t.name, sizeof(t.name), "%s", tag);
    t.astart = astart;
    t.aend = aend;
    t.cstart = content ? content : "";
    t.cend = t.cstart + strlen(t.cstart);
    t.next = t.cend;
    return t;
}

P::String attr(const TagRef& t, const char* name) { return getAttr(t.astart, t.aend, name); }
bool has(const TagRef& t, const char* name) { return hasAttr(t.astart, t.aend, name); }

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
    const size_t n = semi - p - 1;
    for (const auto& en : kEntities) {
        if (strlen(en.name) == n && strncmp(p + 1, en.name, n) == 0) {
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
    int font = 0;
    uint8_t decor = 0;
    P::String href;
    P::String onclick;
    bool same(const Style& o) const {
        return color == o.color && font == o.font && decor == o.decor && href == o.href && onclick == o.onclick;
    }
    bool plain() const { return color == kInherit && !font && !decor && href.empty() && onclick.empty(); }
    bool link() const { return !href.empty() || !onclick.empty(); }
};

struct TextLook {  // block text look
    int font = 0;
    uint32_t color = kInherit;
    int align = -1;
};

TextLook lookFor(const char* n) {
    TextLook l;
    if (is(n, "h1")) l.font = UI::Font::LARGE, l.color = kText;
    else if (is(n, "h2")) l.font = UI::Font::MEDIUM, l.color = kH2;
    else if (n[0] == 'h' && n[1] >= '3' && n[1] <= '6' && !n[2]) l.font = UI::Font::SMALL, l.color = kH2;
    else if (is(n, "dt")) l.color = kAccent;
    else if (is(n, "caption")) l.color = kDim, l.align = LV_TEXT_ALIGN_CENTER;
    else if (is(n, "figcaption") || is(n, "legend")) l.color = kDim;
    return l;
}

void applyLook(lv_obj_t* o, const TextLook& l) {
    if (l.font) lv_obj_set_style_text_font(o, Font::get(l.font), LV_PART_MAIN);
    if (l.color != kInherit) lv_obj_set_style_text_color(o, lv_color_hex(l.color), LV_PART_MAIN);
    if (l.align >= 0) lv_obj_set_style_text_align(o, static_cast<lv_text_align_t>(l.align), LV_PART_MAIN);
}

int fontForHtmlSize(const P::String& size) {  // <font size="1..7">
    if (size.empty()) return 0;
    int v = atoi(size.c_str());
    if (size[0] == '+') v = 3 + v;
    else if (size[0] == '-') v = 3 + v;  // atoi keeps the sign
    if (v <= 4) return UI::Font::SMALL;
    if (v == 5) return UI::Font::MEDIUM;
    if (v == 6) return UI::Font::LARGE;
    return UI::Font::XLARGE;
}

Style derive(const Style& base, const TagRef& t, uint8_t look) {
    Style s = base;
    switch (look) {
        case L_STRONG: s.color = kAccent; break;
        case L_EM: case L_DIM: case L_RT: s.color = kDim; break;
        case L_CODE: s.color = kCode; break;
        case L_MARK: s.color = kMark; break;
        case L_UNDER: s.decor |= kUnderline; break;
        case L_STRIKE: s.decor |= kStrike; break;
        case L_BIG: s.font = UI::Font::MEDIUM; break;
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
    // class / style="color:..." on inline elements: spans have no objects, take the color only
    P::String cls = attr(t, "class");
    if (!cls.empty()) {
        P::String first = cls.substr(0, cls.find(' '));
        if (UI::Css::instance().has(first)) {
            const P::String& col = UI::Css::instance().prop(first, "color");
            if (is_color(col.c_str())) s.color = parse_color(col.c_str());
        }
    }
    P::String st = attr(t, "style");
    size_t cp = st.find("color");
    if (cp != P::String::npos && (cp == 0 || st[cp - 1] == ';' || st[cp - 1] == ' ')) {
        size_t colon = st.find(':', cp);
        if (colon != P::String::npos) {
            size_t endv = st.find(';', colon);
            P::String v = UI::Css::trim(std::string_view(st.c_str() + colon + 1,
                                                         (endv == P::String::npos ? st.size() : endv) - colon - 1));
            if (is_color(v.c_str())) s.color = parse_color(v.c_str());
        }
    }
    return s;
}

// ============ Registry: containers built by this layer ============

std::unordered_set<lv_obj_t*> s_flow;                // every flow box
std::unordered_map<lv_obj_t*, lv_obj_t*> s_rootOf;   // page/TelaOS container → its implicit flow root
int s_autoId = 0;

void onFlowDelete(lv_event_t* e) {
    lv_obj_t* o = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    s_flow.erase(o);
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
}

// Element object → the box that holds its content (<li> row → body, <dialog> backdrop → window)
std::unordered_map<lv_obj_t*, lv_obj_t*> s_contentOf;

void onContentOwnerDelete(lv_event_t* e) { s_contentOf.erase(static_cast<lv_obj_t*>(lv_event_get_user_data(e))); }

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

lv_obj_t* newBox(lv_obj_t* parent, int gap = kGap) {
    lv_obj_t* b = plainObj(parent);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, gap, LV_PART_MAIN);
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

// HTML placed straight into a page or TelaOS container goes into its one implicit root box,
// which also takes the container's TelaOS widgets that have no x/y/align (see parse_children)
lv_obj_t* flowParent(lv_obj_t* parent) {
    if (s_flow.count(parent)) return parent;
    auto it = s_rootOf.find(parent);
    if (it != s_rootOf.end()) return it->second;
    fillWidth(parent);
    lv_obj_t* root = newBox(parent, kGap);
    lv_obj_set_style_pad_all(root, kBodyPad, LV_PART_MAIN);
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
    if (a->href.empty()) return;
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

// ============ Global attributes: id, class, style, hidden, visible, onclick ============

void inlineCss(lv_obj_t* o, const char* tag, const P::String& decls) {
    UI::Css css;
    P::String sheet = ".__inline{";
    sheet += decls;
    sheet += "}";
    css.parse(std::string_view(sheet.c_str(), sheet.size()));
    Widget w{o};
    css.applyMatching(w, tag, "", "__inline");
    P::String d = lower(decls);
    if (d.find("display:none") != P::String::npos || d.find("display: none") != P::String::npos) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

P::String applyGlobal(const TagRef& t, lv_obj_t* o, const char* tplText = nullptr) {
    P::String id = attr(t, "id");
    P::String cls = attr(t, "class");
    P::String style = attr(t, "style");
    P::String vis = attr(t, "visible");
    Widget{o}.applyCss(t.name, id.c_str(), cls.c_str());
    if (!style.empty()) inlineCss(o, t.name, style);
    if (has(t, "hidden")) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (!is(t.name, "a")) makeClickable(o, "", attr(t, "onclick"));

    const bool dynVis = vis.find('{') != P::String::npos;
    const bool dynText = tplText && strchr(tplText, '{');
    if (!id.empty() || dynVis || dynText) {
        if (id.empty()) {
            char buf[24];
            snprintf(buf, sizeof(buf), "_html_%d", s_autoId++);
            id = buf;
        }
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
        if (out.empty()) return;
        if (!items_.empty() && items_.back().type == Item::TEXT && items_.back().st.same(st)) {
            items_.back().text += out;
        } else {
            items_.push_back(Item{Item::TEXT, out, st, {}});
        }
    }

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
    bool atoms = false, formatted = false;
    int links = 0;
    const Style* lastLink = nullptr;
    for (const auto& it : items_) {
        if (it.type == Item::ATOM) atoms = true;
        if (it.type != Item::TEXT) continue;
        if (!it.st.plain()) formatted = true;
        if (it.st.link() && !(lastLink && lastLink->same(it.st))) ++links;
        lastLink = it.st.link() ? &it.st : nullptr;
    }
    if (atoms || links >= 2) asWords();
    else if (!formatted) asLabel();
    else asSpans();
    items_.clear();
    lastSpace_ = true;
}

// Plain text: one wrapped label; {var} stays live through the TelaOS binding mechanism
void Seg::asLabel() {
    P::String text = plainText();
    const bool dyn = strchr(text.c_str(), '{') != nullptr;
    P::String shown = dyn ? render_template(text.c_str()) : text;
    lv_obj_t* l = newTextLabel(box_, shown.c_str());
    applyLook(l, look_);
    if (owner_) {
        applyGlobal(*owner_, l, dyn ? text.c_str() : nullptr);
    } else if (dyn) {
        TagRef none;  // anonymous text run: registered only to keep its {var} live
        snprintf(none.name, sizeof(none.name), "#text");
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
void fillSpans(lv_obj_t* sg, const P::Array<Seg::Item>& items) {
    while (lv_spangroup_get_span_count(sg) > 0) lv_spangroup_delete_span(sg, lv_spangroup_get_child(sg, 0));
    for (const auto& it : items) {
        if (it.type == Seg::Item::ATOM || it.text.empty()) continue;
        lv_span_t* span = lv_spangroup_new_span(sg);
        lv_span_set_text(span, it.text.c_str());
        lv_style_t* s = lv_span_get_style(span);
        if (it.st.color != kInherit) lv_style_set_text_color(s, lv_color_hex(it.st.color));
        if (it.st.font) lv_style_set_text_font(s, Font::get(it.st.font));
        if (it.st.decor) {
            lv_style_set_text_decor(s, static_cast<lv_text_decor_t>(
                                           ((it.st.decor & kUnderline) ? LV_TEXT_DECOR_UNDERLINE : 0) |
                                           ((it.st.decor & kStrike) ? LV_TEXT_DECOR_STRIKETHROUGH : 0)));
        }
    }
    lv_spangroup_refr_mode(sg);
}
#endif

// Formatted text: spans in one spangroup (mock LVGL has no spans: plain label there)
void Seg::asSpans() {
#if LV_USE_SPAN
    lv_obj_t* sg = lv_spangroup_create(box_);
    lv_obj_set_width(sg, lv_pct(100));
    lv_spangroup_set_mode(sg, LV_SPAN_MODE_BREAK);
    // Live {var} inside formatted text: spans are rebuilt from the element source on change
    if (owner_ && memchr(owner_->cstart, '{', owner_->cend - owner_->cstart)) {
        const P::String tpl(owner_->cstart, owner_->cend - owner_->cstart);
        const Ctx ctx = ctx_;
        auto update = [sg, ctx](const P::String& rendered) {
            Seg tmp(nullptr, ctx);
            walk(rendered.c_str(), rendered.c_str() + rendered.size(), tmp, Style{}, true);
            fillSpans(sg, tmp.items());
        };
        update(render_template(tpl.c_str()));
        auto elem = P::create<UI::Element>();
        char buf[24];
        snprintf(buf, sizeof(buf), "_html_%d", s_autoId++);
        elem->id = buf;
        elem->w.handle = sg;
        elem->tpl = tpl;
        elem->updateFn = update;
        g_core.app().elements.push_back(std::move(elem));
    } else {
        fillSpans(sg, items_);
    }
    decorate(sg);
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

// Text with widgets or several links: a row that wraps word by word, like an HTML line box
void Seg::asWords() {
    lv_obj_t* row = plainObj(box_);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);
    decorate(row);
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
        P::String text = strchr(it.text.c_str(), '{') ? render_template(it.text.c_str()) : it.text;
        size_t i = 0;
        while (i < text.size()) {
            size_t sp = text.find(' ', i);
            size_t endw = (sp == P::String::npos) ? text.size() : sp + 1;  // keep the space
            P::String word = text.substr(i, endw - i);
            i = endw;
            lv_obj_t* w = lv_label_create(row);  // a lone " " too: the gap between two links
            lv_label_set_text(w, word.c_str());
            if (it.st.color != kInherit) lv_obj_set_style_text_color(w, lv_color_hex(it.st.color), LV_PART_MAIN);
            if (it.st.font) lv_obj_set_style_text_font(w, Font::get(it.st.font), LV_PART_MAIN);
#if !defined(LVGL_MOCK_ENABLED)
            if (it.st.decor) {
                lv_obj_set_style_text_decor(w, static_cast<lv_text_decor_t>(
                    ((it.st.decor & kUnderline) ? LV_TEXT_DECOR_UNDERLINE : 0) |
                    ((it.st.decor & kStrike) ? LV_TEXT_DECOR_STRIKETHROUGH : 0)), LV_PART_MAIN);
            }
#endif
            makeClickable(w, it.st.href, it.st.onclick);
        }
    }
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

void inlineTag(const TagRef& t, const TagInfo* ti, Seg& seg, const Style& st) {
    const uint8_t look = ti ? ti->look : L_PLAIN;
    if (look == L_SKIP) return;
    Style s = derive(st, t, look);
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

void handleTag(const TagRef& t, Seg& seg, const Style& st, bool inlineOnly) {
    const char* n = t.name;
    if (is(n, "br")) {
        seg.lineBreak();
        return;
    }
    if (is(n, "wbr")) return;
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
            lv_obj_t* b = newBox(seg.box(), kGap);
            Style s = derive(st, t, ti->look);
            makeClickable(b, s.href, s.onclick);
            applyGlobal(t, b);
            Seg inner(b, seg.ctx());
            walk(t.cstart, t.cend, inner, s, false);
            inner.flush();
            return;
        }
        inlineTag(t, ti, seg, st);
        return;
    }
    if (!ti && !isTelaTag(n)) {  // unknown element: its content shows, as in a browser
        walk(t.cstart, t.cend, seg, st, inlineOnly);
        return;
    }
    if (inlineOnly) {  // a block inside inline content: keep its text in the line
        walk(t.cstart, t.cend, seg, st, true);
        return;
    }
    seg.flush();
    renderBlock(t, seg.box(), seg.ctx());
}

void walk(const char* p, const char* end, Seg& seg, const Style& st, bool inlineOnly) {
    while (p < end) {
        if (*p != '<') {
            const char* s = p;
            while (p < end && *p != '<') ++p;
            seg.text(s, p, st);
            continue;
        }
        if (end - p >= 4 && strncmp(p, "<!--", 4) == 0) {
            const char* c = strstr(p + 4, "-->");
            p = (c && c < end) ? c + 3 : end;
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
    Seg seg(box, ctx);
    walk(t.cstart, t.cend, seg, Style{}, false);
    seg.flush();
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
    const TextLook look = lookFor(t.name);
    if (!hasBlockOrAtom(t.cstart, t.cend)) {
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
    lv_obj_t* b = newBox(box, kGap);
    applyLook(b, look);
    applyGlobal(t, b);
    renderInto(t, b, ctx);
}

void blockElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const char* n = t.name;
    if (is(n, "body")) {  // the flow root already is the body
        renderInto(t, box, ctx);
        return;
    }
    lv_obj_t* b = newBox(box, kGap);
    if (is(n, "blockquote")) {
        lv_obj_set_style_pad_left(b, kIndent - 8, LV_PART_MAIN);
        lv_obj_set_style_text_color(b, lv_color_hex(kH2), LV_PART_MAIN);
#if !defined(LVGL_MOCK_ENABLED)
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
#endif
        lv_obj_set_style_border_width(b, 3, LV_PART_MAIN);
        lv_obj_set_style_border_color(b, lv_color_hex(kRule), LV_PART_MAIN);
    } else if (is(n, "dd")) {
        lv_obj_set_style_pad_left(b, kIndent, LV_PART_MAIN);
    } else if (is(n, "fieldset")) {
        lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(b, lv_color_hex(kRule), LV_PART_MAIN);
        lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(b, 10, LV_PART_MAIN);
    } else if (is(n, "figure")) {
        lv_obj_set_style_pad_left(b, 16, LV_PART_MAIN);
        lv_obj_set_style_pad_right(b, 16, LV_PART_MAIN);
    } else if (is(n, "address")) {
        lv_obj_set_style_text_color(b, lv_color_hex(kDim), LV_PART_MAIN);
    } else if (is(n, "center")) {
        lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    applyGlobal(t, b);
    renderInto(t, b, ctx);
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
    char buf[16];
    if ((type == 'a' || type == 'A') && n > 0) {
        P::String s;
        for (int v = n; v > 0; v = (v - 1) / 26) s.insert(s.begin(), static_cast<char>((type == 'a' ? 'a' : 'A') + (v - 1) % 26));
        return s + ".";
    }
    if ((type == 'i' || type == 'I') && n > 0) return roman(n, type == 'I') + ".";
    snprintf(buf, sizeof(buf), "%d.", n);
    return buf;
}

void listItem(const TagRef& li, lv_obj_t* parent, const Ctx& ctx, const P::String& marker) {
    lv_obj_t* row = plainObj(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_t* m = lv_label_create(row);
    lv_label_set_text(m, marker.c_str());
    lv_obj_set_style_text_color(m, lv_color_hex(kDim), LV_PART_MAIN);
    lv_obj_t* body = newBox(row, kGapList);
    lv_obj_set_width(body, 0);
    lv_obj_set_flex_grow(body, 1);
    applyGlobal(li, row);
    setContentBox(row, body);
    Ctx inner = ctx;
    inner.listDepth = ctx.listDepth + 1;
    renderInto(li, body, inner);
}

void listElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    lv_obj_t* lb = newBox(box, kGapList);
    if (ctx.listDepth > 0) lv_obj_set_style_pad_left(lb, kIndent / 2, LV_PART_MAIN);
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
    for (const auto& c : items) {
        if (!is(c.name, "li")) {  // a list nested straight in a list, or other content
            renderBlock(c, lb, ctx);
            continue;
        }
        if (!attr(c, "value").empty()) n = atoi(attr(c, "value").c_str());
        const P::String marker = ordered ? olMarker(n, ty) : P::String(kBullets[ctx.listDepth % 3]);
        listItem(c, lb, ctx, marker);
        n += reversed ? -1 : 1;
    }
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
    if (!id.empty()) a += " id=\"" + id + "\"";
    create_image(a.c_str(), a.c_str() + a.size(), parent);
}

void preElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const char* s = t.cstart;
    if (s < t.cend && *s == '\r') ++s;
    if (s < t.cend && *s == '\n') ++s;  // a newline right after <pre> is not content
    if (is(t.name, "pre") || is(t.name, "listing")) {  // <listing> = old <pre>
        Ctx pre = ctx;
        pre.pre = true;
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
    walk(t.cstart, t.cend, tmp, Style{}, true);
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
    lv_checkbox_set_text(cb, attr(t, "label").c_str());
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
        P::String a = keepAttrs(t, {"id", "class", "min", "max", "bind", "onchange", "w", "h"});
        if (attr(t, "min").empty()) a += " min=\"0\"";
        if (attr(t, "max").empty()) a += " max=\"100\"";
        create_slider(a.c_str(), a.c_str() + a.size(), parent);
        lv_obj_t* s = lv_obj_get_child(parent, -1);
        if (s && attr(t, "bind").empty() && !attr(t, "value").empty())
            lv_slider_set_value(s, atoi(attr(t, "value").c_str()), LV_ANIM_OFF);
        return;
    }
    if (type == "button" || type == "submit" || type == "reset") {
        P::String label = attr(t, "value");
        if (label.empty()) label = type == "submit" ? "Submit" : type == "reset" ? "Reset" : "";
        P::String a = keepAttrs(t, {"id", "class", "onclick", "href", "w", "h", "bgcolor", "color"});
        create_button(a.c_str(), a.c_str() + a.size(), label.c_str(), parent);
        return;
    }
    if (type == "image") {
        imgElement(t, parent);
        return;
    }
    // text-like: text, password, email, search, tel, url, number, date, time, color, file...
    P::String a = keepAttrs(t, {"id", "class", "bind", "placeholder", "onchange", "oninput", "onenter", "onblur",
                                "w", "h", "bgcolor", "color"});
    if (type == "password") a += " password=\"true\"";
    create_input(a.c_str(), a.c_str() + a.size(), "", parent);
    if (attr(t, "bind").empty() && !attr(t, "value").empty()) {
        if (lv_obj_t* ta = lastTextarea(parent)) lv_textarea_set_text(ta, attr(t, "value").c_str());
    }
}

void textareaElement(const TagRef& t, lv_obj_t* parent) {
    P::String a = keepAttrs(t, {"id", "class", "bind", "placeholder", "onchange", "oninput", "onblur", "w",
                                "bgcolor", "color"});
    a += " multiline=\"true\"";
    P::String h = attr(t, "h");
    if (h.empty() && !attr(t, "rows").empty()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", atoi(attr(t, "rows").c_str()) * 20 + 16);
        h = buf;
    }
    if (!h.empty()) a += " h=\"" + h + "\"";
    create_input(a.c_str(), a.c_str() + a.size(), "", parent);
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
    if (meter) {  // inside [low, high] (or no bounds) = fine; outside = warning
        const double lo = attrNum(t.astart, t.aend, "low", mn), hi = attrNum(t.astart, t.aend, "high", mx);
        color = (v < lo || v > hi) ? kMark : kCode;
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

void detailsElement(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    lv_obj_t* d = newBox(box, kGapList);
    applyGlobal(t, d);
    const bool open = has(t, "open");

    lv_obj_t* head = plainObj(d);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(head, 8, LV_PART_MAIN);
    lv_obj_t* marker = lv_label_create(head);
    lv_label_set_text(marker, open ? "\xE2\x80\x93" : "+");
    lv_obj_set_style_text_color(marker, lv_color_hex(kAccent), LV_PART_MAIN);
    lv_obj_t* title = newBox(head, 0);
    lv_obj_set_width(title, 0);
    lv_obj_set_flex_grow(title, 1);

    TagRef sum;
    const bool hasSummary = findChild(t, "summary", sum);
    if (hasSummary) {
        Seg seg(title, ctx, &sum, TextLook{});
        walk(sum.cstart, sum.cend, seg, Style{}, true);
        seg.flush();
    } else {
        newTextLabel(title, "Details");
    }

    lv_obj_t* body = newBox(d, kGap);
    lv_obj_set_style_pad_left(body, kIndent, LV_PART_MAIN);
    if (!open) lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    Seg seg(body, ctx);
    if (hasSummary) {
        walk(t.cstart, sum.src, seg, Style{}, false);
        walk(sum.next, t.cend, seg, Style{}, false);
    } else {
        walk(t.cstart, t.cend, seg, Style{}, false);
    }
    seg.flush();

    auto* fold = new Fold{body, marker};
    lv_obj_add_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(head, onFoldClick, LV_EVENT_CLICKED, fold);
    lv_obj_add_event_cb(head, onFoldDelete, LV_EVENT_DELETE, fold);
}

void onDialogClose(lv_event_t* e) { lv_obj_add_flag(static_cast<lv_obj_t*>(lv_event_get_user_data(e)), LV_OBJ_FLAG_HIDDEN); }

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
    lv_obj_t* win = newBox(back, kGap);
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
    P::String text = P::String("[") + t.name + "]";
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
        walk(t.cstart, t.cend, probe, Style{}, true);
        bool text = false;
        for (const auto& it : probe.items()) {
            for (char c : it.text) text = text || !isspace(static_cast<unsigned char>(c));
        }
        if (text) {
            lv_obj_t* b = newBox(box, kGap);
            applyGlobal(t, b);
            renderInto(t, b, ctx);
            return;
        }
    }
    placeholder(t, box);
}

// ---- Table cells TelaOS does not know ----

void thElement(const TagRef& t, lv_obj_t* parent) {
    // same as TelaOS <td>, header look: accent, centered
    P::String s = "<td";
    s.append(t.astart, t.aend - t.astart);
    s += ">";
    s.append(t.cstart, t.cend - t.cstart);
    s += "</td>";
    parse_children(s.c_str(), static_cast<int>(s.size()), parent);
    if (lv_obj_t* td = lv_obj_get_child(parent, -1)) {
        lv_obj_set_style_text_color(td, lv_color_hex(kAccent), LV_PART_MAIN);
        lv_obj_set_style_text_align(td, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

// ============ Dispatch ============

void renderAtom(const TagRef& t, lv_obj_t* parent) {
    const char* n = t.name;
    if (is(n, "img")) imgElement(t, parent);
    else if (is(n, "input") && isHtmlInput(t.astart, t.aend)) inputElement(t, parent);
    else if (is(n, "textarea")) textareaElement(t, parent);
    else if (is(n, "progress")) barElement(t, parent, false);
    else if (is(n, "meter")) barElement(t, parent, true);
    else delegate(t, parent);
}

void renderBlock(const TagRef& t, lv_obj_t* box, const Ctx& ctx) {
    const char* n = t.name;
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
        case Kind::Hidden: return;
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
    } else if (is(n, "html")) renderInto(t, box, ctx);
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
    LOG_D(Log::UI, "html: <%s>", tag);
}

namespace {

// Inline-level start tag at p: text formatting, <br>, HTML form controls and images.
// TelaOS widgets (label, button, input without type=) are not: they keep their own placement.
bool inlineLevelAt(const char* p, const char* end, TagRef& t) {
    char name[24];
    if (!peekName(p, end, name, sizeof(name))) return false;
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
        if (end - p >= 4 && strncmp(p, "<!--", 4) == 0) {
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
            if (end - p >= 4 && strncmp(p, "<!--", 4) == 0) {
                p = skipComment(p, end);
                continue;
            }
            TagRef t;
            char name[24];
            if (peekName(p, end, name, sizeof(name)) && readTag(p, end, t)) {
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
        if (end - p >= 4 && strncmp(p, "<!--", 4) == 0) {
            p = skipComment(p, end);
            continue;
        }
        char name[24];
        TagRef t;
        if (!peekName(p, end, name, sizeof(name))) {  // end tag, doctype, lone '<'
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

bool setText(lv_obj_t* obj, const char* text) {
    if (!obj) return false;
    const char* s = text ? text : "";
    auto c = s_contentOf.find(obj);
    if (c != s_contentOf.end()) obj = c->second;
#if LV_USE_SPAN
    if (lv_obj_check_type(obj, &lv_spangroup_class)) {  // formatted paragraph: one plain span
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
        lv_obj_clean(obj);
        newTextLabel(obj, s);
        return true;
    }
    return false;
}

void reset() {
    s_flow.clear();
    s_rootOf.clear();
    s_contentOf.clear();
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

void makeFlow(lv_obj_t* container) {
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, kGap, LV_PART_MAIN);
}

void renderContent(const char* html, size_t len, lv_obj_t* container) {
    textDefaults(container);
    if (!s_flow.count(container)) track(container);
    Seg seg(container, Ctx{});
    walk(html, html + len, seg, Style{}, false);
    seg.flush();
}

}  // namespace HtmlFlow
