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

void applyBorder(lv_obj_t* o, const Border& b) {
    lv_border_side_t mask = LV_BORDER_SIDE_NONE;
    for (size_t k = 0; k < 4; ++k) {
        if (b.sides[k].shown()) mask = static_cast<lv_border_side_t>(mask | kSideBits[k]);
    }
    if (mask == LV_BORDER_SIDE_NONE) {
        lv_obj_set_style_border_width(o, 0, 0);
        return;
    }
    size_t k = b.last;
    if (!b.sides[k].shown()) {
        k = 0;
        while (!b.sides[k].shown()) ++k;
    }
    const Line& l = b.sides[k];
    const CssColor c = l.hasColor ? l.color : currentColor(o);
    lv_obj_set_style_border_width(o, l.width, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(c.rgb), 0);
    lv_obj_set_style_border_opa(o, c.alpha, 0);
    lv_obj_set_style_border_side(o, mask, 0);
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

void applyOutline(lv_obj_t* o, const Outline& out) {
    if (out.hasOffset) lv_obj_set_style_outline_pad(o, out.offset, 0);
    if (!out.line.shown()) {
        lv_obj_set_style_outline_width(o, 0, 0);
        return;
    }
    const CssColor c = out.line.hasColor ? out.line.color : currentColor(o);
    lv_obj_set_style_outline_width(o, out.line.width, 0);
    lv_obj_set_style_outline_color(o, lv_color_hex(c.rgb), 0);
    lv_obj_set_style_outline_opa(o, c.alpha, 0);
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

void applyShadow(lv_obj_t* o, const Shadow& s) {
    if (s.none) {
        lv_obj_set_style_shadow_width(o, 0, 0);
        return;
    }
    const CssColor c = s.hasColor ? s.color : currentColor(o);
    lv_obj_set_style_shadow_width(o, std::max<int32_t>(1, s.blur), 0);  // LVGL draws nothing at 0
    lv_obj_set_style_shadow_offset_x(o, s.x, 0);
    lv_obj_set_style_shadow_offset_y(o, s.y, 0);
    lv_obj_set_style_shadow_spread(o, s.spread, 0);
    lv_obj_set_style_shadow_color(o, lv_color_hex(c.rgb), 0);
    lv_obj_set_style_shadow_opa(o, c.alpha, 0);
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

void applyGradient(lv_obj_t* o, const Gradient& g) {
    lv_obj_set_style_bg_color(o, lv_color_hex(g.from.rgb), 0);
    lv_obj_set_style_bg_grad_color(o, lv_color_hex(g.to.rgb), 0);
    lv_obj_set_style_bg_grad_dir(o, g.dir, 0);
    lv_obj_set_style_bg_main_stop(o, g.fromStop, 0);
    lv_obj_set_style_bg_grad_stop(o, g.toStop, 0);
    lv_obj_set_style_bg_main_opa(o, g.from.alpha, 0);
    lv_obj_set_style_bg_grad_opa(o, g.to.alpha, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
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
    if (x == Flow::Visible && y == Flow::Visible) lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

}  // namespace

void applyCssBox(lv_obj_t* o, const P::Array<CssDecl>& decls, float em) {
    if (!o) return;
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
    if (border.any) applyBorder(o, border);
    if (outline.any) applyOutline(o, outline);
    if (hasShadow) applyShadow(o, shadow);
    if (hasGradient) applyGradient(o, gradient);
    if (flowSet) applyOverflow(o, fx, fy);
}

}  // namespace UI
