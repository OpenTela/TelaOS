/**
 * css_box.cpp — see css_box.h
 *
 * LVGL draws one border (one width, one color, any set of sides), one outline and one outer
 * shadow per object, and two-color gradients along an axis. The CSS that maps onto that is put
 * on; what does not is left out, as a value a browser cannot read is.
 */
#include "ui/css_box.h"
#include "ui/css_values.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace UI {

namespace {

constexpr char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

bool startsNoCase(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && equalsNoCase(s.substr(0, prefix.size()), prefix);
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// The color text is drawn in, for a border, an outline or a shadow without one (currentcolor)
CssColor currentColor(lv_obj_t* o) {
    CssColor c;
    c.rgb = lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN)) & 0xFFFFFF;
    c.alpha = static_cast<uint8_t>(lv_obj_get_style_text_opa(o, LV_PART_MAIN));
    return c;
}

// ============ Lines: border and outline ============

// thin, medium, thick or a length, not negative
bool lineWidth(std::string_view p, float em, int32_t& w) {
    p = trimmed(p);
    if (equalsNoCase(p, "thin")) w = 1;
    else if (equalsNoCase(p, "medium")) w = 3;
    else if (equalsNoCase(p, "thick")) w = 5;
    else return cssLength(p, em, w) && w >= 0;
    return true;
}

// none and hidden draw nothing; LVGL draws every other style solid
bool lineStyle(std::string_view p, bool& drawn) {
    static constexpr std::array<const char*, 9> kDrawn{"solid", "dashed", "dotted", "double", "groove",
                                                       "ridge", "inset", "outset", "auto"};
    p = trimmed(p);
    if (equalsNoCase(p, "none") || equalsNoCase(p, "hidden")) {
        drawn = false;
        return true;
    }
    for (const char* k : kDrawn) {
        if (equalsNoCase(p, k)) {
            drawn = true;
            return true;
        }
    }
    return false;
}

struct Line {  // as CSS starts one: no style, medium, the text's color
    bool drawn = false;
    int32_t width = 3;
    bool hasColor = false;
    CssColor color;
    bool shown() const { return drawn && width > 0; }
};

// border: 1px solid #ccc — width, style and color in any order, each at most once
bool lineShorthand(std::string_view value, float em, Line& out) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.empty() || parts.size() > 3) return false;
    Line l;
    bool style = false, width = false;
    for (std::string_view p : parts) {
        bool drawn = false;
        int32_t w = 0;
        CssColor c;
        if (!style && lineStyle(p, drawn)) {
            l.drawn = drawn;
            style = true;
        } else if (!width && lineWidth(p, em, w)) {
            l.width = w;
            width = true;
        } else if (!l.hasColor && cssColor(p, c)) {
            l.color = c;
            l.hasColor = true;
        } else {
            return false;
        }
    }
    out = l;
    return true;
}

// 1 to 4 values, top right bottom left; a missing side takes the opposite one
template <typename T, typename Read>
bool fourSides(std::string_view value, std::array<T, 4>& out, Read read) {
    const P::Array<std::string_view> parts = cssParts(value);
    if (parts.empty() || parts.size() > 4) return false;
    std::array<T, 4> v{};
    for (size_t k = 0; k < parts.size(); ++k) {
        if (!read(parts[k], v[k])) return false;
    }
    const size_t n = parts.size();
    out[0] = v[0];
    out[1] = n > 1 ? v[1] : v[0];
    out[2] = n > 2 ? v[2] : v[0];
    out[3] = n > 3 ? v[3] : out[1];
    return true;
}

constexpr std::array<std::string_view, 4> kSides{"top", "right", "bottom", "left"};
constexpr std::array<lv_border_side_t, 4> kSideBits{LV_BORDER_SIDE_TOP, LV_BORDER_SIDE_RIGHT, LV_BORDER_SIDE_BOTTOM,
                                                    LV_BORDER_SIDE_LEFT};

// border-top → side 0, rest ""; border-left-color → side 3, rest "color"; false if no side is named
bool sideOf(std::string_view name, std::string_view prefix, size_t& side, std::string_view& rest) {
    if (!startsNoCase(name, prefix)) return false;
    name.remove_prefix(prefix.size());
    for (size_t k = 0; k < kSides.size(); ++k) {
        if (name.substr(0, kSides[k].size()) != kSides[k]) continue;
        rest = name.substr(kSides[k].size());
        if (!rest.empty() && rest[0] != '-') continue;
        if (!rest.empty()) rest.remove_prefix(1);
        side = k;
        return true;
    }
    return false;
}

struct Border {
    std::array<Line, 4> sides;
    bool any = false;
    size_t last = 0;  // the side set last: its width and color go on the whole border
};

bool borderDecl(const CssDecl& d, float em, Border& b) {
    const std::string_view name = d.name, value = d.value;
    if (name == "border") {
        Line l;
        if (!lineShorthand(value, em, l)) return false;
        b.sides.fill(l);
        return true;
    }
    if (name == "border-width") {
        std::array<int32_t, 4> w{};
        if (!fourSides(value, w, [em](std::string_view p, int32_t& x) { return lineWidth(p, em, x); })) return false;
        for (size_t k = 0; k < 4; ++k) b.sides[k].width = w[k];
        return true;
    }
    if (name == "border-style") {
        std::array<bool, 4> s{};
        if (!fourSides(value, s, lineStyle)) return false;
        for (size_t k = 0; k < 4; ++k) b.sides[k].drawn = s[k];
        return true;
    }
    if (name == "border-color") {
        std::array<CssColor, 4> c{};
        if (!fourSides(value, c, [](std::string_view p, CssColor& x) { return cssColor(p, x); })) return false;
        for (size_t k = 0; k < 4; ++k) {
            b.sides[k].color = c[k];
            b.sides[k].hasColor = true;
        }
        return true;
    }
    size_t side = 0;
    std::string_view rest;
    if (!sideOf(name, "border-", side, rest)) return false;
    Line& l = b.sides[side];
    if (rest.empty()) {
        if (!lineShorthand(value, em, l)) return false;
    } else if (rest == "width") {
        int32_t w = 0;
        if (!lineWidth(value, em, w)) return false;
        l.width = w;
    } else if (rest == "style") {
        if (!lineStyle(value, l.drawn)) return false;
    } else if (rest == "color") {
        if (!cssColor(value, l.color)) return false;
        l.hasColor = true;
    } else {
        return false;  // border-top-left-radius and the like
    }
    b.last = side;
    return true;
}

void applyBorder(lv_obj_t* o, const Border& b, lv_style_selector_t sel) {
    lv_border_side_t mask = LV_BORDER_SIDE_NONE;
    for (size_t k = 0; k < 4; ++k) {
        if (b.sides[k].shown()) mask = static_cast<lv_border_side_t>(mask | kSideBits[k]);
    }
    if (mask == LV_BORDER_SIDE_NONE) {
        lv_obj_set_style_border_width(o, 0, sel);
        return;
    }
    size_t k = b.last;
    if (!b.sides[k].shown()) {
        k = 0;
        while (!b.sides[k].shown()) ++k;
    }
    const Line& l = b.sides[k];
    const CssColor c = l.hasColor ? l.color : currentColor(o);
    lv_obj_set_style_border_width(o, l.width, sel);
    lv_obj_set_style_border_color(o, lv_color_hex(c.rgb), sel);
    lv_obj_set_style_border_opa(o, c.alpha, sel);
    lv_obj_set_style_border_side(o, mask, sel);
}

struct Outline {
    Line line;
    bool any = false;
    bool hasOffset = false;
    int32_t offset = 0;
};

bool outlineDecl(const CssDecl& d, float em, Outline& out) {
    const std::string_view name = d.name, value = d.value;
    if (name == "outline") return lineShorthand(value, em, out.line);
    if (name == "outline-width") {
        int32_t w = 0;
        if (!lineWidth(value, em, w)) return false;
        out.line.width = w;
        return true;
    }
    if (name == "outline-style") return lineStyle(value, out.line.drawn);
    if (name == "outline-color") {
        if (!cssColor(value, out.line.color)) return false;
        out.line.hasColor = true;
        return true;
    }
    if (name == "outline-offset") {
        if (!cssLength(value, em, out.offset)) return false;
        out.hasOffset = true;
        return true;
    }
    return false;
}

void applyOutline(lv_obj_t* o, const Outline& out, lv_style_selector_t sel) {
    if (out.hasOffset) lv_obj_set_style_outline_pad(o, out.offset, sel);
    if (!out.line.shown()) {
        lv_obj_set_style_outline_width(o, 0, sel);
        return;
    }
    const CssColor c = out.line.hasColor ? out.line.color : currentColor(o);
    lv_obj_set_style_outline_width(o, out.line.width, sel);
    lv_obj_set_style_outline_color(o, lv_color_hex(c.rgb), sel);
    lv_obj_set_style_outline_opa(o, c.alpha, sel);
}

// ============ margin ============

// A length, negative too; auto is 0 (LVGL has no auto margins: margin: 0 auto does not center)
bool marginValue(std::string_view p, float em, int32_t& v) {
    if (equalsNoCase(trimmed(p), "auto")) {
        v = 0;
        return true;
    }
    return cssLength(p, em, v);
}

// ============ box-shadow ============

struct Shadow {
    bool none = true;
    int32_t x = 0, y = 0, blur = 0, spread = 0;
    bool hasColor = false;
    CssColor color;
};

// box-shadow: 2px 4px 8px 1px rgba(0, 0, 0, 0.5). LVGL draws one shadow, outside the box: the first
// one that is not inset.
bool shadowValue(std::string_view value, float em, Shadow& out) {
    if (equalsNoCase(trimmed(value), "none")) {
        out = Shadow{};
        return true;
    }
    const P::Array<std::string_view> list = cssList(value);
    if (list.empty()) return false;
    bool found = false;
    for (std::string_view one : list) {
        Shadow s;
        s.none = false;
        bool inset = false;
        size_t count = 0;
        std::array<int32_t, 4> len{};
        for (std::string_view p : cssParts(one)) {
            int32_t v = 0;
            if (equalsNoCase(p, "inset") && !inset) {
                inset = true;
            } else if (count < len.size() && cssLength(p, em, v)) {
                len[count++] = v;
            } else if (!s.hasColor && cssColor(p, s.color)) {
                s.hasColor = true;
            } else {
                return false;  // a shadow that does not read: the whole declaration is out
            }
        }
        if (count < 2 || (count > 2 && len[2] < 0)) return false;  // offsets are needed; no negative blur
        if (inset || found) continue;
        s.x = len[0];
        s.y = len[1];
        s.blur = count > 2 ? len[2] : 0;
        s.spread = count > 3 ? len[3] : 0;
        out = s;
        found = true;
    }
    if (!found) out = Shadow{};  // inset ones only: nothing outside
    return true;
}

void applyShadow(lv_obj_t* o, const Shadow& s, lv_style_selector_t sel) {
    if (s.none) {
        lv_obj_set_style_shadow_width(o, 0, sel);
        return;
    }
    const CssColor c = s.hasColor ? s.color : currentColor(o);
    lv_obj_set_style_shadow_width(o, std::max<int32_t>(1, s.blur), sel);  // LVGL draws nothing at 0
    lv_obj_set_style_shadow_offset_x(o, s.x, sel);
    lv_obj_set_style_shadow_offset_y(o, s.y, sel);
    lv_obj_set_style_shadow_spread(o, s.spread, sel);
    lv_obj_set_style_shadow_color(o, lv_color_hex(c.rgb), sel);
    lv_obj_set_style_shadow_opa(o, c.alpha, sel);
}

// ============ linear-gradient() ============

struct Gradient {
    lv_grad_dir_t dir = LV_GRAD_DIR_VER;
    CssColor from, to;
    int32_t fromStop = 0, toStop = 255;  // 0..255 along the box
};

// A color stop: #f00, #f00 20%
bool colorStop(std::string_view item, CssColor& c, int32_t& stop, int32_t def) {
    const P::Array<std::string_view> parts = cssParts(item);
    if (parts.empty() || parts.size() > 2 || !cssColor(parts[0], c)) return false;
    stop = def;
    if (parts.size() == 2) {
        size_t i = 0;
        float v = 0;
        const std::string_view p = parts[1];
        if (!cssNumber(p, i, v) || p.substr(i) != "%") return false;
        stop = static_cast<int32_t>(std::lround(std::min(std::max(v, 0.0f), 100.0f) * 255 / 100));
    }
    return true;
}

// linear-gradient(to right, #f00, #00f 80%). LVGL draws two colors along an axis: the first and the
// last stop; a corner or an angle off the axes is left out.
bool linearGradient(std::string_view text, Gradient& g) {
    text = trimmed(text);
    constexpr std::string_view kFn = "linear-gradient(";
    if (!startsNoCase(text, kFn) || text.back() != ')') return false;
    const P::Array<std::string_view> items = cssList(text.substr(kFn.size(), text.size() - kFn.size() - 1));
    if (items.size() < 2) return false;
    size_t first = 0;
    int angle = 180;  // to bottom
    const std::string_view head = items[0];
    if (startsNoCase(head, "to ")) {
        const std::string_view side = trimmed(head.substr(3));
        if (equalsNoCase(side, "bottom")) angle = 180;
        else if (equalsNoCase(side, "top")) angle = 0;
        else if (equalsNoCase(side, "right")) angle = 90;
        else if (equalsNoCase(side, "left")) angle = 270;
        else return false;
        first = 1;
    } else {
        size_t i = 0;
        float v = 0;
        if (cssNumber(head, i, v)) {
            const std::string_view unit = head.substr(i);
            if (equalsNoCase(unit, "turn")) v *= 360;
            else if (!equalsNoCase(unit, "deg")) return false;
            const float a = std::fmod(std::fmod(v, 360.0f) + 360.0f, 360.0f);
            if (a != 0 && a != 90 && a != 180 && a != 270) return false;
            angle = static_cast<int>(a);
            first = 1;
        }
    }
    if (items.size() - first < 2) return false;
    if (!colorStop(items[first], g.from, g.fromStop, 0) || !colorStop(items.back(), g.to, g.toStop, 255)) return false;
    g.dir = (angle == 90 || angle == 270) ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER;
    if (angle == 0 || angle == 270) {  // to top, to left: LVGL goes down and right, so the other way round
        std::swap(g.from, g.to);
        const int32_t a = g.fromStop;
        g.fromStop = 255 - g.toStop;
        g.toStop = 255 - a;
    }
    return true;
}

void applyGradient(lv_obj_t* o, const Gradient& g, lv_style_selector_t sel) {
    lv_obj_set_style_bg_color(o, lv_color_hex(g.from.rgb), sel);
    lv_obj_set_style_bg_grad_color(o, lv_color_hex(g.to.rgb), sel);
    lv_obj_set_style_bg_grad_dir(o, g.dir, sel);
    lv_obj_set_style_bg_main_stop(o, g.fromStop, sel);
    lv_obj_set_style_bg_grad_stop(o, g.toStop, sel);
    lv_obj_set_style_bg_main_opa(o, g.from.alpha, sel);
    lv_obj_set_style_bg_grad_opa(o, g.to.alpha, sel);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, sel);
}

// ============ overflow ============

enum class Flow { Visible, Hidden, Scroll, Auto };

bool flowValue(std::string_view p, Flow& f) {
    p = trimmed(p);
    if (equalsNoCase(p, "visible")) f = Flow::Visible;
    else if (equalsNoCase(p, "hidden") || equalsNoCase(p, "clip")) f = Flow::Hidden;
    else if (equalsNoCase(p, "scroll")) f = Flow::Scroll;
    else if (equalsNoCase(p, "auto")) f = Flow::Auto;
    else return false;
    return true;
}

// Scrolls along an axis that is scroll or auto; LVGL clips the content of every object unless it
// lets it overflow, so the content shows outside only when both axes are visible
void applyOverflow(lv_obj_t* o, Flow x, Flow y) {
    const bool sx = x == Flow::Scroll || x == Flow::Auto;
    const bool sy = y == Flow::Scroll || y == Flow::Auto;
    if (sx || sy) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(o, sx && sy ? LV_DIR_ALL : sx ? LV_DIR_HOR : LV_DIR_VER);
        lv_obj_set_scrollbar_mode(o, (x == Flow::Scroll || y == Flow::Scroll) ? LV_SCROLLBAR_MODE_ON
                                                                             : LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    }
    letOverflow(o, x == Flow::Visible && y == Flow::Visible);
}

void overflowRoom(lv_event_t* e) { lv_event_set_ext_draw_size(e, kOverflowRoom); }

}  // namespace

void letOverflow(lv_obj_t* o, bool visible) {
    if (!o) return;
    lv_obj_remove_event_cb(o, overflowRoom);
    if (visible) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_add_event_cb(o, overflowRoom, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    } else {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    }
    lv_obj_refresh_ext_draw_size(o);
}

void applyCssBox(lv_obj_t* o, const P::Array<CssDecl>& decls, float em, uint16_t state) {
    if (!o) return;
    const bool base = state == 0;
    const lv_style_selector_t sel = LV_PART_MAIN | state;
    Border border;
    Outline outline;
    Shadow shadow;
    bool hasShadow = false;
    bool hasGradient = false;
    Gradient gradient;
    bool flowSet = false;
    Flow fx = Flow::Visible, fy = Flow::Visible;
    for (const CssDecl& d : decls) {
        const std::string_view name = d.name, value = d.value;
        int32_t v = 0;
        if (startsNoCase(name, "border")) {
            if (borderDecl(d, em, border)) border.any = true;
        } else if (startsNoCase(name, "outline")) {
            if (outlineDecl(d, em, outline)) outline.any = true;
        } else if (!base && (startsNoCase(name, "margin") || startsNoCase(name, "overflow"))) {
            continue;  // where the element sits and what it shows: the element itself, not a state
        } else if (name == "margin") {
            std::array<int32_t, 4> m{};
            if (fourSides(value, m, [em](std::string_view p, int32_t& x) { return marginValue(p, em, x); })) {
                lv_obj_set_style_margin_top(o, m[0], 0);
                lv_obj_set_style_margin_right(o, m[1], 0);
                lv_obj_set_style_margin_bottom(o, m[2], 0);
                lv_obj_set_style_margin_left(o, m[3], 0);
            }
        } else if (name == "margin-top") {
            if (marginValue(value, em, v)) lv_obj_set_style_margin_top(o, v, 0);
        } else if (name == "margin-right") {
            if (marginValue(value, em, v)) lv_obj_set_style_margin_right(o, v, 0);
        } else if (name == "margin-bottom") {
            if (marginValue(value, em, v)) lv_obj_set_style_margin_bottom(o, v, 0);
        } else if (name == "margin-left") {
            if (marginValue(value, em, v)) lv_obj_set_style_margin_left(o, v, 0);
        } else if (name == "box-shadow") {
            if (shadowValue(value, em, shadow)) hasShadow = true;
        } else if (name == "background" || name == "background-image") {
            // the later of the two decides: background without a gradient resets the image too
            hasGradient = false;
            for (std::string_view p : cssParts(value)) {
                if (linearGradient(p, gradient)) {
                    hasGradient = true;
                    break;
                }
            }
        } else if (name == "overflow") {
            const P::Array<std::string_view> parts = cssParts(value);
            Flow a = Flow::Visible, b = Flow::Visible;
            if (!parts.empty() && parts.size() <= 2 && flowValue(parts[0], a) &&
                (parts.size() == 1 ? (b = a, true) : flowValue(parts[1], b))) {
                fx = a;
                fy = b;
                flowSet = true;
            }
        } else if (name == "overflow-x") {
            if (flowValue(value, fx)) flowSet = true;
        } else if (name == "overflow-y") {
            if (flowValue(value, fy)) flowSet = true;
        }
    }
    if (border.any) applyBorder(o, border, sel);
    if (outline.any) applyOutline(o, outline, sel);
    if (hasShadow) applyShadow(o, shadow, sel);
    if (hasGradient) applyGradient(o, gradient, sel);
    if (flowSet && base) applyOverflow(o, fx, fy);
}

namespace {

// ============ transform ============

struct Transform {
    bool given = false;      // a transform that reads (none too)
    bool translate = false;  // translate() in it, or none: then translate_x/y are set
    int32_t x = 0, y = 0;    // px, or lv_pct() of the element's own size
    float sx = 1, sy = 1;
    float deg = 0;
};

// An angle: 45deg, 0.25turn, 1.57rad, 50grad; 0 without a unit
bool angleValue(std::string_view v, float& deg) {
    v = trimmed(v);
    size_t i = 0;
    float n = 0;
    if (!cssNumber(v, i, n)) return false;
    const std::string_view u = v.substr(i);
    if (u.empty() && n == 0) deg = 0;
    else if (equalsNoCase(u, "deg")) deg = n;
    else if (equalsNoCase(u, "turn")) deg = n * 360;
    else if (equalsNoCase(u, "rad")) deg = n * 57.29578f;
    else if (equalsNoCase(u, "grad")) deg = n * 0.9f;
    else return false;
    return true;
}

// translate(): a length, or a percentage of the element's own size (LVGL takes lv_pct() for that)
bool shiftValue(std::string_view v, float em, int32_t& px) {
    float p = 0;
    if (cssPercent(v, p)) {
        px = lv_pct(static_cast<int32_t>(std::lround(p)));
        return true;
    }
    return cssLength(v, em, px);
}

// scale(): a number or a percentage
bool scaleValue(std::string_view v, float& s) {
    v = trimmed(v);
    size_t i = 0;
    if (cssPercent(v, s)) {
        s /= 100;
        return true;
    }
    return cssNumber(v, i, s) && i == v.size();
}

// transform: translate(10px, 5px) scale(1.2) rotate(45deg), none. LVGL applies a translation, a
// scale and a rotation, in that order, whatever the order in CSS; skew(), matrix() and 3D are out.
bool transformValue(std::string_view value, float em, Transform& t) {
    value = trimmed(value);
    Transform out;
    out.given = true;
    if (equalsNoCase(value, "none")) {
        out.translate = true;
        t = out;
        return true;
    }
    const P::Array<std::string_view> fns = cssParts(value);
    if (fns.empty()) return false;
    for (std::string_view fn : fns) {
        const size_t open = fn.find('(');
        if (open == std::string_view::npos || fn.back() != ')') return false;
        const std::string_view name = fn.substr(0, open);
        const P::Array<std::string_view> args = cssList(fn.substr(open + 1, fn.size() - open - 2));
        const size_t n = args.size();
        float a = 0, b = 0;
        if (equalsNoCase(name, "translate") && (n == 1 || n == 2)) {
            if (!shiftValue(args[0], em, out.x) || (n == 2 && !shiftValue(args[1], em, out.y))) return false;
            out.translate = true;
        } else if (equalsNoCase(name, "translatex") && n == 1) {
            if (!shiftValue(args[0], em, out.x)) return false;
            out.translate = true;
        } else if (equalsNoCase(name, "translatey") && n == 1) {
            if (!shiftValue(args[0], em, out.y)) return false;
            out.translate = true;
        } else if (equalsNoCase(name, "scale") && (n == 1 || n == 2)) {
            if (!scaleValue(args[0], a) || (n == 2 && !scaleValue(args[1], b))) return false;
            out.sx *= a;
            out.sy *= n == 2 ? b : a;
        } else if (equalsNoCase(name, "scalex") && n == 1) {
            if (!scaleValue(args[0], a)) return false;
            out.sx *= a;
        } else if (equalsNoCase(name, "scaley") && n == 1) {
            if (!scaleValue(args[0], a)) return false;
            out.sy *= a;
        } else if ((equalsNoCase(name, "rotate") || equalsNoCase(name, "rotatez")) && n == 1) {
            if (!angleValue(args[0], a)) return false;
            out.deg += a;
        } else {
            return false;
        }
    }
    t = out;
    return true;
}

// ============ transition ============

// What a CSS property animates in LVGL
struct Animated {
    const char* name;
    std::array<lv_style_prop_t, 6> props;  // the rest is LV_STYLE_PROP_INV
};

const std::array<Animated, 13> kAnimated{{
    {"color", {LV_STYLE_TEXT_COLOR, LV_STYLE_TEXT_OPA}},
    {"background-color", {LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA}},
    {"background", {LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA}},
    {"opacity", {LV_STYLE_OPA}},
    {"border-color", {LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_OPA}},
    {"border-width", {LV_STYLE_BORDER_WIDTH}},
    {"border", {LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_OPA, LV_STYLE_BORDER_WIDTH}},
    {"outline-color", {LV_STYLE_OUTLINE_COLOR, LV_STYLE_OUTLINE_OPA}},
    {"outline-width", {LV_STYLE_OUTLINE_WIDTH}},
    {"outline", {LV_STYLE_OUTLINE_COLOR, LV_STYLE_OUTLINE_OPA, LV_STYLE_OUTLINE_WIDTH}},
    {"box-shadow", {LV_STYLE_SHADOW_WIDTH, LV_STYLE_SHADOW_OFFSET_X, LV_STYLE_SHADOW_OFFSET_Y, LV_STYLE_SHADOW_SPREAD,
                    LV_STYLE_SHADOW_COLOR, LV_STYLE_SHADOW_OPA}},
    {"transform", {LV_STYLE_TRANSLATE_X, LV_STYLE_TRANSLATE_Y, LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y,
                   LV_STYLE_TRANSFORM_ROTATION}},
    {"border-radius", {LV_STYLE_RADIUS}},
}};
constexpr uint32_t kAll = (1u << kAnimated.size()) - 1;

struct Transition {
    uint32_t props = 0;  // bits of kAnimated
    uint32_t time = 0;   // ms
    uint32_t delay = 0;
    lv_anim_path_cb_t path = lv_anim_path_ease_in_out;  // ease
};

// 0.3s, 300ms
bool timeValue(std::string_view v, uint32_t& ms) {
    v = trimmed(v);
    size_t i = 0;
    float n = 0;
    if (!cssNumber(v, i, n) || n < 0) return false;
    const std::string_view u = v.substr(i);
    if (equalsNoCase(u, "s")) ms = static_cast<uint32_t>(std::lround(n * 1000));
    else if (equalsNoCase(u, "ms")) ms = static_cast<uint32_t>(std::lround(n));
    else return false;
    return true;
}

// The easing: LVGL's paths; steps() steps, cubic-bezier() eases in and out
bool pathValue(std::string_view v, lv_anim_path_cb_t& path) {
    v = trimmed(v);
    if (equalsNoCase(v, "linear")) path = lv_anim_path_linear;
    else if (equalsNoCase(v, "ease") || equalsNoCase(v, "ease-in-out")) path = lv_anim_path_ease_in_out;
    else if (equalsNoCase(v, "ease-in")) path = lv_anim_path_ease_in;
    else if (equalsNoCase(v, "ease-out")) path = lv_anim_path_ease_out;
    else if (equalsNoCase(v, "step-start") || equalsNoCase(v, "step-end") || startsNoCase(v, "steps(")) path = lv_anim_path_step;
    else if (startsNoCase(v, "cubic-bezier(")) path = lv_anim_path_ease_in_out;
    else return false;
    return true;
}

// A property name: its bit, all of them for all; 0 for one LVGL does not animate
uint32_t propBits(std::string_view p) {
    if (equalsNoCase(p, "all")) return kAll;
    for (size_t k = 0; k < kAnimated.size(); ++k) {
        if (equalsNoCase(p, kAnimated[k].name)) return 1u << k;
    }
    return 0;
}

// transition: background-color 0.3s ease, color 0.3s. LVGL runs one transition per state, so the
// properties of all the items go together, with the time, easing and delay of the first item that
// has a time.
bool transitionValue(std::string_view value, Transition& t) {
    value = trimmed(value);
    if (equalsNoCase(value, "none")) {
        t = Transition{};
        return true;
    }
    const P::Array<std::string_view> items = cssList(value);
    if (items.empty()) return false;
    Transition out;
    bool timed = false;
    for (std::string_view item : items) {
        uint32_t props = 0, time = 0, delay = 0;
        bool named = false, hasTime = false, hasDelay = false;
        lv_anim_path_cb_t path = nullptr;
        for (std::string_view p : cssParts(item)) {
            uint32_t ms = 0;
            lv_anim_path_cb_t cb = nullptr;
            if (timeValue(p, ms)) {
                if (!hasTime) time = ms, hasTime = true;
                else if (!hasDelay) delay = ms, hasDelay = true;
                else return false;
            } else if (pathValue(p, cb)) {
                path = cb;
            } else if (!named) {
                props = propBits(p);  // an unknown property animates nothing, as a browser does
                named = true;
            } else {
                return false;
            }
        }
        out.props |= named ? props : kAll;  // transition: 0.3s is all of them
        if (hasTime && !timed) {
            out.time = time;
            out.delay = delay;
            if (path) out.path = path;
            timed = true;
        }
    }
    t = out;
    return true;
}

struct PooledTransition {
    std::array<lv_style_prop_t, 32> props{};  // ends with LV_STYLE_PROP_INV
    lv_style_transition_dsc_t dsc{};
    Transition key;
};

// LVGL keeps a pointer to the descriptor and its list as long as the style lives: they stay, one
// for each different transition
const lv_style_transition_dsc_t* pooled(const Transition& t) {
    static P::Array<P::Ptr<PooledTransition>> s_pool;
    for (const auto& p : s_pool) {
        if (p->key.props == t.props && p->key.time == t.time && p->key.delay == t.delay && p->key.path == t.path) {
            return &p->dsc;
        }
    }
    P::Ptr<PooledTransition> p = P::create<PooledTransition>();
    if (!p) return nullptr;
    size_t n = 0;
    for (size_t k = 0; k < kAnimated.size(); ++k) {
        if (!(t.props & (1u << k))) continue;
        for (lv_style_prop_t prop : kAnimated[k].props) {
            if (prop == LV_STYLE_PROP_INV || n + 1 >= p->props.size()) continue;
            if (std::find(p->props.begin(), p->props.begin() + n, prop) == p->props.begin() + n) p->props[n++] = prop;
        }
    }
    p->props[n] = LV_STYLE_PROP_INV;
    p->key = t;
    lv_style_transition_dsc_init(&p->dsc, p->props.data(), t.path, t.time, t.delay, nullptr);
    s_pool.push_back(std::move(p));
    return &s_pool.back()->dsc;
}

}  // namespace

void applyCssTransform(lv_obj_t* o, const P::Array<CssDecl>& decls, float em, uint16_t state) {
    if (!o) return;
    Transform t;
    for (const CssDecl& d : decls) {
        if (d.name == "transform") transformValue(d.value, em, t);
    }
    if (!t.given) return;
    const lv_style_selector_t sel = LV_PART_MAIN | state;
    if (t.translate) {  // a transform without translate() keeps a position: relative shift
        lv_obj_set_style_translate_x(o, t.x, sel);
        lv_obj_set_style_translate_y(o, t.y, sel);
    }
    lv_obj_set_style_transform_scale_x(o, static_cast<int32_t>(std::lround(t.sx * 256)), sel);
    lv_obj_set_style_transform_scale_y(o, static_cast<int32_t>(std::lround(t.sy * 256)), sel);
    lv_obj_set_style_transform_rotation(o, static_cast<int32_t>(std::lround(t.deg * 10)), sel);
    lv_obj_set_style_transform_pivot_x(o, lv_pct(50), sel);  // about the middle: transform-origin: center
    lv_obj_set_style_transform_pivot_y(o, lv_pct(50), sel);
}

void applyCssTransitions(lv_obj_t* o, const P::Array<CssStateDecls>& groups) {
    if (!o || groups.empty() || groups[0].state != 0) return;
    Transition t;
    bool any = false;
    for (const CssDecl& d : groups[0].decls) {
        const std::string_view name = d.name, value = d.value;
        if (name == "transition") {
            if (transitionValue(value, t)) any = true;
        } else if (name == "transition-property") {
            uint32_t props = 0;
            for (std::string_view p : cssList(value)) props |= propBits(trimmed(p));
            t.props = props;
            any = true;
        } else if (name == "transition-duration") {
            const P::Array<std::string_view> l = cssList(value);
            if (!l.empty() && timeValue(l[0], t.time)) any = true;
        } else if (name == "transition-delay") {
            const P::Array<std::string_view> l = cssList(value);
            if (!l.empty()) timeValue(l[0], t.delay);
        } else if (name == "transition-timing-function") {
            const P::Array<std::string_view> l = cssList(value);
            if (!l.empty()) pathValue(l[0], t.path);
        }
    }
    if (!any || t.time == 0 || t.props == 0) return;
    const lv_style_transition_dsc_t* dsc = pooled(t);
    if (!dsc) return;
    lv_obj_set_style_transition(o, dsc, LV_PART_MAIN);  // back from a state
    for (const CssStateDecls& g : groups) {
        if (g.state) lv_obj_set_style_transition(o, dsc, LV_PART_MAIN | g.state);  // into it
    }
}

}  // namespace UI
