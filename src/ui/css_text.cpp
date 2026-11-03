/**
 * css_text.cpp — see css_text.h
 *
 * line-height becomes the space LVGL adds between lines (text_line_space): the line height asked
 * for minus the font's own. LVGL labels wrap at their width, so one line (white-space: nowrap) is
 * a label as wide as its text, or one line high when its width is set: cut there (LONG_CLIP), or
 * with dots (text-overflow: ellipsis, LONG_DOT).
 */
#include "ui/css_text.h"
#include "ui/css_values.h"
#include "utils/font.h"

#include <algorithm>
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

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// text-decoration: underline, line-through, both, none; a color or a style in it is not drawn
bool decorValue(std::string_view v, lv_text_decor_t& d) {
    const P::Array<std::string_view> parts = cssParts(v);
    if (parts.empty()) return false;
    int out = LV_TEXT_DECOR_NONE;
    bool line = false;
    for (std::string_view p : parts) {
        CssColor c;
        if (equalsNoCase(p, "none")) {
            line = true;
        } else if (equalsNoCase(p, "underline")) {
            out |= LV_TEXT_DECOR_UNDERLINE;
            line = true;
        } else if (equalsNoCase(p, "line-through")) {
            out |= LV_TEXT_DECOR_STRIKETHROUGH;
            line = true;
        } else if (equalsNoCase(p, "overline") || equalsNoCase(p, "solid") || equalsNoCase(p, "double") ||
                   equalsNoCase(p, "dotted") || equalsNoCase(p, "dashed") || equalsNoCase(p, "wavy") ||
                   cssColor(p, c)) {
            // LVGL has no overline, styles or colors of the line: the line itself still counts
        } else {
            return false;
        }
    }
    if (!line) return false;
    d = static_cast<lv_text_decor_t>(out);
    return true;
}

// line-height: normal, 1.5 (times the font size), 150%, 24px — in px, 0 for normal
bool lineHeightValue(std::string_view v, float fontPx, int32_t& px) {
    v = trimmed(v);
    if (equalsNoCase(v, "normal")) {
        px = 0;
        return true;
    }
    size_t i = 0;
    float n = 0;
    if (cssNumber(v, i, n) && i == v.size()) {
        if (n < 0) return false;
        px = static_cast<int32_t>(std::lround(n * fontPx));
        return true;
    }
    if (cssPercent(v, n)) {
        if (n < 0) return false;
        px = static_cast<int32_t>(std::lround(n * fontPx / 100));
        return true;
    }
    return cssLength(v, fontPx, px) && px >= 0;
}

}  // namespace

void applyCssText(lv_obj_t* o, const P::Array<CssDecl>& decls, float em, uint16_t state) {
    if (!o) return;
    const lv_style_selector_t sel = LV_PART_MAIN | state;
    bool nowrap = false, ellipsis = false, width = false;
    for (const CssDecl& d : decls) {
        const std::string_view name = d.name, value = d.value;
        if (name == "text-decoration" || name == "text-decoration-line") {
            lv_text_decor_t decor = LV_TEXT_DECOR_NONE;
            if (decorValue(value, decor)) lv_obj_set_style_text_decor(o, decor, sel);
        } else if (name == "letter-spacing") {
            const std::string_view v = trimmed(value);
            int32_t px = 0;
            if (equalsNoCase(v, "normal")) lv_obj_set_style_text_letter_space(o, 0, sel);
            else if (cssLength(v, em, px)) lv_obj_set_style_text_letter_space(o, px, sel);
        } else if (name == "line-height") {
            int32_t px = 0;
            if (lineHeightValue(value, em, px)) {
                // LVGL adds this much between lines; normal is the font's own line
                const int32_t font = lv_font_get_line_height(UI::Font::get(static_cast<int>(em)));
                lv_obj_set_style_text_line_space(o, px == 0 ? 0 : px - font, sel);
            }
        } else if (name == "white-space") {
            nowrap = equalsNoCase(trimmed(value), "nowrap");
        } else if (name == "text-overflow") {
            ellipsis = equalsNoCase(trimmed(value), "ellipsis");
        } else if (name == "width" || name == "w") {
            width = true;
        }
    }
    if (state != 0 || !nowrap || !lv_obj_check_type(o, &lv_label_class)) return;
    if (!width && !ellipsis) {  // as wide as its text; the parent cuts what does not fit
        lv_obj_set_width(o, LV_SIZE_CONTENT);
        return;
    }
    lv_label_set_long_mode(o, ellipsis ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_CLIP);
    lv_obj_set_height(o, lv_font_get_line_height(UI::Font::get(static_cast<int>(em))));
}

void applyCssPlaceholder(lv_obj_t* o, const P::Array<CssDecl>& decls, float em) {
    if (!o) return;
    const lv_style_selector_t sel = LV_PART_TEXTAREA_PLACEHOLDER;
    int alpha = LV_OPA_COVER, opacity = LV_OPA_COVER;
    bool fade = false;
    for (const CssDecl& d : decls) {
        const std::string_view name = d.name, value = d.value;
        if (name == "color") {
            CssColor c;
            if (cssColor(value, c)) {
                lv_obj_set_style_text_color(o, lv_color_hex(c.rgb), sel);
                alpha = c.alpha;
                fade = true;
            }
        } else if (name == "opacity") {
            float v = 0;
            size_t i = 0;
            const std::string_view t = trimmed(value);
            if (cssPercent(t, v)) v /= 100;
            else if (!cssNumber(t, i, v) || i != t.size()) continue;
            opacity = static_cast<int>(std::lround(std::min(std::max(v, 0.0f), 1.0f) * LV_OPA_COVER));
            fade = true;
        } else if (name == "letter-spacing") {
            const std::string_view v = trimmed(value);
            int32_t px = 0;
            if (equalsNoCase(v, "normal")) lv_obj_set_style_text_letter_space(o, 0, sel);
            else if (cssLength(v, em, px)) lv_obj_set_style_text_letter_space(o, px, sel);
        } else if (name == "text-decoration" || name == "text-decoration-line") {
            lv_text_decor_t decor = LV_TEXT_DECOR_NONE;
            if (decorValue(value, decor)) lv_obj_set_style_text_decor(o, decor, sel);
        }
    }
    // a hint has no box of its own: its opacity is its text's, with the color's alpha
    if (fade) lv_obj_set_style_text_opa(o, static_cast<lv_opa_t>(alpha * opacity / LV_OPA_COVER), sel);
}

}  // namespace UI
