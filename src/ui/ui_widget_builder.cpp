/**
 * ui_widget_builder.cpp - Widget creation functions
 * 
 * Extracted from ui_html.cpp. Contains:
 * - CommonAttrs parsing helpers
 * - Event handlers (button, switch, slider, input, canvas)
 * - Alignment/positioning helpers
 * - create_label, create_button, create_switch, create_slider,
 *   create_input, create_image, create_canvas
 */

#include "lvgl.h"
#include "ui/ui_engine.h"
#include "ui/ui_html_internal.h"
#include "ui/xml_utils.h"
#include "ui/css_parser.h"
#include "ui_layout.h"
#include "widgets/widgets.h"
#include "widgets/widget_common.h"
#include "widgets/widget_methods.h"
#include "core/state_store.h"
#include "utils/string_utils.h"
#include "utils/log_config.h"
#include <string>

#include "esp_heap_caps.h"

static const char* TAG = "ui_builder";

// Use utility namespaces
using namespace UI::StringUtils;
using namespace UI::XmlUtils;

namespace {

namespace Defaults {
    namespace Slider {
        constexpr int WIDTH = 150;
        constexpr int MIN = 0;
        constexpr int MAX = 100;
    }
    namespace Input {
        constexpr int WIDTH = 150;
        constexpr int HEIGHT = 40;
    }
}

} // anonymous namespace

// ============ Helpers ============

static P::String resolve_resource_path(const P::String& src) {
    if (src.empty()) return src;
    
    // Already absolute path (starts with / or A:)
    if (src[0] == '/' || (src.length() > 1 && src[1] == ':')) {
        return src;
    }
    
    // Relative path - prepend app resources folder
    if (!app_path.empty()) {
        char buf[128]; snprintf(buf, sizeof(buf), "%s/resources/%s", app_path.c_str(), src.c_str());
        return buf;
    }
    
    return src;
}

// ============ Common attribute parsing ============

struct CommonAttrs {
    P::String id;
    P::String cssClass;
    P::String visible;      // "{varname}" for visibility binding
    int zIndex = 0;
    bool hasDynamicClass = false;
    bool hasDynamicVisible = false;
};

static CommonAttrs parseCommonAttrs(const char *astart, const char *aend) {
    CommonAttrs attrs;
    attrs.id = getAttr(astart, aend, "id");
    attrs.cssClass = getAttr(astart, aend, "class");
    attrs.visible = getAttr(astart, aend, "visible");
    attrs.hasDynamicClass = contains(attrs.cssClass, '{');
    attrs.hasDynamicVisible = contains(attrs.visible, '{');
    attrs.zIndex = getAttrInt(astart, aend, "z-index");
    return attrs;
}

// Auto-generate id if needed for dynamic bindings
static void ensureId(CommonAttrs& attrs, const char* prefix, bool hasDynamicContent = false) {
    if (attrs.id.empty() && (hasDynamicContent || attrs.hasDynamicClass || attrs.hasDynamicVisible)) {
        static int auto_id = 0;
        char buf[32]; snprintf(buf, sizeof(buf), "%s%d", prefix, auto_id++);
        attrs.id = buf;
    }
}

// Helper: extract var name from "{varname}" pattern
P::String extractBindVar(const char* bindStr) {
    if (!bindStr || !bindStr[0]) return "";
    P::String vb = bindStr;
    if (vb.length() > 2 && vb[0] == '{' && vb.back() == '}') {
        return vb.substr(1, vb.length() - 2);
    }
    return vb;  // allow plain var name too
}

static void button_click_handler(lv_event_t *e) {
    s_in_lvgl_callback = true;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    LOG_D(Log::UI, "button_event: idx=%d code=%d elements=%d heap=%lu", 
             idx, (int)code, (int)elements.size(), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    
    if (idx < 0 || idx >= (int)elements.size()) {
        LOG_E(Log::UI, "button_event: invalid idx!");
        s_in_lvgl_callback = false;
        return;
    }
    
    LOG_I(Log::UI, "button_event: id=%s onclick='%s' onhold='%s' href='%s'", 
             elements[idx]->id.c_str(), elements[idx]->onclick.c_str(), 
             elements[idx]->onhold.c_str(), elements[idx]->href.c_str());
    
    // Handle LONG_PRESSED -> onhold
    if (code == LV_EVENT_LONG_PRESSED) {
        if (!elements[idx]->onhold.empty() && g_onhold_handler) {
            LOG_I(Log::UI, "onhold: %s()", elements[idx]->onhold.c_str());
            g_onhold_handler(elements[idx]->onhold.c_str());
        }
        s_in_lvgl_callback = false;
        return;
    }
    
    // Handle CLICKED -> onclick
    if (code == LV_EVENT_CLICKED) {
        // Handle onclick first (call Lua function)
        if (!elements[idx]->onclick.empty() && g_onclick_handler) {
            LOG_I(Log::UI, "onclick: %s()", elements[idx]->onclick.c_str());
            g_onclick_handler(elements[idx]->onclick.c_str());
        }
        
        // Then handle href (navigation)
        if (!elements[idx]->href.empty()) {
            navigate(elements[idx]->href.c_str());
        }
    }
    
    s_in_lvgl_callback = false;
}

// Switch event handler - UI updates directly
static void switch_event_handler(lv_event_t *e) {
    if (g_updating_from_binding) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)elements.size()) return;
    
    lv_obj_t *sw = elements[idx]->obj();
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    const char *val = checked ? "true" : "false";
    
    // Update state and UI directly
    if (!elements[idx]->bind.empty()) {
        ui_update_bindings(elements[idx]->bind.c_str(), val);
        // Sync Lua state so onchange can read current value
        if (g_state_change_handler) {
            g_state_change_handler(elements[idx]->bind.c_str(), val);
        }
    }
    
    // Call onchange Lua handler
    if (!elements[idx]->onchange.empty() && g_onclick_handler) {
        g_onclick_handler(elements[idx]->onchange.c_str());
    }
    
    LOG_I(Log::UI, "switch %s: %s", elements[idx]->id.c_str(), checked ? "ON" : "OFF");
}

// Slider event handler - live UI updates with throttle
static void slider_event_handler(lv_event_t *e) {
    if (g_updating_from_binding) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)elements.size()) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    
    lv_obj_t *slider = elements[idx]->obj();
    int value = lv_slider_get_value(slider);
    char val_str[SMALL_BUF_LEN];
    snprintf(val_str, sizeof(val_str), "%d", value);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Throttle UI updates during drag
        uint32_t now = lv_tick_get();
        if (now - elements[idx]->last_update < SLIDER_THROTTLE_MS) {
            return;
        }
        elements[idx]->last_update = now;
        
        // Update UI only (no Lua) during drag
        if (!elements[idx]->bind.empty()) {
            ui_update_bindings(elements[idx]->bind.c_str(), val_str);
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        LOG_I(Log::UI, "slider %s: value=%d", elements[idx]->id.c_str(), value);
        // Final update
        if (!elements[idx]->bind.empty()) {
            ui_update_bindings(elements[idx]->bind.c_str(), val_str);
        }
        
        // Sync Lua state
        if (!elements[idx]->bind.empty() && g_state_change_handler) {
            g_state_change_handler(elements[idx]->bind.c_str(), val_str);
        }
        
        // Call Lua onchange handler
        if (!elements[idx]->onchange.empty() && g_onclick_handler) {
            g_onclick_handler(elements[idx]->onchange.c_str());
        }
    }
}

// Input (textarea) event handler
static void input_event_handler(lv_event_t *e) {
    if (g_updating_from_binding) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)elements.size()) return;
    
    lv_obj_t *ta = elements[idx]->obj();
    const char *text = lv_textarea_get_text(ta);
    if (!text) return;
    
    // Update state and UI directly
    if (!elements[idx]->bind.empty()) {
        ui_update_bindings(elements[idx]->bind.c_str(), text);
    }
}

// Input defocus - sync Lua state and call onchange
static void input_complete_handler(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)elements.size()) return;
    
    lv_obj_t *ta = elements[idx]->obj();
    const char *text = lv_textarea_get_text(ta);
    if (!text) return;
    
    // Sync Lua state
    if (!elements[idx]->bind.empty() && g_state_change_handler) {
        g_state_change_handler(elements[idx]->bind.c_str(), text);
    }
    
    // Call onchange Lua handler
    if (!elements[idx]->onchange.empty() && g_onclick_handler) {
        g_onclick_handler(elements[idx]->onchange.c_str());
    }
}

static void keyboard_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = (lv_obj_t*)lv_event_get_target(e);
    
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        // Unfocus textarea
        lv_obj_t *ta = lv_keyboard_get_textarea(kb);
        if (ta) {
            lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        }
    }
}

// Hide keyboard when textarea loses focus
static void input_defocus_handler(lv_event_t *e) {
    lv_obj_t *ta = (lv_obj_t*)lv_event_get_target(e);
    
    // Walk up parent chain to find page
    lv_obj_t *obj = lv_obj_get_parent(ta);
    while (obj) {
        for (int i = 0; i < page_count; i++) {
            if (page_objs[i] == obj && g_keyboards[i]) {
                lv_obj_add_flag(g_keyboards[i], LV_OBJ_FLAG_HIDDEN);
                return;
            }
        }
        obj = lv_obj_get_parent(obj);
    }
}

static void input_focus_handler(lv_event_t *e) {
    lv_obj_t *ta = (lv_obj_t*)lv_event_get_target(e);
    LOG_I(Log::UI, "input_focus_handler: ta=%p", ta);
    
    // Walk up parent chain to find which page this textarea belongs to
    int page_idx = INVALID_INDEX;
    lv_obj_t *obj = lv_obj_get_parent(ta);
    while (obj) {
        for (int i = 0; i < page_count; i++) {
            if (page_objs[i] == obj) {
                page_idx = i;
                break;
            }
        }
        if (page_idx >= 0) break;
        obj = lv_obj_get_parent(obj);
    }
    
    if (page_idx < 0) {
        LOG_W(Log::UI, "input_focus_handler: page not found for ta=%p", ta);
        return;
    }
    lv_obj_t *parent = page_objs[page_idx];
    LOG_I(Log::UI, "input_focus_handler: page_idx=%d parent=%p", page_idx, parent);
    
    // Create keyboard for this page if needed
    if (!g_keyboards[page_idx]) {
        g_keyboards[page_idx] = lv_keyboard_create(parent);
        lv_obj_add_event_cb(g_keyboards[page_idx], keyboard_event_handler, LV_EVENT_ALL, nullptr);
        lv_obj_set_size(g_keyboards[page_idx], lv_pct(FULL_SIZE_PCT), lv_pct(KEYBOARD_HEIGHT_PCT));
        lv_obj_align(g_keyboards[page_idx], LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_text_font(g_keyboards[page_idx], UI::Font::get(), LV_PART_ITEMS);
    }
    
    lv_keyboard_set_textarea(g_keyboards[page_idx], ta);
    lv_obj_clear_flag(g_keyboards[page_idx], LV_OBJ_FLAG_HIDDEN);
}

uint32_t parse_color(const char *s) {
    if (!s) return 0xFFFFFF;
    if (*s == '#') s++;
    
    size_t len = strlen(s);
    uint32_t color = strtol(s, nullptr, 16);
    
    // Handle short form #RGB -> #RRGGBB
    if (len == 3) {
        uint32_t r = (color >> 8) & 0xF;
        uint32_t g = (color >> 4) & 0xF;
        uint32_t b = color & 0xF;
        color = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    }
    
    return color;
}

// Parse coordinate - supports pixels and percentages, with float values
// Examples: "5" "5px" "5.7" "5.7px" "12.5%" 
// Returns rounded integer pixels
int32_t parse_coord_w(const char *s) {
    if (!s || !s[0]) return 0;
    
    // Parse as float to handle decimals
    char* end = nullptr;
    float val = strtof(s, &end);
    
    // Check suffix
    if (end && *end == '%') {
        // Percentage of width, with rounding
        return (int32_t)(val * SCREEN_WIDTH / 100.0f + 0.5f);
    }
    // "px" suffix or just number - round to int
    return (int32_t)(val + 0.5f);
}

// Height variant: % calculated from SCREEN_HEIGHT
int32_t parse_coord_h(const char *s) {
    if (!s || !s[0]) return 0;
    
    char* end = nullptr;
    float val = strtof(s, &end);
    
    if (end && *end == '%') {
        return (int32_t)(val * SCREEN_HEIGHT / 100.0f + 0.5f);
    }
    return (int32_t)(val + 0.5f);
}

// Convenience: read coordinate attribute (px or %) - width based
static int32_t getAttrCoordW(const char* s, const char* e, const char* name, int32_t def = 0) {
    char buf[32];
    if (!getAttr(s, e, name, buf, sizeof(buf))) return def;
    return parse_coord_w(buf);
}

// Convenience: read coordinate attribute (px or %) - height based
static int32_t getAttrCoordH(const char* s, const char* e, const char* name, int32_t def = 0) {
    char buf[32];
    if (!getAttr(s, e, name, buf, sizeof(buf))) return def;
    return parse_coord_h(buf);
}

// Default (backward compat) - uses width
static int32_t getAttrCoord(const char* s, const char* e, const char* name, int32_t def = 0) {
    return getAttrCoordW(s, e, name, def);
}

// Width/height variants with "has" flag
static int32_t getAttrCoordW(const char* s, const char* e, const char* name, bool& found) {
    char buf[32];
    found = getAttr(s, e, name, buf, sizeof(buf));
    return found ? parse_coord_w(buf) : 0;
}

static int32_t getAttrCoordH(const char* s, const char* e, const char* name, bool& found) {
    char buf[32];
    found = getAttr(s, e, name, buf, sizeof(buf));
    return found ? parse_coord_h(buf) : 0;
}

static void set_pos(lv_obj_t *obj, int32_t x, int32_t y) {
    lv_obj_set_x(obj, x);
    lv_obj_set_y(obj, y);
}

static void set_size(lv_obj_t *obj, int32_t w, int32_t h) {
    if (w != 0) lv_obj_set_width(obj, w);
    if (h != 0) lv_obj_set_height(obj, h);
}

// Parse alignment string "h v" or just "h"
// h: left, center, right
// v: top, center, bottom
struct AlignPair {
    P::String h = "left";
    P::String v = "top";
};

static AlignPair parseAlignPair(const P::String& align) {
    AlignPair result;
    if (align.empty()) return result;
    
    // Find space separator
    size_t space = align.find(' ');
    if (space != P::String::npos) {
        result.h = align.substr(0, space);
        result.v = align.substr(space + 1);
    } else {
        result.h = align;
    }
    return result;
}

// Get LVGL alignment constant from h/v pair
static lv_align_t getLvAlign(const P::String& h, const P::String& v) {
    // 3x3 matrix of alignments
    if (v == "top" || v.empty()) {
        if (h == "left" || h.empty()) return LV_ALIGN_TOP_LEFT;
        if (h == "center") return LV_ALIGN_TOP_MID;
        if (h == "right") return LV_ALIGN_TOP_RIGHT;
    } else if (v == "center") {
        if (h == "left" || h.empty()) return LV_ALIGN_LEFT_MID;
        if (h == "center") return LV_ALIGN_CENTER;
        if (h == "right") return LV_ALIGN_RIGHT_MID;
    } else if (v == "bottom") {
        if (h == "left" || h.empty()) return LV_ALIGN_BOTTOM_LEFT;
        if (h == "center") return LV_ALIGN_BOTTOM_MID;
        if (h == "right") return LV_ALIGN_BOTTOM_RIGHT;
    }
    return LV_ALIGN_TOP_LEFT;
}

// Apply vertical text alignment via padding
static void applyTextValign(lv_obj_t* lbl, const P::String& valign, int32_t h) {
    if (valign.empty() || valign == "top" || h <= 0) return;
    
    const lv_font_t* font = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
    if (!font) font = lv_font_default();
    int lineHeight = lv_font_get_line_height(font);
    
    // Account for existing bottom padding (e.g. from CSS)
    int padBottom = lv_obj_get_style_pad_bottom(lbl, LV_PART_MAIN);
    int available = h - padBottom;
    
    if (valign == "center") {
        int padTop = (available - lineHeight) / 2;
        if (padTop > 0) {
            lv_obj_set_style_pad_top(lbl, padTop, LV_PART_MAIN);
        }
    } else if (valign == "bottom") {
        int padTop = available - lineHeight;
        if (padTop > 0) {
            lv_obj_set_style_pad_top(lbl, padTop, LV_PART_MAIN);
        }
    }
}

void create_label(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    
    // Element positioning: align="h v" or align="h" + valign="v"
    auto align = getAttr(astart, aend, "align");
    auto valignPos = getAttr(astart, aend, "valign");
    
    // Text alignment: text-align="h v" or text-align="h" + text-valign="v"  
    auto textAlign = getAttr(astart, aend, "text-align");
    auto textValign = getAttr(astart, aend, "text-valign");
    
    bool hasX, hasY, hasW, hasH;
    int32_t x = getAttrCoordW(astart, aend, "x", hasX);
    int32_t y = getAttrCoordH(astart, aend, "y", hasY);
    int32_t w = getAttrCoordW(astart, aend, "w", hasW);
    int32_t h = getAttrCoordH(astart, aend, "h", hasH);
    
    auto text = trimmed(content);
    bool hasDynamicText = contains(text, '{');
    
    // Check for dynamic color bindings
    P::String colorAttr = getAttr(astart, aend, "color");
    P::String bgcolorAttr = getAttr(astart, aend, "bgcolor");
    bool hasDynamicColor = contains(colorAttr, '{');
    bool hasDynamicBgcolor = contains(bgcolorAttr, '{');
    
    ensureId(attrs, "_lbl", hasDynamicText || hasDynamicColor || hasDynamicBgcolor);
    
    // Parse static visual properties
    int fontSize  = getAttrInt(astart, aend, "font");
    int radiusVal = getAttrInt(astart, aend, "radius");
    
    P::String rendered = render_template(text.c_str());
    
    // Create via Label widget — handles LVGL creation + static visuals
    Label widget = {
        .text    = rendered.c_str(),
        .font    = fontSize,
        .color   = (!colorAttr.empty() && !hasDynamicColor) ? parse_color(colorAttr.c_str()) : NO_COLOR,
        .bgcolor = (!bgcolorAttr.empty() && !hasDynamicBgcolor) ? parse_color(bgcolorAttr.c_str()) : NO_COLOR,
        .radius  = radiusVal,
    };
    widget.create(parent);
    auto *lbl = widget.handle;
    
    // Apply CSS (tag, #id, .class cascade)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{lbl}.applyCss("label", attrs.id.c_str(), renderedClass.c_str());
    }
    
    // === ELEMENT POSITIONING (HTML-specific: align/valign + x/y combinations) ===
    bool useAlign = !align.empty() || !valignPos.empty();
    
    if (useAlign && !hasX && !hasY) {
        AlignPair pos = parseAlignPair(align);
        if (!valignPos.empty()) pos.v = valignPos;
        lv_obj_align(lbl, getLvAlign(pos.h, pos.v), 0, 0);
    } else if (useAlign && !hasX) {
        AlignPair pos = parseAlignPair(align);
        lv_align_t lvAlign = LV_ALIGN_TOP_LEFT;
        if (pos.h == "center") lvAlign = LV_ALIGN_TOP_MID;
        else if (pos.h == "right") lvAlign = LV_ALIGN_TOP_RIGHT;
        lv_obj_align(lbl, lvAlign, 0, y);
    } else if (useAlign && !hasY) {
        AlignPair pos = parseAlignPair(align);
        if (!valignPos.empty()) pos.v = valignPos;
        if (pos.v == "center") {
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, x, 0);
        } else if (pos.v == "bottom") {
            lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, x, 0);
        } else {
            set_pos(lbl, x, 0);
        }
    } else {
        // Only set position if explicitly specified in HTML
        if (hasX || hasY) set_pos(lbl, x, y);
    }
    
    if (w != 0 || h != 0) {
        set_size(lbl, w, h);
        if (w != 0) {
            auto overflow = getAttr(astart, aend, "overflow");
            if (overflow == "ellipsis" || overflow == "dot") {
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            } else if (overflow == "clip") {
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
            } else if (overflow == "scroll") {
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            } else {
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            }
        }
    }
    
    // === TEXT ALIGNMENT INSIDE ===
    AlignPair textPos = parseAlignPair(textAlign);
    if (!textValign.empty()) textPos.v = textValign;
    
    if (!textPos.h.empty() && textPos.h != "left") {
        lv_text_align_t hAlign = LV_TEXT_ALIGN_LEFT;
        if (textPos.h == "center") hAlign = LV_TEXT_ALIGN_CENTER;
        else if (textPos.h == "right") hAlign = LV_TEXT_ALIGN_RIGHT;
        lv_obj_set_style_text_align(lbl, hAlign, LV_PART_MAIN);
    }
    
    // Vertical text alignment (font already applied by CSS at this point)
    // Default to "center" when height is set and no explicit text-valign
    if (hasH && h > 0) {
        // Check if user explicitly set vertical text alignment
        bool hasExplicitValign = !textValign.empty() || 
            (textAlign.find(' ') != P::String::npos); // "h v" form
        P::String vAlign = hasExplicitValign ? textPos.v : P::String("center");
        applyTextValign(lbl, vAlign, h);
    }
    
    // Dynamic color bindings — initial values from state
    if (hasDynamicColor) {
        P::String varName = extractBindVar(colorAttr.c_str());
        P::String initVal = State::store().getString(varName);
        if (!initVal.empty()) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(parse_color(initVal.c_str())), LV_PART_MAIN);
        }
    }
    if (hasDynamicBgcolor) {
        P::String varName = extractBindVar(bgcolorAttr.c_str());
        P::String initVal = State::store().getString(varName);
        if (!initVal.empty()) {
            lv_obj_set_style_bg_color(lbl, lv_color_hex(parse_color(initVal.c_str())), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
    
    if (!attrs.id.empty()) {
        ElementDesc d;
        d.id          = attrs.id.c_str();
        d.obj         = lbl;
        d.tpl         = text.c_str();
        d.classTpl    = attrs.hasDynamicClass   ? attrs.cssClass.c_str() : nullptr;
        d.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
        d.bgcolorBind = hasDynamicBgcolor        ? bgcolorAttr.c_str()    : nullptr;
        d.colorBind   = hasDynamicColor          ? colorAttr.c_str()      : nullptr;
        d.zIndex      = attrs.zIndex;
        store_element(d);
    }
}

// ── Flatten nested tags inside button content ──────────────────────
// If content is  <label font="22" color="#FFF">7</label>  → extract "7" + font/color
// If multiple labels → strip HTML, concatenate text
struct FlatContent {
    P::String text;
    int       font  = 0;       // extracted from nested label (0 = not set)
    P::String color;            // extracted from nested label
    P::String textAlign;        // extracted from nested label
};

static FlatContent flattenButtonContent(const char* content) {
    FlatContent result;
    P::String raw = trimmed(content);
    
    // No HTML tags? Return as-is
    if (raw.find('<') == P::String::npos) {
        result.text = raw;
        return result;
    }
    
    // Count <label occurrences
    int labelCount = 0;
    const char* scan = raw.c_str();
    while ((scan = strstr(scan, "<label")) != nullptr) {
        char c = scan[6]; // char after "<label"
        if (c == '>' || c == ' ' || c == '/') labelCount++;
        scan++;
    }
    
    if (labelCount == 1) {
        // Single <label>: extract text content + font/color attributes
        const char* lstart = strstr(raw.c_str(), "<label");
        if (lstart) {
            const char* attrStart = lstart + 6; // skip "<label"
            const char* gt = strchr(attrStart, '>');
            if (gt) {
                // Extract attributes from the label tag
                result.font = getAttrInt(attrStart, gt, "font");
                result.color = getAttr(attrStart, gt, "color");
                result.textAlign = getAttr(attrStart, gt, "text-align");
                if (result.textAlign.empty()) {
                    result.textAlign = getAttr(attrStart, gt, "align");
                }
                
                // Extract inner text
                const char* textStart = gt + 1;
                const char* lclose = strstr(textStart, "</label>");
                if (lclose) {
                    result.text = trimmed(P::String(textStart, lclose - textStart));
                }
            }
        }
    } else {
        // Multiple tags or non-label tags: strip all HTML, keep text
        P::String stripped;
        bool inTag = false;
        for (char c : raw) {
            if (c == '<') { inTag = true; continue; }
            if (c == '>') { inTag = false; continue; }
            if (!inTag) stripped += c;
        }
        result.text = trimmed(stripped);
    }
    
    return result;
}

void create_button(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto href = getAttr(astart, aend, "href");
    auto onclick = getAttr(astart, aend, "onclick");
    auto onhold = getAttr(astart, aend, "onhold");
    auto align = getAttr(astart, aend, "align");
    auto iconAttr = getAttr(astart, aend, "icon");
    
    int32_t x = getAttrCoordW(astart, aend, "x");
    int32_t y = getAttrCoordH(astart, aend, "y");
    int32_t w = getAttrCoordW(astart, aend, "w");
    int32_t h = getAttrCoordH(astart, aend, "h");
    
    // Flatten nested <label> inside button content
    auto flat = flattenButtonContent(content);
    auto text = flat.text;
    bool hasDynamicText = contains(text, '{');
    bool hasIcon = !iconAttr.empty();
    bool hasText = !text.empty();
    
    // Check for dynamic color bindings
    P::String colorAttr = getAttr(astart, aend, "color");
    P::String bgcolorAttr = getAttr(astart, aend, "bgcolor");
    bool hasDynamicColor = contains(colorAttr, '{');
    bool hasDynamicBgcolor = contains(bgcolorAttr, '{');
    
    // Button always needs id for click handling or dynamic bindings
    ensureId(attrs, "_btn", hasDynamicText || !href.empty() || !onclick.empty() || !onhold.empty() || hasDynamicColor || hasDynamicBgcolor);
    
    // Parse static visual properties
    int radiusVal = getAttrInt(astart, aend, "radius");
    int fontSize  = getAttrInt(astart, aend, "font");
    int iconSize  = getAttrInt(astart, aend, "iconsize", 24);
    
    // Merge font/color from nested <label> if button didn't set them
    if (fontSize == 0 && flat.font > 0) fontSize = flat.font;
    if (colorAttr.empty() && !flat.color.empty()) colorAttr = flat.color;
    
    // Prepare icon path with proper lifetime (s_iconPaths persists until ui_clear)
    const char* iconPath = nullptr;
    if (hasIcon) {
        P::String resolvedPath = resolve_resource_path(iconAttr);
        s_iconPaths.push_back("C:" + resolvedPath);
        iconPath = s_iconPaths.back().c_str();
        LOG_D(Log::UI, "Button icon: %s", iconPath);
    }
    
    // Render text template
    P::String renderedText;
    if (hasText) {
        renderedText = hasDynamicText ? render_template(text.c_str()) : decodeEntities(text);
    }
    
    // Determine static bgcolor (dynamic bgcolor applied after creation)
    uint32_t staticBgcolor = NO_COLOR;
    if (!bgcolorAttr.empty() && !hasDynamicBgcolor) {
        staticBgcolor = parse_color(bgcolorAttr.c_str());
    }
    
    // Create via Button widget struct (handles LVGL creation, icon+text flex, press transform)
    // NOTE: Don't pass bgcolor here - we apply CSS first, then local override
    Button widget = {
        .text       = hasText ? renderedText.c_str() : "",
        .iconPath   = iconPath ? iconPath : "",
        .iconSize   = iconSize,
        .font       = fontSize,    // Apply font at creation, before CSS
        .bgcolor    = NO_COLOR,  // Will be set after CSS
        .radius     = radiusVal,
        .forceLabel = hasDynamicText,  // ensure label exists for {binding} updates
    };
    widget.create(parent);
    auto *btn = widget.handle;
    
    _unique_id(btn, attrs.id.c_str());  // For testing mock
    // === HTML-specific positioning (overrides struct defaults) ===
    if (align == "center") {
        if (w != 0 || h != 0) set_size(btn, w, h);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    } else {
        set_pos(btn, x, y);
        if (w != 0 || h != 0) set_size(btn, w, h);
    }
    
    lv_obj_set_ext_click_area(btn, ClickArea::BUTTON);
    
    // 1. CSS cascade (tag < .class < tag.class < #id)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        widget.applyCss("button", attrs.id.c_str(), renderedClass.c_str());
    }
    
    // 2. Local bgcolor override (static or dynamic)
    if (!bgcolorAttr.empty()) {
        if (hasDynamicBgcolor) {
            P::String varName = extractBindVar(bgcolorAttr.c_str());
            P::String initVal = State::store().getString(varName);
            if (!initVal.empty()) {
                UI::setBgColor(widget, parse_color(initVal.c_str()));
            }
        } else {
            UI::setBgColor(widget, staticBgcolor);
        }
    }
    
    // Apply HTML default font on label, then override if font attr specified
    if (widget.label.handle) {
        lv_obj_set_style_text_font(widget.label.handle, UI::Font::get(), LV_PART_MAIN);
        if (fontSize > 0) {
            widget.label.applyFont(fontSize);
        }
    }
    
    // Text color — static or dynamic
    if (!colorAttr.empty() && widget.label.handle) {
        if (hasDynamicColor) {
            P::String varName = extractBindVar(colorAttr.c_str());
            P::String initVal = State::store().getString(varName);
            if (!initVal.empty()) {
                UI::setColor(widget.label, parse_color(initVal.c_str()));
            }
        } else {
            UI::setColor(widget.label, parse_color(colorAttr.c_str()));
        }
    }
    
    // Store: obj=label (text updates) or icon, parentObj=button
    lv_obj_t* storeObj = widget.label.handle ? widget.label.handle : (widget.icon.handle ? widget.icon.handle : btn);
    ElementDesc bd;
    bd.id          = attrs.id.c_str();
    bd.obj         = storeObj;
    bd.href        = href.c_str();
    bd.onclick     = onclick.c_str();
    bd.tpl         = (hasDynamicText && widget.label.handle) ? text.c_str() : nullptr;
    bd.classTpl    = attrs.hasDynamicClass   ? attrs.cssClass.c_str() : nullptr;
    bd.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    bd.bgcolorBind = hasDynamicBgcolor        ? bgcolorAttr.c_str()    : nullptr;
    bd.colorBind   = hasDynamicColor          ? colorAttr.c_str()      : nullptr;
    bd.zIndex      = attrs.zIndex;
    int idx = store_element(bd);
    
    if (idx >= 0 && idx < (int)elements.size()) {
        elements[idx]->parentObj = btn;
        elements[idx]->onhold = onhold;
        
        // For buttons, apply visibility to the button, not the label
        if (!elements[idx]->visibleBind.empty()) {
            P::String visVal = State::store().getString(elements[idx]->visibleBind);
            bool visible = (visVal == "true" || visVal == "1");
            if (!visible) {
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(storeObj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    if (!href.empty() || !onclick.empty()) {
        widget.on(LV_EVENT_CLICKED, button_click_handler, idx);
    }
    
    if (!onhold.empty()) {
        widget.on(LV_EVENT_LONG_PRESSED, button_click_handler, idx);
    }
}

void create_switch(const char *astart, const char *aend, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto bind = getAttr(astart, aend, "bind");
    auto onchange = getAttr(astart, aend, "onchange");
    
    int32_t x = getAttrCoordW(astart, aend, "x");
    int32_t y = getAttrCoordH(astart, aend, "y");
    
    ensureId(attrs, "_sw", !bind.empty() || !onchange.empty());
    
    // Determine initial checked state from binding
    bool checked = false;
    if (!bind.empty()) {
        if (auto *init_val = get_state_value(bind.c_str()); init_val && toBool(init_val)) {
            checked = true;
        }
    }
    
    // Create via Switch widget struct
    Switch widget = { .checked = checked };
    widget.create(parent);
    auto *sw = widget.handle;
    
    set_pos(sw, x, y);
    lv_obj_set_ext_click_area(sw, ClickArea::SWITCH);
    
    // Apply CSS (always — tag/id selectors don't need class)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{sw}.applyCss("switch", attrs.id.c_str(), renderedClass.c_str());
    }
    
    ElementDesc sd;
    sd.id          = attrs.id.c_str();
    sd.obj         = sw;
    sd.onchange    = onchange.c_str();
    sd.bind        = bind.c_str();
    sd.visibleBind = attrs.hasDynamicVisible ? attrs.visible.c_str() : nullptr;
    sd.zIndex      = attrs.zIndex;
    int idx = store_element(sd);
    
    widget.on(LV_EVENT_VALUE_CHANGED, switch_event_handler, idx);
}

void create_slider(const char *astart, const char *aend, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto bind = getAttr(astart, aend, "bind");
    auto onchange = getAttr(astart, aend, "onchange");
    
    int32_t x = getAttrCoordW(astart, aend, "x");
    int32_t y = getAttrCoordH(astart, aend, "y");
    int32_t w = getAttrCoordW(astart, aend, "w", Defaults::Slider::WIDTH);
    int min_val = getAttrInt(astart, aend, "min", Defaults::Slider::MIN);
    int max_val = getAttrInt(astart, aend, "max", Defaults::Slider::MAX);
    
    ensureId(attrs, "_sl", !bind.empty() || !onchange.empty());
    
    // Determine initial value from binding
    int initValue = 0;
    if (!bind.empty()) {
        if (auto *init_val = get_state_value(bind.c_str())) {
            initValue = atoi(init_val);
        }
    }
    
    // Create via Slider widget struct
    Slider widget = {
        .min_val = min_val,
        .max_val = max_val,
        .value   = initValue,
        .w       = w,
    };
    widget.create(parent);
    auto *slider = widget.handle;
    
    set_pos(slider, x, y);
    lv_obj_set_ext_click_area(slider, ClickArea::SLIDER);
    
    // Apply CSS (always — tag/id selectors don't need class)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{slider}.applyCss("slider", attrs.id.c_str(), renderedClass.c_str());
    }
    
    ElementDesc sld;
    sld.id          = attrs.id.c_str();
    sld.obj         = slider;
    sld.onchange    = onchange.c_str();
    sld.bind        = bind.c_str();
    sld.visibleBind = attrs.hasDynamicVisible ? attrs.visible.c_str() : nullptr;
    sld.zIndex      = attrs.zIndex;
    int idx = store_element(sld);
    
    widget.on(LV_EVENT_VALUE_CHANGED, slider_event_handler, idx);
    widget.on(LV_EVENT_RELEASED, slider_event_handler, idx);
}

void create_input(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto bind = getAttr(astart, aend, "bind");
    auto onchange = getAttr(astart, aend, "onchange");
    auto onenter = getAttr(astart, aend, "onenter");
    auto onblur = getAttr(astart, aend, "onblur");
    auto placeholder = getAttr(astart, aend, "placeholder");
    auto bgcolorAttr = getAttr(astart, aend, "bgcolor");
    auto colorAttr = getAttr(astart, aend, "color");
    
    int32_t x = getAttrCoordW(astart, aend, "x");
    int32_t y = getAttrCoordH(astart, aend, "y");
    int32_t w = getAttrCoordW(astart, aend, "w", Defaults::Input::WIDTH);
    int32_t h = getAttrCoordH(astart, aend, "h", Defaults::Input::HEIGHT);
    
    ensureId(attrs, "_inp", !bind.empty() || !onchange.empty());
    
    // Determine initial text from binding
    const char* initText = "";
    if (!bind.empty()) {
        if (auto *init_val = get_state_value(bind.c_str()); init_val && init_val[0]) {
            initText = init_val;
        }
    }
    
    // Create via Input widget struct
    auto multilineAttr = getAttr(astart, aend, "multiline");
    bool isMultiline = (multilineAttr == "true" || multilineAttr == "1");
    auto passwordAttr = getAttr(astart, aend, "password");
    bool isPassword = (passwordAttr == "true" || passwordAttr == "1");
    
    Input widget = {
        .placeholder = placeholder.c_str(),
        .text        = initText,
        .password    = isPassword,
        .multiline   = isMultiline,
    };
    widget.create(parent);
    auto *ta = widget.handle;
    
    // Apply bgcolor: explicit or transparent
    if (!bgcolorAttr.empty()) {
        uint32_t bgc = parse_color(bgcolorAttr.c_str());
        lv_obj_set_style_bg_color(ta, lv_color_hex(bgc), 0);
        lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, 0);
    }
    
    // Apply text color
    if (!colorAttr.empty()) {
        uint32_t c = parse_color(colorAttr.c_str());
        lv_obj_set_style_text_color(ta, lv_color_hex(c), 0);
    }
    
    set_pos(ta, x, y);
    set_size(ta, w, h);
    
    // Apply CSS (always — tag/id selectors don't need class)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{ta}.applyCss("input", attrs.id.c_str(), renderedClass.c_str());
    }
    
    ElementDesc ind;
    ind.id          = attrs.id.c_str();
    ind.obj         = ta;
    ind.onchange    = onchange.c_str();
    ind.bind        = bind.c_str();
    ind.visibleBind = attrs.hasDynamicVisible ? attrs.visible.c_str() : nullptr;
    ind.zIndex      = attrs.zIndex;
    int idx = store_element(ind);
    
    widget.on(LV_EVENT_VALUE_CHANGED, input_event_handler, idx);
    widget.on(LV_EVENT_FOCUSED, input_focus_handler, idx);
    widget.on(LV_EVENT_DEFOCUSED, input_defocus_handler, idx);
    widget.on(LV_EVENT_DEFOCUSED, input_complete_handler, idx);
}

// Image element - displays PNG image with optional click handler
void create_image(const char *astart, const char *aend, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto src = getAttr(astart, aend, "src");
    auto onclick = getAttr(astart, aend, "onclick");
    auto align = getAttr(astart, aend, "align");
    
    int32_t x = getAttrCoordW(astart, aend, "x");
    int32_t y = getAttrCoordH(astart, aend, "y");
    int32_t w = getAttrCoordW(astart, aend, "w");
    int32_t h = getAttrCoordH(astart, aend, "h");
    
    P::String imgPath = resolve_resource_path(src);
    
    // Using global s_imagePaths (cleared on ui_clear)
    s_imagePaths.push_back("C:" + imgPath);
    const char* lvglPath = s_imagePaths.back().c_str();
    
    // Create via Image widget struct
    Image widget = { .src = lvglPath };
    widget.create(parent);
    auto *img = widget.handle;
    
    if (align == "center") {
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, y);
    } else {
        set_pos(img, x, y);
    }
    
    if (w > 0 && h > 0) {
        lv_obj_set_size(img, w, h);
    }
    
    // Make clickable if onclick specified
    if (!onclick.empty()) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(img, ClickArea::IMAGE);
        ensureId(attrs, "_img", true);
        
        ElementDesc imd;
        imd.id          = attrs.id.c_str();
        imd.obj         = img;
        imd.onclick     = onclick.c_str();
        imd.visibleBind = attrs.hasDynamicVisible ? attrs.visible.c_str() : nullptr;
        imd.zIndex      = attrs.zIndex;
        int idx = store_element(imd);
        widget.on(LV_EVENT_CLICKED, button_click_handler, idx);
    } else if (!attrs.id.empty() || attrs.hasDynamicVisible) {
        ensureId(attrs, "_img", false);
        ElementDesc imd2;
        imd2.id          = attrs.id.c_str();
        imd2.obj         = img;
        imd2.visibleBind = attrs.hasDynamicVisible ? attrs.visible.c_str() : nullptr;
        imd2.zIndex      = attrs.zIndex;
        store_element(imd2);
    }
}

// Canvas touch handler
static void canvas_touch_handler(lv_event_t* e) {
    lv_obj_t* canvas = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (idx < 0 || idx >= (int)elements.size()) return;
    auto& elem = elements[idx];
    if (!elem->is_canvas) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    
    // Reset last position on new touch start
    if (code == LV_EVENT_PRESSED) {
        state_store_set_silent("lastX", "-1");
        state_store_set_silent("lastY", "-1");
    }
    
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    
    // Get canvas position
    int32_t canvas_x = lv_obj_get_x(canvas);
    int32_t canvas_y = lv_obj_get_y(canvas);
    
    lv_obj_t* parent = lv_obj_get_parent(canvas);
    if (parent) {
        canvas_x += lv_obj_get_x(parent);
        canvas_y += lv_obj_get_y(parent);
    }
    
    int32_t local_x = point.x - canvas_x;
    int32_t local_y = point.y - canvas_y;
    
    // Clamp to canvas bounds
    if (local_x < 0 || local_x >= elem->canvasWidth) return;
    if (local_y < 0 || local_y >= elem->canvasHeight) return;
    
    // Call ontap callback on CLICKED (single tap)
    if (code == LV_EVENT_CLICKED && !elem->ontap.empty() && g_ontap_handler) {
        g_ontap_handler(elem->ontap.c_str(), (int)local_x, (int)local_y);
    }
    
    // Call onhold callback on LONG_PRESSED (with coordinates for canvas)
    if (code == LV_EVENT_LONG_PRESSED && !elem->onhold.empty() && g_onhold_xy_handler) {
        g_onhold_xy_handler(elem->onhold.c_str(), (int)local_x, (int)local_y);
    }
    
    // Call ondraw callback on PRESSED/PRESSING (for drawing)
    if ((code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) && 
        !elem->onclick.empty() && g_onclick_handler) {
        char xbuf[16], ybuf[16];
        snprintf(xbuf, sizeof(xbuf), "%d", (int)local_x);
        snprintf(ybuf, sizeof(ybuf), "%d", (int)local_y);
        state_store_set_silent("_touchX", xbuf);
        state_store_set_silent("_touchY", ybuf);
        g_onclick_handler(elem->onclick.c_str());
    }
}

void create_canvas(const char *astart, const char *aend, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto ondraw = getAttr(astart, aend, "ondraw");
    auto ontap = getAttr(astart, aend, "ontap");
    auto onhold = getAttr(astart, aend, "onhold");
    auto bgcolor = getAttr(astart, aend, "bgcolor");
    
    int32_t x = getAttrCoordW(astart, aend, "x");
    int32_t y = getAttrCoordH(astart, aend, "y");
    int32_t w = getAttrCoordW(astart, aend, "w", 200);
    int32_t h = getAttrCoordH(astart, aend, "h", 200);
    
    ensureId(attrs, "_canvas", true);
    
    auto *canvas = lv_canvas_create(parent);
    set_pos(canvas, x, y);
    
    // Allocate buffer in PSRAM (ARGB8888 = 4 bytes per pixel)
    size_t buf_size = w * h * 4;
    uint8_t *buf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    
    if (!buf) {
        LOG_E(Log::UI, "canvas: Failed to allocate %d bytes in PSRAM", (int)buf_size);
        lv_obj_del(canvas);
        return;
    }
    
    // Fill buffer with background color
    uint32_t bgColorRgb = 0xFFFFFF;
    if (!bgcolor.empty()) {
        bgColorRgb = parse_color(bgcolor.c_str());
    }
    uint32_t fillColor = rgb_to_native(bgColorRgb);
    
    uint32_t* buf32 = (uint32_t*)buf;
    for (int i = 0; i < w * h; i++) {
        buf32[i] = fillColor;
    }
    
    lv_canvas_set_buffer(canvas, buf, w, h, LV_COLOR_FORMAT_ARGB8888);
    
    // Store element with canvas info
    auto elem = P::create<UI::Element>();
    elem->id = attrs.id;
    elem->w.handle = canvas;
    elem->onclick = ondraw;  // ondraw uses onclick field for continuous drawing
    elem->ontap = ontap;     // ontap for single tap with coordinates
    elem->onhold = onhold;   // onhold for long press with coordinates
    elem->canvasBuffer = buf;
    elem->canvasWidth = w;
    elem->canvasHeight = h;
    elem->is_canvas = true;
    
    // Handle visibility binding
    if (attrs.hasDynamicVisible) {
        P::String vb = attrs.visible;
        if (vb.length() > 2 && vb[0] == '{' && vb.back() == '}') {
            elem->visibleBind = vb.substr(1, vb.length() - 2);
        } else {
            elem->visibleBind = vb;
        }
        
        if (!elem->visibleBind.empty()) {
            P::String visVal = State::store().getString(elem->visibleBind);
            bool visible = (visVal == "true" || visVal == "1");
            if (!visible) {
                lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    int idx = elements.size();
    elements.push_back(std::move(elem));
    
    // Add touch handler if ondraw, ontap or onhold specified
    if (!ondraw.empty() || !ontap.empty() || !onhold.empty()) {
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        Widget wb{canvas};
        wb.on(LV_EVENT_PRESSED, canvas_touch_handler, idx);
        wb.on(LV_EVENT_PRESSING, canvas_touch_handler, idx);
        wb.on(LV_EVENT_CLICKED, canvas_touch_handler, idx);
        wb.on(LV_EVENT_LONG_PRESSED, canvas_touch_handler, idx);
    }
}

// ============ MARKDOWN ============

#if LV_USE_SPAN

void create_markdown(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
    auto text = trimmed(content);
    bool hasDynamicText = contains(text, '{');
    
    CommonAttrs attrs = parseCommonAttrs(astart, aend);
    ensureId(attrs, "_md", hasDynamicText);
    
    P::String rendered = hasDynamicText ? render_template(text.c_str()) : decodeEntities(text);
    
    // Parse position attrs (same pattern as create_label)
    auto align = getAttr(astart, aend, "align");
    auto valignPos = getAttr(astart, aend, "valign");
    bool hasX, hasY, hasW, hasH;
    int32_t x = getAttrCoordW(astart, aend, "x", hasX);
    int32_t y = getAttrCoordH(astart, aend, "y", hasY);
    int32_t w = getAttrCoordW(astart, aend, "w", hasW);
    int32_t h = getAttrCoordH(astart, aend, "h", hasH);
    
    // Parse optional color attributes
    P::String colorAttr = getAttr(astart, aend, "color");
    P::String bgcolorAttr = getAttr(astart, aend, "bgcolor");
    P::String h1colorAttr = getAttr(astart, aend, "h1color");
    P::String h2colorAttr = getAttr(astart, aend, "h2color");
    P::String accentAttr = getAttr(astart, aend, "accent");
    P::String codecolorAttr = getAttr(astart, aend, "codecolor");
    
    Markdown md;
    if (!colorAttr.empty()) md.color = parse_color(colorAttr.c_str());
    if (!h1colorAttr.empty()) md.h1Color = parse_color(h1colorAttr.c_str());
    if (!h2colorAttr.empty()) md.h2Color = parse_color(h2colorAttr.c_str());
    if (!accentAttr.empty()) md.accentColor = parse_color(accentAttr.c_str());
    if (!codecolorAttr.empty()) md.codeColor = parse_color(codecolorAttr.c_str());
    if (!bgcolorAttr.empty()) md.bgcolor = parse_color(bgcolorAttr.c_str());
    
    md.create(parent);
    lv_obj_t* spangroup = md.handle;
    
    // Position (same logic as label)
    bool useAlign = !align.empty() || !valignPos.empty();
    if (useAlign && !hasX && !hasY) {
        AlignPair pos = parseAlignPair(align);
        if (!valignPos.empty()) pos.v = valignPos;
        lv_obj_align(spangroup, getLvAlign(pos.h, pos.v), 0, 0);
    } else if (useAlign && !hasX) {
        AlignPair pos = parseAlignPair(align);
        lv_align_t lvAlign = LV_ALIGN_TOP_LEFT;
        if (pos.h == "center") lvAlign = LV_ALIGN_TOP_MID;
        else if (pos.h == "right") lvAlign = LV_ALIGN_TOP_RIGHT;
        lv_obj_align(spangroup, lvAlign, 0, y);
    } else {
        set_pos(spangroup, x, y);
    }
    if (hasW) lv_obj_set_width(spangroup, w);
    if (hasH) lv_obj_set_height(spangroup, h);
    
    // Apply CSS cascade
    P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
    Widget{spangroup}.applyCss("markdown", attrs.id.c_str(), renderedClass.c_str());
    
    // Initial render
    md.render(rendered);
    
    // Store element
    auto elem = P::create<UI::Element>();
    elem->id = attrs.id;
    elem->w.handle = spangroup;
    elem->tpl = hasDynamicText ? P::String(text) : "";
    
    if (hasDynamicText) {
        elem->classTemplate = attrs.hasDynamicClass ? P::String(attrs.cssClass) : "";
        
        // Capture md colors for re-render on update
        uint32_t mdColor = md.color;
        uint32_t mdH1Color = md.h1Color;
        uint32_t mdH2Color = md.h2Color;
        uint32_t mdAccentColor = md.accentColor;
        uint32_t mdDimColor = md.dimColor;
        uint32_t mdCodeColor = md.codeColor;
        
        elem->updateFn = [spangroup, mdColor, mdH1Color, mdH2Color, mdAccentColor, mdDimColor, mdCodeColor](const P::String& text) {
            Markdown renderer;
            renderer.handle = spangroup;
            renderer.color = mdColor;
            renderer.h1Color = mdH1Color;
            renderer.h2Color = mdH2Color;
            renderer.accentColor = mdAccentColor;
            renderer.dimColor = mdDimColor;
            renderer.codeColor = mdCodeColor;
            renderer.render(text);
        };
    }
    
    // Visibility binding
    if (!attrs.visible.empty()) {
        P::String vb = attrs.visible;
        if (vb.length() > 2 && vb[0] == '{' && vb.back() == '}') {
            elem->visibleBind = vb.substr(1, vb.length() - 2);
        } else {
            elem->visibleBind = vb;
        }
        if (!elem->visibleBind.empty()) {
            P::String visVal = State::store().getString(elem->visibleBind);
            bool visible = (visVal == "true" || visVal == "1");
            if (!visible) {
                lv_obj_add_flag(spangroup, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    elements.push_back(std::move(elem));
}

#endif // LV_USE_SPAN
