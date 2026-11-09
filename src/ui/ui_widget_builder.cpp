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
#include "core/core.h"
#include "ui/ui_html_internal.h"
#include "ui/xml_utils.h"
#include "ui/css_parser.h"
#include "ui/css_colors.h"
#include "ui/dom.h"
#include "ui/ui_html_flow.h"
#include "ui/ui_tag_reader.h"
#include "ui_layout.h"
#include "widgets/widgets.h"
#include "widgets/widget_common.h"
#include "widgets/widget_methods.h"
#include "core/state_store.h"
#include "utils/string_utils.h"
#include "utils/log_config.h"
#include <algorithm>
#include <array>
#include <string>
#include <cctype>

#include "esp_heap_caps.h"
#if !defined(LVGL_MOCK_ENABLED)
#include "src/core/lv_obj_event_private.h"  // lv_hit_test_info_t: the point of a hit test and its answer
#endif

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

P::String resolve_resource_path(const P::String& src) {
    if (src.empty()) return src;
    
    // Already absolute path (starts with / or A:)
    if (src[0] == '/' || (src.length() > 1 && src[1] == ':')) {
        return src;
    }
    
    // Relative path - prepend app resources folder
    if (!g_core.app().appPath().empty()) {
        char buf[128]; snprintf(buf, sizeof(buf), "%s/resources/%s", g_core.app().appPath().c_str(), src.c_str());
        return buf;
    }
    
    return src;
}

// ============ Common attribute parsing ============

struct CommonAttrs {
    P::String id;
    P::String cssClass;
    P::String visible;      // "{varname}" for visibility binding
    P::String disabled;     // "{varname}" for disabled binding (or "true"/"1" static)
    bool disabledNow = false;  // disabled from the start: bare (HTML), "true" or "1"
    int zIndex = 0;
    bool hasDynamicClass = false;
    bool hasDynamicVisible = false;
    bool hasDynamicDisabled = false;
};

// A boolean attribute: present without a value (HTML: <input readonly>) or "true"/"1" (TelaOS)
static bool flagAttr(const char* astart, const char* aend, const char* name) {
    const UI::Attrs a(std::string_view(astart, static_cast<size_t>(aend - astart)));
    if (!a.has(name)) return false;
    const P::String v = a.get(name);
    return v.empty() || v == "true" || v == "1" || v == name;
}

static CommonAttrs parseCommonAttrs(const char *astart, const char *aend) {
    CommonAttrs attrs;
    attrs.id = getAttr(astart, aend, "id");
    attrs.cssClass = getAttr(astart, aend, "class");
    attrs.visible = getAttr(astart, aend, "visible");
    attrs.disabled = getAttr(astart, aend, "disabled");
    attrs.hasDynamicClass = contains(attrs.cssClass, '{');
    attrs.hasDynamicVisible = contains(attrs.visible, '{');
    attrs.hasDynamicDisabled = contains(attrs.disabled, '{');
    attrs.disabledNow = !attrs.hasDynamicDisabled && flagAttr(astart, aend, "disabled");
    attrs.zIndex = getAttrInt(astart, aend, "z-index");
    return attrs;
}

// Auto-generate id if needed for dynamic bindings
// Static disabled, disabled="true" / disabled="1" (no binding): grey the widget out at
// creation time. Dynamic "{var}" initial state is handled in addElement().
// ---- The finger's room around a control (ClickArea) ----
// LVGL gives a point to the last control that has it in its room, so the room of a neighbour took
// a tap right on a small button (two browser-sized buttons, one under the other). In HTML a point
// in the room and off the control is the control's only when no other control is right under it.
// Apps without HTML keep LVGL's hit test, and their memory: the check is an event callback each
#if !defined(LVGL_MOCK_ENABLED)
static bool s_onlyRightOn = false;  // while the control right under a point is looked for

static bool isAncestorOf(const lv_obj_t* a, const lv_obj_t* o) {
    for (const lv_obj_t* p = o ? lv_obj_get_parent(o) : nullptr; p; p = lv_obj_get_parent(p)) {
        if (p == a) return true;
    }
    return false;
}

static void roomHitTest(lv_event_t* e) {
    lv_hit_test_info_t* hit = lv_event_get_hit_test_info(e);
    lv_obj_t* self = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_area_t a;
    lv_obj_get_coords(self, &a);
    const lv_point_t* pt = hit->point;
    if (pt->x >= a.x1 && pt->x <= a.x2 && pt->y >= a.y1 && pt->y <= a.y2) return;  // right on it
    if (s_onlyRightOn) {
        hit->res = false;
        return;
    }
    s_onlyRightOn = true;
    lv_point_t p = *hit->point;
    lv_display_t* d = lv_obj_get_display(self);
    lv_obj_t* under = lv_indev_search_obj(lv_display_get_layer_top(d), &p);
    if (!under) under = lv_indev_search_obj(lv_display_get_screen_active(d), &p);
    s_onlyRightOn = false;
    // another control right there takes it; the page or a box this one is in does not
    if (under && under != self && !isAncestorOf(under, self)) hit->res = false;
}
#endif

// html: a control HTML lays out (HtmlFlow::browserSized(), or right in a flow box)
void fingerRoom(lv_obj_t* o, int px, bool html) {
    lv_obj_set_ext_click_area(o, px);
#if !defined(LVGL_MOCK_ENABLED)
    if (!html) return;
    lv_obj_add_flag(o, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(o, roomHitTest, LV_EVENT_HIT_TEST, nullptr);
#else
    (void)html;
#endif
}

static void applyStaticDisabled(lv_obj_t* obj, const CommonAttrs& attrs) {
    if (attrs.disabledNow) lv_obj_add_state(obj, LV_STATE_DISABLED);
}

// State only a few widgets have, kept beside them rather than in every Element
struct SliderStep {      // <input type="range" step>: values min + k·step
    int32_t min = 0;
    int32_t step = 0;
};
struct DropdownChoice {  // <select> with <option disabled>
    P::Array<bool> disabled;
    uint16_t chosen = 0;  // the last option that could be chosen
};
// Never destroyed, as UI::Dom's tree: the widgets that go at the simulator's exit, after the
// statics, still find them
static P::Map<const lv_obj_t*, SliderStep>& s_sliderSteps = *P::create<P::Map<const lv_obj_t*, SliderStep>>().release();
static P::Map<const lv_obj_t*, DropdownChoice>& s_dropdownChoices =
    *P::create<P::Map<const lv_obj_t*, DropdownChoice>>().release();

static void forget_widget_state(lv_event_t* e) {
    const auto* obj = static_cast<const lv_obj_t*>(lv_event_get_user_data(e));
    s_sliderSteps.erase(obj);
    s_dropdownChoices.erase(obj);
}

static void ensureId(CommonAttrs& attrs, const char* prefix, bool hasDynamicContent = false) {
    if (attrs.id.empty() && (hasDynamicContent || attrs.hasDynamicClass || attrs.hasDynamicVisible || attrs.hasDynamicDisabled)) {
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

// Helper: check if text is a quoted literal ("...") and strip quotes
// Returns true if literal (no binding needed), modifies text in-place
static bool stripLiteral(P::String& text) {
    if (text.length() >= 2 && text[0] == '"' && text.back() == '"') {
        // Strip outer quotes
        text = text.substr(1, text.length() - 2);
        // Replace "" -> "
        P::String result;
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                result += '"';
                i++; // skip second "
            } else {
                result += text[i];
            }
        }
        text = result;
        return true;
    }
    return false;
}

static void button_click_handler(lv_event_t *e) {
    g_core.app().beginCallback();
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    LOG_D(Log::UI, "button_event: idx=%d code=%d elements=%d heap=%lu", 
             idx, (int)code, (int)g_core.app().elements.size(), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) {
        LOG_E(Log::UI, "button_event: invalid idx!");
        g_core.app().endCallback();
        return;
    }
    
    LOG_I(Log::UI, "button_event: id=%s onclick='%s' onhold='%s' href='%s'", 
             g_core.app().elements[idx]->id.c_str(), g_core.app().elements[idx]->onclick.c_str(), 
             g_core.app().elements[idx]->onhold.c_str(), g_core.app().elements[idx]->href.c_str());
    
    // Handle LONG_PRESSED -> onhold
    if (code == LV_EVENT_LONG_PRESSED) {
        if (!g_core.app().elements[idx]->onhold.empty() && g_onhold_handler) {
            LOG_I(Log::UI, "onhold: %s()", g_core.app().elements[idx]->onhold.c_str());
            g_onhold_handler(g_core.app().elements[idx]->onhold.c_str());
        }
        g_core.app().endCallback();
        return;
    }
    
    // Handle CLICKED -> onclick
    if (code == LV_EVENT_CLICKED) {
        // Handle onclick first (call Lua function)
        if (!g_core.app().elements[idx]->onclick.empty() && g_onclick_handler) {
            LOG_I(Log::UI, "onclick: %s()", g_core.app().elements[idx]->onclick.c_str());
            g_onclick_handler(g_core.app().elements[idx]->onclick.c_str());
        }
        
        // Then handle href (navigation), unless a listener called preventDefault() (docs/DOM.md)
        if (!g_core.app().elements[idx]->href.empty() && !UI::Dom::defaultPrevented(e)) {
            navigate(g_core.app().elements[idx]->href.c_str());
        }
    }
    
    g_core.app().endCallback();
}

// Switch event handler - UI updates directly
static void switch_event_handler(lv_event_t *e) {
    if (g_core.app().isUpdatingFromBinding()) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) return;
    
    lv_obj_t *sw = g_core.app().elements[idx]->obj();
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    const char *val = checked ? "true" : "false";
    
    // Update state and UI directly
    if (!g_core.app().elements[idx]->bind.empty()) {
        ui_update_bindings(g_core.app().elements[idx]->bind.c_str(), val);
        // Sync Lua state so onchange can read current value
        if (g_state_change_handler) {
            g_state_change_handler(g_core.app().elements[idx]->bind.c_str(), val);
        }
    }
    
    // Call onchange Lua handler
    if (!g_core.app().elements[idx]->onchange.empty() && g_onclick_handler) {
        g_onclick_handler(g_core.app().elements[idx]->onchange.c_str());
    }
    
    LOG_I(Log::UI, "switch %s: %s", g_core.app().elements[idx]->id.c_str(), checked ? "ON" : "OFF");
}

// Slider event handler - live UI updates with throttle
static void slider_event_handler(lv_event_t *e) {
    if (g_core.app().isUpdatingFromBinding()) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) return;
    
    lv_event_code_t code = lv_event_get_code(e);
    
    lv_obj_t *slider = g_core.app().elements[idx]->obj();
    int value = lv_slider_get_value(slider);
    if (const auto st = s_sliderSteps.find(slider); st != s_sliderSteps.end()) {
        if (const int snapped = snapToStep(value, st->second.min, st->second.step); snapped != value) {
            lv_slider_set_value(slider, snapped, LV_ANIM_OFF);  // step="5": the thumb stops on 0, 5, 10…
            value = snapped;
        }
    }
    char val_str[SMALL_BUF_LEN];
    snprintf(val_str, sizeof(val_str), "%d", value);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        // Throttle UI updates during drag
        uint32_t now = lv_tick_get();
        if (now - g_core.app().elements[idx]->last_update < SLIDER_THROTTLE_MS) {
            return;
        }
        g_core.app().elements[idx]->last_update = now;
        
        // Update UI only (no Lua) during drag
        if (!g_core.app().elements[idx]->bind.empty()) {
            ui_update_bindings(g_core.app().elements[idx]->bind.c_str(), val_str);
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        LOG_I(Log::UI, "slider %s: value=%d", g_core.app().elements[idx]->id.c_str(), value);
        // Final update
        if (!g_core.app().elements[idx]->bind.empty()) {
            ui_update_bindings(g_core.app().elements[idx]->bind.c_str(), val_str);
        }
        
        // Sync Lua state
        if (!g_core.app().elements[idx]->bind.empty() && g_state_change_handler) {
            g_state_change_handler(g_core.app().elements[idx]->bind.c_str(), val_str);
        }
        
        // Call Lua onchange handler
        if (!g_core.app().elements[idx]->onchange.empty() && g_onclick_handler) {
            g_onclick_handler(g_core.app().elements[idx]->onchange.c_str());
        }
    }
}

// Input (textarea) event handler — fires on every character
static void input_event_handler(lv_event_t *e) {
    if (g_core.app().isUpdatingFromBinding()) return;
    
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) return;
    
    lv_obj_t *ta = g_core.app().elements[idx]->obj();
    const char *text = lv_textarea_get_text(ta);
    if (!text) return;
    
    // Update state and UI directly
    if (!g_core.app().elements[idx]->bind.empty()) {
        ui_update_bindings(g_core.app().elements[idx]->bind.c_str(), text);
    }
    
    // Sync Lua state so oninput handler can read current value
    if (!g_core.app().elements[idx]->bind.empty() && g_state_change_handler) {
        g_state_change_handler(g_core.app().elements[idx]->bind.c_str(), text);
    }
    
    // Call oninput Lua handler (HTML standard: fires on every input)
    if (!g_core.app().elements[idx]->oninput.empty() && g_onclick_handler) {
        g_onclick_handler(g_core.app().elements[idx]->oninput.c_str());
    }
}

// Input defocus - sync Lua state and call onchange
static void input_complete_handler(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) return;
    
    lv_obj_t *ta = g_core.app().elements[idx]->obj();
    const char *text = lv_textarea_get_text(ta);
    if (!text) return;
    
    // Sync Lua state
    if (!g_core.app().elements[idx]->bind.empty() && g_state_change_handler) {
        g_state_change_handler(g_core.app().elements[idx]->bind.c_str(), text);
    }
    
    // Call onchange Lua handler
    if (!g_core.app().elements[idx]->onchange.empty() && g_onclick_handler) {
        g_onclick_handler(g_core.app().elements[idx]->onchange.c_str());
    }
}

void keyboard_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = (lv_obj_t*)lv_event_get_target(e);
    
    if (code == LV_EVENT_READY) {
        lv_obj_t *ta = lv_keyboard_get_textarea(kb);
        if (ta) {
            for (auto& el : g_core.app().elements) {
                if (el->obj() == ta && !el->onclick.empty() && g_onclick_handler) {
                    g_onclick_handler(el->onclick.c_str());
                    break;
                }
            }
        }
    }
    
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
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
        for (int i = 0; i < g_core.app().pagesCount(); i++) {
            if (g_core.app().pageObj(i) == obj && g_core.app().keyboards[i]) {
                lv_obj_add_flag(g_core.app().keyboards[i], LV_OBJ_FLAG_HIDDEN);
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
        for (int i = 0; i < g_core.app().pagesCount(); i++) {
            if (g_core.app().pageObj(i) == obj) {
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
    lv_obj_t *parent = g_core.app().pageObj(page_idx);
    LOG_I(Log::UI, "input_focus_handler: page_idx=%d parent=%p", page_idx, parent);
    
    // Create keyboard for this page if needed
    if (!g_core.app().keyboards[page_idx]) {
        g_core.app().keyboards[page_idx] = lv_keyboard_create(parent);
        lv_obj_add_event_cb(g_core.app().keyboards[page_idx], keyboard_event_handler, LV_EVENT_ALL, nullptr);
        lv_obj_set_size(g_core.app().keyboards[page_idx], lv_pct(FULL_SIZE_PCT), lv_pct(KEYBOARD_HEIGHT_PCT));
        lv_obj_align(g_core.app().keyboards[page_idx], LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_flag(g_core.app().keyboards[page_idx], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(g_core.app().keyboards[page_idx], LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    
    lv_keyboard_set_textarea(g_core.app().keyboards[page_idx], ta);
    lv_obj_clear_flag(g_core.app().keyboards[page_idx], LV_OBJ_FLAG_HIDDEN);
}

// CSS named colors: color="teal", bgcolor="navy" (same table as CSS, see css_colors.h)
static bool named_color(const char *s, uint32_t &rgb) {
    return s && UI::cssNamedColor(s, rgb);
}

// "#RGB", "#RRGGBB", "RRGGBB" or a CSS color name
bool is_color(const char *s) {
    if (!s || !*s) return false;
    uint32_t rgb;
    if (named_color(s, rgb)) return true;
    if (*s == '#') s++;
    const size_t len = strlen(s);
    if (len != 3 && len != 6) return false;
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

uint32_t parse_color(const char *s) {
    if (!s) return 0xFFFFFF;
    uint32_t named;
    if (*s != '#' && named_color(s, named)) return named;
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
// Position: % always from screen (absolute positioning on page)
int32_t parse_coord_w(const char *s, lv_obj_t *parent) {
    if (!s || !s[0]) return 0;
    char* end = nullptr;
    float val = strtof(s, &end);
    if (end && *end == '%') {
        return (int32_t)(val * SCREEN_WIDTH / 100.0f + 0.5f);
    }
    return (int32_t)(val + 0.5f);
}

int32_t parse_coord_h(const char *s, lv_obj_t *parent) {
    if (!s || !s[0]) return 0;
    char* end = nullptr;
    float val = strtof(s, &end);
    if (end && *end == '%') {
        return (int32_t)(val * SCREEN_HEIGHT / 100.0f + 0.5f);
    }
    return (int32_t)(val + 0.5f);
}

// Size: % via lv_pct() — LVGL resolves from parent at layout time
int32_t parse_size(const char *s) {
    if (!s || !s[0]) return 0;
    char* end = nullptr;
    float val = strtof(s, &end);
    if (end && *end == '%') {
        return lv_pct((int)val);
    }
    return (int32_t)(val + 0.5f);
}

// Convenience: read coordinate attribute (px or %) - width based
static int32_t getAttrCoordW(const char* s, const char* e, const char* name, int32_t def = 0, lv_obj_t* parent = nullptr) {
    char buf[32];
    if (!getAttr(s, e, name, buf, sizeof(buf))) return def;
    return parse_coord_w(buf, parent);
}

static int32_t getAttrCoordH(const char* s, const char* e, const char* name, int32_t def = 0, lv_obj_t* parent = nullptr) {
    char buf[32];
    if (!getAttr(s, e, name, buf, sizeof(buf))) return def;
    return parse_coord_h(buf, parent);
}

// Default (backward compat) - uses width
static int32_t getAttrCoord(const char* s, const char* e, const char* name, int32_t def = 0) {
    return getAttrCoordW(s, e, name, def);
}

// Width/height variants with "has" flag
static int32_t getAttrCoordW(const char* s, const char* e, const char* name, bool& found, lv_obj_t* parent = nullptr) {
    char buf[32];
    found = getAttr(s, e, name, buf, sizeof(buf));
    return found ? parse_coord_w(buf, parent) : 0;
}

static int32_t getAttrCoordH(const char* s, const char* e, const char* name, bool& found, lv_obj_t* parent = nullptr) {
    char buf[32];
    found = getAttr(s, e, name, buf, sizeof(buf));
    return found ? parse_coord_h(buf, parent) : 0;
}

// Size helpers: % via lv_pct() (for w, h)
static int32_t getAttrSize(const char* s, const char* e, const char* name, int32_t def = 0) {
    char buf[32];
    if (!getAttr(s, e, name, buf, sizeof(buf))) return def;
    return parse_size(buf);
}

static int32_t getAttrSize(const char* s, const char* e, const char* name, bool& found) {
    char buf[32];
    found = getAttr(s, e, name, buf, sizeof(buf));
    return found ? parse_size(buf) : 0;
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
    
    // h might be lv_pct encoded — resolve to actual pixels
    if (LV_COORD_IS_PCT(h)) {
        lv_obj_update_layout(lbl);
        h = lv_obj_get_height(lbl);
        if (h <= 0) return;
    }
    
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
    int32_t x = getAttrCoordW(astart, aend, "x", hasX, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", hasY, parent);
    int32_t w = getAttrSize(astart, aend, "w", hasW);
    int32_t h = getAttrSize(astart, aend, "h", hasH);
    
    auto text = trimmed(content);
    bool isLiteral = stripLiteral(text);
    bool hasDynamicText = !isLiteral && contains(text, '{');
    
    // Check for dynamic color bindings
    P::String colorAttr = getAttr(astart, aend, "color");
    P::String bgcolorAttr = getAttr(astart, aend, "bgcolor");
    bool hasDynamicColor = contains(colorAttr, '{');
    bool hasDynamicBgcolor = contains(bgcolorAttr, '{');
    
    ensureId(attrs, "_lbl", hasDynamicText || hasDynamicColor || hasDynamicBgcolor);
    
    // Parse static visual properties
    int fontSize  = getAttrInt(astart, aend, "font");
    int radiusVal = getAttrInt(astart, aend, "radius");
    
    P::String rendered = isLiteral ? P::String(text) : render_template(text.c_str());
    
    // HTML inside (<label>Name <i>(optional)</i></label>, <label><input type="checkbox"> Remember</label>):
    // the text is HTML, as in a browser; the label's own attributes and CSS still apply to it
    const bool rich = !isLiteral && HtmlFlow::hasMarkup(text.c_str(), text.c_str() + text.size(), true);
    lv_obj_t* lbl = nullptr;
    if (rich) {
        UI::CssScope inside("label", std::string_view(astart, static_cast<size_t>(aend - astart)),
                            std::string_view(text.c_str(), text.size()));  // .x i {} sees it
        lbl = HtmlFlow::richText(parent, text.c_str(), text.c_str() + text.size(), !hasW);
    }
    if (lbl) {
        Widget look{lbl};  // what Label::create would give it
        if (!colorAttr.empty() && !hasDynamicColor) look.applyColor(parse_color(colorAttr.c_str()));
        if (!bgcolorAttr.empty() && !hasDynamicBgcolor) look.applyBgColor(parse_color(bgcolorAttr.c_str()));
        if (fontSize > 0) look.applyFont(fontSize);
        if (radiusVal > 0) look.applyRadius(radiusVal);
    } else {
        // Create via Label widget — handles LVGL creation + static visuals
        Label widget = {
            .text    = rendered.c_str(),
            .font    = fontSize,
            .color   = (!colorAttr.empty() && !hasDynamicColor) ? parse_color(colorAttr.c_str()) : NO_COLOR,
            .bgcolor = (!bgcolorAttr.empty() && !hasDynamicBgcolor) ? parse_color(bgcolorAttr.c_str()) : NO_COLOR,
            .radius  = radiusVal,
        };
        widget.create(parent);
        lbl = widget.handle;
    }
    // <label for="id">: a tap on the label works that control, as in a browser; without for=,
    // the control inside it (<label><input type="checkbox"> Remember</label>)
    if (const P::String target = getAttr(astart, aend, "for"); !target.empty()) HtmlFlow::labelFor(lbl, target.c_str());
    else if (rich) HtmlFlow::labelControl(lbl, HtmlFlow::firstControl(lbl));
    
    // Apply CSS (tag, #id, .class cascade)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{lbl}.applyCss("label", attrs.id.c_str(), renderedClass.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
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
        if (w != 0 && lv_obj_check_type(lbl, &lv_label_class)) {  // HTML inside is not one label
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
    // Skip for percentage heights — lv_pct() values are encoded and not real pixels
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
        P::String initVal = g_core.store().getString(varName);
        if (!initVal.empty()) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(parse_color(initVal.c_str())), LV_PART_MAIN);
        }
    }
    if (hasDynamicBgcolor) {
        P::String varName = extractBindVar(bgcolorAttr.c_str());
        P::String initVal = g_core.store().getString(varName);
        if (!initVal.empty()) {
            lv_obj_set_style_bg_color(lbl, lv_color_hex(parse_color(initVal.c_str())), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
    
    if (!attrs.id.empty()) {
        ElementDesc d;
        d.id          = attrs.id.c_str();
        d.obj         = lbl;
        d.tpl         = rich ? nullptr : text.c_str();  // HTML inside keeps its {var} live itself
        d.classTpl    = attrs.hasDynamicClass   ? attrs.cssClass.c_str() : nullptr;
        d.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
        d.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
        applyStaticDisabled(d.obj, attrs);
        d.bgcolorBind = hasDynamicBgcolor        ? bgcolorAttr.c_str()    : nullptr;
        d.colorBind   = hasDynamicColor          ? colorAttr.c_str()      : nullptr;
        d.zIndex      = attrs.zIndex;
        g_core.app().addElement(d);
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
    P::String markup;           // HTML inside (<b>Save</b> now): rendered as HTML; text is its plain form
    P::String imgSrc;           // <img src> inside: the button's icon
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
    
    // HTML inside (<b>Save</b> now, <img src="go.png"> Go): the first <img> is the icon, the rest
    // is rendered as HTML. The plain text below still sizes the button and names it for ui.setText.
    if (labelCount != 1 && HtmlFlow::hasMarkup(raw.c_str(), raw.c_str() + raw.size(), false)) {
        P::String markup = raw;
        const size_t img = markup.find("<img");
        if (img != P::String::npos && img + 4 < markup.size() && !std::isalnum(static_cast<unsigned char>(markup[img + 4]))) {
            const size_t gt = markup.find('>', img);
            if (gt != P::String::npos) {
                result.imgSrc = getAttr(markup.c_str() + img + 4, markup.c_str() + gt, "src");
                markup.erase(img, gt + 1 - img);
            }
        }
        if (HtmlFlow::hasMarkup(markup.c_str(), markup.c_str() + markup.size(), false)) result.markup = trimmed(markup);
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
    
    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    int32_t w = getAttrSize(astart, aend, "w");
    int32_t h = getAttrSize(astart, aend, "h");
    
    // Flatten nested <label> inside button content
    auto flat = flattenButtonContent(content);
    if (iconAttr.empty() && !flat.imgSrc.empty()) iconAttr = flat.imgSrc;  // <button><img src> Go</button>
    const bool rich = !flat.markup.empty();
    auto text = flat.text;
    bool isLiteral = stripLiteral(text);
    bool hasDynamicText = !isLiteral && contains(text, '{');
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
    
    // Prepare icon path with proper lifetime (g_core.app().iconPaths persists until ui_clear)
    const char* iconPath = nullptr;
    if (hasIcon) {
        P::String resolvedPath = resolve_resource_path(iconAttr);
        iconPath = g_core.app().addIconPath(resolvedPath);
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
        .text       = (hasText && !rich) ? renderedText.c_str() : "",
        .iconPath   = iconPath ? iconPath : "",
        .iconSize   = iconSize,
        .font       = fontSize,    // Apply font at creation, before CSS
        .bgcolor    = NO_COLOR,  // Will be set after CSS
        .radius     = radiusVal,
        .forceLabel = hasDynamicText && !rich,  // ensure label exists for {binding} updates
    };
    widget.create(parent);
    auto *btn = widget.handle;
    if (rich) {  // HTML inside: rendered where the label would be, after the icon
        UI::CssScope inside("button", std::string_view(astart, static_cast<size_t>(aend - astart)),
                            std::string_view(flat.markup.c_str(), flat.markup.size()));  // .x b {} sees it
        widget.label.handle = HtmlFlow::richText(btn, flat.markup.c_str(), flat.markup.c_str() + flat.markup.size(), true);
        if (widget.label.handle && hasIcon) {
            lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        } else if (widget.label.handle) {
            lv_obj_center(widget.label.handle);
        }
    }
    
    _unique_id(btn, attrs.id.c_str());  // For testing mock
    
    // A button HTML lays out (in a line of text, or a block of an HTML page), as a browser's:
    // 13.33px text (drawn at 14), 1px 6px of padding, a 2px border (transparent: the button's
    // color shows through it). CSS comes after it. One placed by x, y or align, or in a TelaOS
    // container's own layout, stays as TelaOS draws it
    const bool browser = (HtmlFlow::browserSized() || HtmlFlow::inFlow(parent)) && getAttr(astart, aend, "x").empty() &&
                         getAttr(astart, aend, "y").empty() && align.empty();
    fingerRoom(btn, ClickArea::BUTTON, browser);  // a browser-sized one has it too
    if (browser) {
        lv_obj_set_style_text_font(btn, UI::Font::get(14), LV_PART_MAIN);
        lv_obj_set_style_pad_left(btn, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_top(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        // its text in the button's font, not in the label's own 16px: CSS sizes the button
        if (widget.label.handle && fontSize == 0) {
            lv_obj_remove_local_style_prop(widget.label.handle, LV_STYLE_TEXT_FONT, LV_PART_MAIN);
        }
    }
    
    // 1. CSS cascade (tag < .class < tag.class < #id)
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        widget.applyCss("button", attrs.id.c_str(), renderedClass.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    }
    
    // 2. Inline positioning (overrides CSS — HTML standard)
    if (align == "center") {
        if (w != 0 || h != 0) set_size(btn, w, h);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    } else {
        set_pos(btn, x, y);
        if (w != 0 || h != 0) set_size(btn, w, h);
    }
    
    // 2. Local bgcolor override (static or dynamic)
    if (!bgcolorAttr.empty()) {
        if (hasDynamicBgcolor) {
            P::String varName = extractBindVar(bgcolorAttr.c_str());
            P::String initVal = g_core.store().getString(varName);
            if (!initVal.empty()) {
                UI::setBgColor(widget, parse_color(initVal.c_str()));
            }
        } else {
            UI::setBgColor(widget, staticBgcolor);
        }
    }
    
    // Apply HTML default font on label, then override if font attr specified (a browser-sized
    // button's label takes the button's font: its 14px, or its CSS)
    if (widget.label.handle) {
        if (!browser) lv_obj_set_style_text_font(widget.label.handle, UI::Font::get(), LV_PART_MAIN);
        if (fontSize > 0) {
            widget.label.applyFont(fontSize);
        }
    }
    
    // Text color — static or dynamic
    if (!colorAttr.empty() && widget.label.handle) {
        if (hasDynamicColor) {
            P::String varName = extractBindVar(colorAttr.c_str());
            P::String initVal = g_core.store().getString(varName);
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
    bd.tpl         = (hasDynamicText && widget.label.handle && !rich) ? text.c_str() : nullptr;  // HTML keeps its own
    bd.classTpl    = attrs.hasDynamicClass   ? attrs.cssClass.c_str() : nullptr;
    bd.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    bd.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
    // NOTE: initial disabled applied to `btn` below, after parentObj is set —
    // bd.obj here is the label, and greying the label doesn't block clicks.
    bd.bgcolorBind = hasDynamicBgcolor        ? bgcolorAttr.c_str()    : nullptr;
    bd.colorBind   = hasDynamicColor          ? colorAttr.c_str()      : nullptr;
    bd.zIndex      = attrs.zIndex;
    int idx = g_core.app().addElement(bd);
    
    if (idx >= 0 && idx < (int)g_core.app().elements.size()) {
        g_core.app().elements[idx]->parentObj = btn;
        g_core.app().elements[idx]->onhold = onhold;
        
        // For buttons, apply visibility to the button, not the label
        if (!g_core.app().elements[idx]->visibleBind.empty()) {
            P::String visVal = g_core.store().getString(g_core.app().elements[idx]->visibleBind);
            bool visible = (visVal == "true" || visVal == "1");
            if (!visible) {
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(storeObj, LV_OBJ_FLAG_HIDDEN);
        }

        // Same for disabled: grey the button itself. addElement() applied the
        // initial state to bd.obj (the label), which doesn't block clicks —
        // undo that and put the state on the button where it belongs.
        if (!g_core.app().elements[idx]->disabledBind.empty()) {
            P::String disVal = g_core.store().getString(g_core.app().elements[idx]->disabledBind);
            bool disabled = (disVal == "true" || disVal == "1");
            lv_obj_remove_state(storeObj, LV_STATE_DISABLED);
            if (disabled) lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
    }
    applyStaticDisabled(btn, attrs);  // a button without an id is not an element, but is disabled too
    
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
    
    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    
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
    
    fingerRoom(sw, ClickArea::SWITCH, HtmlFlow::browserSized() || HtmlFlow::inFlow(parent));
    
    // CSS first, then inline overrides
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{sw}.applyCss("switch", attrs.id.c_str(), renderedClass.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    }
    set_pos(sw, x, y);
    
    ElementDesc sd;
    sd.id          = attrs.id.c_str();
    sd.obj         = sw;
    sd.onchange    = onchange.c_str();
    sd.bind        = bind.c_str();
    sd.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    sd.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
    applyStaticDisabled(sd.obj, attrs);
    sd.zIndex      = attrs.zIndex;
    int idx = g_core.app().addElement(sd);
    
    widget.on(LV_EVENT_VALUE_CHANGED, switch_event_handler, idx);
}

// A range HTML lays out, as Chromium has it with the watch's font: 129x16 (w= keeps its width),
// a margin of 2px. LVGL draws a slider's track and filling in the whole box: transform_height
// draws them 4px high in its middle; the knob, as high as the box, is 16
static void browserRange(lv_obj_t* s, bool ownWidth) {
    if (!ownWidth)
        lv_obj_set_width(s, 129);
    lv_obj_set_height(s, 16);
    lv_obj_set_style_margin_top(s, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_right(s, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(s, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_left(s, 2, LV_PART_MAIN);
    lv_obj_set_style_transform_height(s, -6, LV_PART_MAIN);
    lv_obj_set_style_transform_height(s, -6, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(s, 0, LV_PART_KNOB);
}

void create_slider(const char *astart, const char *aend, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto bind = getAttr(astart, aend, "bind");
    auto onchange = getAttr(astart, aend, "onchange");
    
    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    int32_t w = getAttrSize(astart, aend, "w", Defaults::Slider::WIDTH);
    int min_val = getAttrInt(astart, aend, "min", Defaults::Slider::MIN);
    int max_val = getAttrInt(astart, aend, "max", Defaults::Slider::MAX);
    const int step = getAttrInt(astart, aend, "step", 0);
    
    ensureId(attrs, "_sl", !bind.empty() || !onchange.empty());
    
    // Determine initial value from binding
    int initValue = 0;
    if (!bind.empty()) {
        if (auto *init_val = get_state_value(bind.c_str())) {
            initValue = snapToStep(atoi(init_val), min_val, step);
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
    
    // as before: a slider's own hit test (LV_OBJ_FLAG_ADV_HITTEST) would take only its knob
    lv_obj_set_ext_click_area(slider, ClickArea::SLIDER);

    // A range HTML lays out (<input type="range">, or a slider in a line of text) is a browser's;
    // one placed by x or y stays as TelaOS draws it
    const bool browser = (HtmlFlow::browserSized() || HtmlFlow::inFlow(parent)) && getAttr(astart, aend, "x").empty() &&
                         getAttr(astart, aend, "y").empty();
    if (browser)
        browserRange(slider, !getAttr(astart, aend, "w").empty());
    
    // CSS first, then inline overrides
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{slider}.applyCss("slider", attrs.id.c_str(), renderedClass.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    }
    set_pos(slider, x, y);
    
    ElementDesc sld;
    sld.id          = attrs.id.c_str();
    sld.obj         = slider;
    sld.onchange    = onchange.c_str();
    sld.bind        = bind.c_str();
    sld.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    sld.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
    applyStaticDisabled(sld.obj, attrs);
    sld.zIndex      = attrs.zIndex;
    int idx = g_core.app().addElement(sld);
    if (step > 1) {
        s_sliderSteps[slider] = SliderStep{min_val, step};
        lv_obj_add_event_cb(slider, forget_widget_state, LV_EVENT_DELETE, slider);
    }
    
    widget.on(LV_EVENT_VALUE_CHANGED, slider_event_handler, idx);
    widget.on(LV_EVENT_RELEASED, slider_event_handler, idx);
}

// HTML's size=, cols=, rows=: a whole number above 0, or else the default
static int htmlCount(const char* astart, const char* aend, const char* name, int def) {
    const int n = getAttrInt(astart, aend, name, def);
    return n > 0 ? n : def;
}

// A field HTML lays out, as Chromium draws it with the watch's font: 13.33px text (drawn at 14);
// a text input 1px 2px of padding in a 2px border, 8px a character of size= (20) and 50 more, 21
// high; a textarea 2px in 1px, 8px a character of cols= (20) and 22 more, 15 a line of rows= (2)
// and 6 more. CSS comes after it
static int32_t browserField(lv_obj_t* ta, bool multiline, const char* astart, const char* aend) {
    lv_obj_set_style_text_font(ta, UI::Font::get(14), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, multiline ? 1 : 2, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ta, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ta, 2, LV_PART_MAIN);
    // a line of LVGL's 14px is 2px higher than Chromium's: the one line of an input takes them
    // from its padding, so the text stays whole in the same 21px
    lv_obj_set_style_pad_top(ta, multiline ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(ta, multiline ? 2 : 0, LV_PART_MAIN);
    const int32_t h = multiline ? 15 * htmlCount(astart, aend, "rows", 2) + 6 : 21;
    if (multiline)
        lv_obj_set_size(ta, 8 * htmlCount(astart, aend, "cols", 20) + 22, h);
    else
        lv_obj_set_size(ta, 8 * htmlCount(astart, aend, "size", 20) + 50, h);
    return h;
}

// A field whose font CSS changed (font-size): as high as a browser makes it with that font, its
// lines (rows=, or one) and its padding and border. LVGL's line of a font is a browser's, but for
// the 14px that stands for a browser's 13.33px (browserField): a field in it keeps its size
static void fieldHeightForFont(lv_obj_t* ta, bool multiline, const char* astart, const char* aend) {
    const lv_font_t* font = lv_obj_get_style_text_font(ta, LV_PART_MAIN);
    if (!font || font == UI::Font::get(14))
        return;
    // the one line of a field gave its 1px above and below to LVGL's taller 14px: in a font of its
    // own it has them back, unless CSS gave it a padding
    const bool lent = lv_obj_get_style_pad_top(ta, LV_PART_MAIN) == 0 && lv_obj_get_style_pad_bottom(ta, LV_PART_MAIN) == 0;
    if (!multiline && lent) {
        lv_obj_set_style_pad_top(ta, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(ta, 1, LV_PART_MAIN);
    }
    const int32_t lines = multiline ? htmlCount(astart, aend, "rows", 2) : 1;
    const int32_t border = lv_obj_get_style_border_width(ta, LV_PART_MAIN);
    lv_obj_set_height(ta, lines * lv_font_get_line_height(font) + lv_obj_get_style_pad_top(ta, LV_PART_MAIN) +
                              lv_obj_get_style_pad_bottom(ta, LV_PART_MAIN) + 2 * border);
}

void create_input(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto bind = getAttr(astart, aend, "bind");
    auto onchange = getAttr(astart, aend, "onchange");
    auto onenter = getAttr(astart, aend, "onenter");
    auto onblur = getAttr(astart, aend, "onblur");
    auto oninput = getAttr(astart, aend, "oninput");
    auto placeholder = getAttr(astart, aend, "placeholder");
    auto bgcolorAttr = getAttr(astart, aend, "bgcolor");
    auto colorAttr = getAttr(astart, aend, "color");
    const int maxLength = getAttrInt(astart, aend, "maxlength", 0);
    const bool readOnly = flagAttr(astart, aend, "readonly");  // shown, but no keyboard
    
    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    int32_t w = getAttrSize(astart, aend, "w", Defaults::Input::WIDTH);
    int32_t h = getAttrSize(astart, aend, "h", Defaults::Input::HEIGHT);
    
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
    if (maxLength > 0) lv_textarea_set_max_length(ta, static_cast<uint32_t>(maxLength));
    
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
    
    // A field HTML lays out (in a line of text, or a block of an HTML page) is a browser's; one
    // placed by x or y stays as TelaOS draws it
    const bool browser = (HtmlFlow::browserSized() || HtmlFlow::inFlow(parent)) && getAttr(astart, aend, "x").empty() &&
                         getAttr(astart, aend, "y").empty();
    const int32_t browserHeight = browser ? browserField(ta, isMultiline, astart, aend) : 0;

    // CSS first, then inline overrides
    {
        P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
        Widget{ta}.applyCss("input", attrs.id.c_str(), renderedClass.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    }
    set_pos(ta, x, y);
    if (!browser) {
        set_size(ta, w, h);
    } else {  // the browser's size, or CSS's; w= and h= over them, as inline values
        if (lv_obj_get_style_height(ta, LV_PART_MAIN) == browserHeight)  // CSS gave no height
            fieldHeightForFont(ta, isMultiline, astart, aend);
        if (!getAttr(astart, aend, "w").empty())
            lv_obj_set_width(ta, w);
        if (!getAttr(astart, aend, "h").empty())
            lv_obj_set_height(ta, h);
    }
    
    ElementDesc ind;
    ind.id          = attrs.id.c_str();
    ind.obj         = ta;
    ind.onclick     = onenter.c_str();
    ind.onchange    = onchange.c_str();
    ind.oninput     = oninput.c_str();
    ind.bind        = bind.c_str();
    ind.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    ind.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
    applyStaticDisabled(ind.obj, attrs);
    ind.zIndex      = attrs.zIndex;
    int idx = g_core.app().addElement(ind);
    
    widget.on(LV_EVENT_VALUE_CHANGED, input_event_handler, idx);
    if (!readOnly) widget.on(LV_EVENT_FOCUSED, input_focus_handler, idx);
    widget.on(LV_EVENT_DEFOCUSED, input_defocus_handler, idx);
    widget.on(LV_EVENT_DEFOCUSED, input_complete_handler, idx);
}

// The file behind an LVGL path ("C:/apps/x/pic.png") can be opened
static bool image_exists(const char* lvglPath) {
#if defined(LVGL_MOCK_ENABLED)
    (void)lvglPath;
    return true;  // the mock has no files: every picture is there
#else
    lv_fs_file_t f;
    if (lv_fs_open(&f, lvglPath, LV_FS_MODE_RD) != LV_FS_RES_OK) return false;
    lv_fs_close(&f);
    return true;
#endif
}

// Image element - displays PNG image with optional click handler
void create_image(const char *astart, const char *aend, lv_obj_t *parent) {
    auto attrs = parseCommonAttrs(astart, aend);
    auto src = getAttr(astart, aend, "src");
    auto onclick = getAttr(astart, aend, "onclick");
    auto align = getAttr(astart, aend, "align");
    
    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    int32_t w = getAttrSize(astart, aend, "w");
    int32_t h = getAttrSize(astart, aend, "h");
    
    P::String imgPath = resolve_resource_path(src);
    
    // Using global g_core.app().imagePaths (cleared on ui_clear)
    const char* lvglPath = g_core.app().addImagePath(imgPath);
    
    // A picture that is not there shows its alt= text, as in a browser; the rest (place, size,
    // onclick, visible=) is the picture's
    lv_obj_t* img = nullptr;
    if (const P::String alt = getAttr(astart, aend, "alt"); !alt.empty() && !image_exists(lvglPath)) {
        Label text = {.text = alt, .color = 0xAAAAAA};
        text.create(parent);
        img = text.handle;
    } else {
        Image widget = { .src = lvglPath };
        widget.create(parent);
        img = widget.handle;
    }
    
    UI::Dom::element(img, "image", std::string_view(astart, static_cast<size_t>(aend - astart)));  // no CSS, still an element

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
        fingerRoom(img, ClickArea::IMAGE, HtmlFlow::browserSized() || HtmlFlow::inFlow(parent));
        ensureId(attrs, "_img", true);
        
        ElementDesc imd;
        imd.id          = attrs.id.c_str();
        imd.obj         = img;
        imd.onclick     = onclick.c_str();
        imd.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
        imd.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
        applyStaticDisabled(imd.obj, attrs);
        imd.zIndex      = attrs.zIndex;
        int idx = g_core.app().addElement(imd);
        Widget{img}.on(LV_EVENT_CLICKED, button_click_handler, idx);
    } else if (!attrs.id.empty() || attrs.hasDynamicVisible) {
        ensureId(attrs, "_img", false);
        ElementDesc imd2;
        imd2.id          = attrs.id.c_str();
        imd2.obj         = img;
        imd2.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
        imd2.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
        applyStaticDisabled(imd2.obj, attrs);
        imd2.zIndex      = attrs.zIndex;
        g_core.app().addElement(imd2);
    }
}

// Canvas touch handler
static void canvas_touch_handler(lv_event_t* e) {
    lv_obj_t* canvas = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) return;
    auto& elem = g_core.app().elements[idx];
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
    
    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    // Canvas MUST be in pixels — it allocates a pixel buffer
    // HTML <canvas width height> or TelaOS w h
    int32_t w = getAttrCoordW(astart, aend, getAttr(astart, aend, "w").empty() ? "width" : "w", 200);
    int32_t h = getAttrCoordH(astart, aend, getAttr(astart, aend, "h").empty() ? "height" : "h", 200);
    
    ensureId(attrs, "_canvas", true);
    
    auto *canvas = lv_canvas_create(parent);
    _unique_id(canvas, attrs.id.c_str());  // For testing mock
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
    UI::Dom::element(canvas, "canvas", std::string_view(astart, static_cast<size_t>(aend - astart)));
    
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
            P::String visVal = g_core.store().getString(elem->visibleBind);
            bool visible = (visVal == "true" || visVal == "1");
            if (!visible) {
                lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    int idx = g_core.app().elements.size();
    g_core.app().elements.push_back(std::move(elem));
    
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
    bool isLiteral = stripLiteral(text);
    bool hasDynamicText = !isLiteral && contains(text, '{');
    
    CommonAttrs attrs = parseCommonAttrs(astart, aend);
    ensureId(attrs, "_md", hasDynamicText);
    
    P::String rendered = hasDynamicText ? render_template(text.c_str()) : decodeEntities(text);
    
    // Parse position attrs (same pattern as create_label)
    auto align = getAttr(astart, aend, "align");
    auto valignPos = getAttr(astart, aend, "valign");
    bool hasX, hasY, hasW, hasH;
    int32_t x = getAttrCoordW(astart, aend, "x", hasX, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", hasY, parent);
    int32_t w = getAttrSize(astart, aend, "w", hasW);
    int32_t h = getAttrSize(astart, aend, "h", hasH);
    
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
    
    // CSS first, then inline overrides
    P::String renderedClass = attrs.hasDynamicClass ? render_template(attrs.cssClass.c_str()) : attrs.cssClass;
    Widget{spangroup}.applyCss("markdown", attrs.id.c_str(), renderedClass.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    
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
            P::String visVal = g_core.store().getString(elem->visibleBind);
            bool visible = (visVal == "true" || visVal == "1");
            if (!visible) {
                lv_obj_add_flag(spangroup, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    g_core.app().elements.push_back(std::move(elem));
}

#endif // LV_USE_SPAN

// ============ QR CODE ============
// <qr size="200" dark="#000000" light="#ffffff">{payload}</qr>
// Content may be literal or a {var} template; on bound-var change the code
// is re-rendered via updateFn (same mechanism markdown uses).
void create_qr(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
#if !LV_USE_QRCODE
    (void)astart; (void)aend; (void)content; (void)parent;
    LOG_W(Log::UI, "<qr> ignored: LV_USE_QRCODE is disabled");
    return;
#else
    auto text = trimmed(content);
    bool isLiteral = stripLiteral(text);
    bool hasDynamicText = !isLiteral && contains(text, '{');

    CommonAttrs attrs = parseCommonAttrs(astart, aend);
    ensureId(attrs, "_qr", hasDynamicText);

    P::String payload = hasDynamicText ? render_template(text.c_str()) : decodeEntities(text);

    // Geometry + colors
    int32_t size = getAttrSize(astart, aend, "size");
    if (size <= 0) size = 200;
    P::String darkAttr  = getAttr(astart, aend, "dark");
    P::String lightAttr = getAttr(astart, aend, "light");
    uint32_t darkRgb  = darkAttr.empty()  ? 0x000000 : parse_color(darkAttr.c_str());
    uint32_t lightRgb = lightAttr.empty() ? 0xFFFFFF : parse_color(lightAttr.c_str());

    lv_obj_t* qr = lv_qrcode_create(parent);
    UI::Dom::element(qr, "qr", std::string_view(astart, static_cast<size_t>(aend - astart)));
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr,  lv_color_hex(darkRgb));
    lv_qrcode_set_light_color(qr, lv_color_hex(lightRgb));

    // Position (same logic as image/markdown)
    auto align = getAttr(astart, aend, "align");
    bool hasX, hasY;
    int32_t x = getAttrCoordW(astart, aend, "x", hasX, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", hasY, parent);
    if (align == "center" && !hasX) {
        lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, y);
    } else {
        set_pos(qr, x, y);
    }

    // Initial render (empty payload -> leave blank, qrcodegen rejects 0-length)
    if (!payload.empty()) {
        lv_qrcode_update(qr, payload.c_str(), payload.length());
    } else {
        // Blank = all light. lv_qrcode_set_size() clears the pixels but not the two-colour
        // palette in front of them: without this the square shows whatever colour was in
        // that memory before (a different one on every boot)
        lv_canvas_set_palette(qr, 0, lv_color_to_32(lv_color_hex(lightRgb), LV_OPA_COVER));
        lv_canvas_set_palette(qr, 1, lv_color_to_32(lv_color_hex(darkRgb), LV_OPA_COVER));
    }

    // Store element
    auto elem = P::create<UI::Element>();
    elem->id = attrs.id;
    elem->w.handle = qr;
    elem->tpl = hasDynamicText ? P::String(text) : "";

    if (hasDynamicText) {
        elem->updateFn = [qr](const P::String& rendered) {
            if (rendered.empty()) return;
            lv_qrcode_update(qr, rendered.c_str(), rendered.length());
        };
    }

    if (!attrs.visible.empty()) {
        P::String vb = attrs.visible;
        if (vb.length() > 2 && vb[0] == '{' && vb.back() == '}') {
            elem->visibleBind = vb.substr(1, vb.length() - 2);
        } else {
            elem->visibleBind = vb;
        }
        if (!elem->visibleBind.empty()) {
            P::String visVal = g_core.store().getString(elem->visibleBind);
            if (!(visVal == "true" || visVal == "1")) {
                lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    g_core.app().elements.push_back(std::move(elem));
#endif // LV_USE_QRCODE
}

// ============ SELECT / DROPDOWN ============

static void dropdown_event_handler(lv_event_t *e) {
    if (g_core.app().isUpdatingFromBinding()) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)g_core.app().elements.size()) return;

    lv_obj_t *dd = g_core.app().elements[idx]->obj();
    uint16_t sel = lv_dropdown_get_selected(dd);

    if (const auto ch = s_dropdownChoices.find(dd); ch != s_dropdownChoices.end()) {
        if (sel < ch->second.disabled.size() && ch->second.disabled[sel]) {
            lv_dropdown_set_selected(dd, ch->second.chosen);  // <option disabled>: cannot be chosen
            return;
        }
        ch->second.chosen = sel;
    }

    const auto& values = g_core.app().elements[idx]->dropdownValues;
    if (sel >= values.size()) return;

    const char* val = values[sel].c_str();

    LOG_I(Log::UI, "dropdown %s: index=%d value=%s",
          g_core.app().elements[idx]->id.c_str(), (int)sel, val);

    if (!g_core.app().elements[idx]->bind.empty()) {
        ui_update_bindings(g_core.app().elements[idx]->bind.c_str(), val);
        if (g_state_change_handler) {
            g_state_change_handler(g_core.app().elements[idx]->bind.c_str(), val);
        }
    }

    if (!g_core.app().elements[idx]->onchange.empty() && g_onclick_handler) {
        g_onclick_handler(g_core.app().elements[idx]->onchange.c_str());
    }
}

struct SelectOptions {
    P::Array<P::String> values;
    P::Array<P::String> labels;
    P::Array<bool>      disabled;       // <option disabled>
    int                 selected = -1;  // <option selected>, the first one
    P::String           lvglStr;        // "Label1\nLabel2\n..."
};

// The <option>s of a <select>, read like a browser reads them: </option> may be left out
static SelectOptions parse_options(const char* content) {
    SelectOptions out;
    if (!content) return out;
    UI::TagReader reader(content);
    UI::StartTag tag;
    for (auto step = reader.next(tag); step != UI::TagReader::Step::End; step = reader.next(tag)) {
        if (step != UI::TagReader::Step::Tag || tag.name != "option") continue;
        P::String label = trimmed(reader.content(tag));
        if (contains(label, '{')) label = render_template(label.c_str());
        P::String value = tag.attrs.get("value");
        if (value.empty()) value = label;
        else if (contains(value, '{')) value = render_template(value.c_str());

        if (out.selected < 0 && tag.attrs.has("selected")) out.selected = static_cast<int>(out.values.size());
        out.disabled.push_back(tag.attrs.has("disabled"));
        out.values.push_back(value);
        out.labels.push_back(label);
        if (!out.lvglStr.empty()) out.lvglStr += '\n';
        out.lvglStr += label;
    }
    return out;
}

// The arrow of a <select>: expand_more of the icons behind every font. LVGL's own, LV_SYMBOL_DOWN,
// is a glyph of its symbol font, which the watch's fonts have not: it was drawn as an empty box.
// Without the icons, no arrow
static const char* select_arrow() {
    static std::array<char, 4> arrow{};
    if (!arrow[0]) {
        const uint32_t c = UI::Font::iconCode("expand_more");
        if (c >= 0x800 && c <= 0xFFFF) {  // three bytes of UTF-8, as every icon of the font
            arrow[0] = static_cast<char>(0xE0 | (c >> 12));
            arrow[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            arrow[2] = static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return arrow[0] ? arrow.data() : nullptr;
}

// A <select> HTML lays out, as Chromium draws it with the watch's font: 13.33px text (drawn at
// 14, as a field's), a 1px border, 19 high (LVGL's line of 14px takes all of it), and as wide as
// its longest option at 13.33px with 22 more for its arrow and padding. CSS comes after it
static void browserSelect(lv_obj_t* dd, const SelectOptions& opts) {
    const lv_font_t* font = UI::Font::get(14);
    lv_obj_set_style_text_font(dd, font, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd, font, LV_PART_INDICATOR);  // the arrow
    lv_obj_set_style_border_width(dd, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_top(dd, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(dd, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(dd, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(dd, 4, LV_PART_MAIN);
    int32_t widest = 0;
#if !defined(LVGL_MOCK_ENABLED)
    for (const P::String& label : opts.labels) {
        widest = std::max(widest, lv_text_get_width(label.c_str(), static_cast<uint32_t>(label.size()), font, 0));
    }
#else
    (void)opts;
#endif
    // the text is measured at 14px: 20/21 of it is Chromium's 13.33, rounded up as Chromium does
    lv_obj_set_size(dd, (widest * 20 + 20) / 21 + 22, 19);
}

void create_select(const char *astart, const char *aend, const char *content, lv_obj_t *parent) {
    auto attrs    = parseCommonAttrs(astart, aend);
    auto bind     = getAttr(astart, aend, "bind");
    auto onchange = getAttr(astart, aend, "onchange");

    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    int32_t w = getAttrSize(astart, aend, "w");
    int32_t h = getAttrSize(astart, aend, "h");

    ensureId(attrs, "_sel", !bind.empty() || !onchange.empty());

    SelectOptions opts = parse_options(content);
    if (opts.values.empty()) {
        LOG_W(Log::UI, "create_select: no <option> found, skipping");
        return;
    }

    lv_obj_t* dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, opts.lvglStr.c_str());
    lv_dropdown_set_symbol(dd, select_arrow());
    // Its list opens on the screen, out of the page: in the watch's font, not the theme's 8px
    // Montserrat a child of the screen has
    if (lv_obj_t* list = lv_dropdown_get_list(dd))
        lv_obj_set_style_text_font(list, UI::Font::get(), LV_PART_MAIN);

    // Initial selection: the state, else <option selected>, else the first option that can be chosen
    int initIdx = opts.selected;
    if (!bind.empty()) {
        if (const char* sv = get_state_value(bind.c_str()); sv && sv[0]) {
            for (int i = 0; i < (int)opts.values.size(); ++i) {
                if (opts.values[i] == sv) { initIdx = i; break; }
            }
        }
    }
    if (initIdx < 0) {
        initIdx = 0;
        while (initIdx + 1 < (int)opts.disabled.size() && opts.disabled[initIdx]) ++initIdx;
    }
    lv_dropdown_set_selected(dd, (uint16_t)initIdx);

    // A <select> HTML lays out is a browser's; one placed by x or y stays as TelaOS draws it
    const bool browser = (HtmlFlow::browserSized() || HtmlFlow::inFlow(parent)) && getAttr(astart, aend, "x").empty() &&
                         getAttr(astart, aend, "y").empty();
    if (browser)
        browserSelect(dd, opts);

    set_pos(dd, x, y);
    if (w != 0) lv_obj_set_width(dd, w);
    if (h != 0) lv_obj_set_height(dd, h);

    // CSS cascade
    {
        P::String cls = attrs.hasDynamicClass
            ? render_template(attrs.cssClass.c_str())
            : attrs.cssClass;
        Widget{dd}.applyCss("select", attrs.id.c_str(), cls.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    }

    // Inline visual overrides
    P::String bgcolorAttr = getAttr(astart, aend, "bgcolor");
    P::String colorAttr   = getAttr(astart, aend, "color");
    if (!bgcolorAttr.empty()) {
        lv_obj_set_style_bg_color(dd, lv_color_hex(parse_color(bgcolorAttr.c_str())), 0);
        lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    }
    if (!colorAttr.empty()) {
        lv_obj_set_style_text_color(dd, lv_color_hex(parse_color(colorAttr.c_str())), 0);
    }

    ElementDesc sd;
    sd.id          = attrs.id.c_str();
    sd.obj         = dd;
    sd.bind        = bind.c_str();
    sd.onchange    = onchange.c_str();
    sd.visibleBind = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    sd.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
    applyStaticDisabled(sd.obj, attrs);
    sd.zIndex      = attrs.zIndex;
    int elemIdx = g_core.app().addElement(sd);

    if (elemIdx >= 0 && elemIdx < (int)g_core.app().elements.size()) {
        g_core.app().elements[elemIdx]->is_dropdown    = true;
        g_core.app().elements[elemIdx]->dropdownValues = std::move(opts.values);

        if (attrs.hasDynamicVisible && !attrs.visible.empty()) {
            P::String vb  = extractBindVar(attrs.visible.c_str());
            P::String vis = g_core.store().getString(vb);
            if (vis != "true" && vis != "1") {
                lv_obj_add_flag(dd, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (std::find(opts.disabled.begin(), opts.disabled.end(), true) != opts.disabled.end()) {
        s_dropdownChoices[dd] = DropdownChoice{std::move(opts.disabled), static_cast<uint16_t>(initIdx)};
        lv_obj_add_event_cb(dd, forget_widget_state, LV_EVENT_DELETE, dd);
    }
    Widget{dd}.on(LV_EVENT_VALUE_CHANGED, dropdown_event_handler, elemIdx);

    LOG_I(Log::UI, "create_select: id=%s bind=%s options=%d init=%d",
          attrs.id.c_str(), bind.c_str(),
          elemIdx >= 0 ? (int)g_core.app().elements[elemIdx]->dropdownValues.size() : 0,
          initIdx);
}

// ===================== <list> =====================
//
// Two flavours:
//
//   Static:    <list><item>Foo</item><item onclick="doX">Bar</item></list>
//   Dynamic:   <list bind="files" itemClick="onTap"/>
//
// Dynamic items come from a state array (declared as <array name="files"/> in
// <state>, or via YAML). When the array is reassigned in Lua
// (state.files = {...}) the list rebuilds; when one item changes
// (state.files[3] = "x") only that button's text is patched. Tap invokes
//   itemClick(idx, value)   -- idx is 1-based, value is the item string.
//
// Static <item> children may carry onclick="fn" — invoked as a plain no-arg
// call, same as a regular <button>.

// Per-list click bridge: user_data packs (elementIdx << 16) | itemIdx1.
// Both stay well under 16 bits on this class of devices.
static int listItemUserData(int elementIdx, int itemIdx1) {
    return ((elementIdx & 0xFFFF) << 16) | (itemIdx1 & 0xFFFF);
}

static void list_item_click_handler(lv_event_t* e) {
    intptr_t packed = (intptr_t) lv_event_get_user_data(e);
    int elementIdx = ((int)packed >> 16) & 0xFFFF;
    int itemIdx1   = ((int)packed)       & 0xFFFF;

    if (elementIdx < 0 || elementIdx >= (int)g_core.app().elements.size()) return;
    const auto& el = g_core.app().elements[elementIdx];
    if (!el) return;

    const P::String& cb = el->itemClick;
    if (cb.empty() || !g_onitem_handler) return;

    P::String value;
    if (!el->bind.empty()) {
        // Dynamic: current value from the bound array (idx is 1-based).
        value = g_core.store().getArrayItem(el->bind, itemIdx1 - 1);
    } else {
        // Static: the button's own text.
        lv_obj_t* btn = (lv_obj_t*) lv_event_get_target(e);
        const char* txt = lv_list_get_button_text(el->w.handle, btn);
        if (txt) value = txt;
    }
    g_onitem_handler(cb.c_str(), itemIdx1, value.c_str());
}

// Append one button with click wired. `onclickName` non-null ⇒ static item
// with its own no-arg onclick; otherwise the (idx,value) itemClick bridge.
static void list_append_item(lv_obj_t* list, int elementIdx, int itemIdx1,
                             const char* text, const char* onclickName) {
    lv_obj_t* btn = lv_list_add_button(list, nullptr, text ? text : "");
    if (!btn) return;
    if (onclickName && onclickName[0]) {
        struct StaticHook {
            static void cb(lv_event_t* e) {
                const char* fn = (const char*) lv_event_get_user_data(e);
                if (fn && fn[0] && g_onclick_handler) g_onclick_handler(fn);
            }
        };
        lv_obj_add_event_cb(btn, StaticHook::cb, LV_EVENT_CLICKED, (void*) onclickName);
    } else {
        lv_obj_add_event_cb(btn, list_item_click_handler, LV_EVENT_CLICKED,
                            (void*)(intptr_t) listItemUserData(elementIdx, itemIdx1));
    }
}

// Build/rebuild contents from a state array. Clears existing children first —
// used both at creation (initial fill) and on full-array updates.
void list_rebuild_from_array(lv_obj_t* list, int elementIdx, const P::String& arrayName) {
    lv_obj_clean(list);
    auto& store = g_core.store();
    int n = store.getArraySize(arrayName);
    for (int i = 0; i < n; i++) {
        P::String v = store.getArrayItem(arrayName, i);
        list_append_item(list, elementIdx, i + 1, v.c_str(), nullptr);
    }
}

// Parse static <item>text</item> children (optionally with onclick="fn").
// Item styling inherits LVGL list-button defaults for now.
static void parse_static_items(const char* content, lv_obj_t* list, int elementIdx) {
    if (!content) return;
    const char* p = content;
    int idx = 1;
    while (p && *p) {
        const char* open = strstr(p, "<item");
        if (!open) break;
        const char* attrEnd = strchr(open, '>');
        if (!attrEnd) break;
        const char* close = strstr(attrEnd, "</item>");
        if (!close) break;

        P::String onclickAttr = getAttr(open + 5, attrEnd, "onclick");

        P::String txt(attrEnd + 1, (size_t)(close - attrEnd - 1));
        size_t s = 0, e = txt.size();
        while (s < e && (txt[s]==' '||txt[s]=='\n'||txt[s]=='\t'||txt[s]=='\r')) s++;
        while (e > s && (txt[e-1]==' '||txt[e-1]=='\n'||txt[e-1]=='\t'||txt[e-1]=='\r')) e--;
        P::String clean(txt.c_str() + s, e - s);

        // Keep the callback string alive for the app's lifetime: LVGL only
        // stores the pointer we hand it.
        const char* onclickPtr = nullptr;
        if (!onclickAttr.empty() && elementIdx >= 0 &&
            elementIdx < (int)g_core.app().elements.size()) {
            g_core.app().elements[elementIdx]->staticItemOnclicks.push_back(onclickAttr);
            onclickPtr = g_core.app().elements[elementIdx]->staticItemOnclicks.back().c_str();
        }
        list_append_item(list, elementIdx, idx++, clean.c_str(), onclickPtr);
        p = close + 7;
    }
}

void create_list(const char* astart, const char* aend, const char* content, lv_obj_t* parent) {
    auto attrs     = parseCommonAttrs(astart, aend);
    auto bind      = getAttr(astart, aend, "bind");
    auto itemClick = getAttr(astart, aend, "itemClick");

    int32_t x = getAttrCoordW(astart, aend, "x", 0, parent);
    int32_t y = getAttrCoordH(astart, aend, "y", 0, parent);
    int32_t w = getAttrSize(astart, aend, "w");
    int32_t h = getAttrSize(astart, aend, "h");

    ensureId(attrs, "_list", !bind.empty() || !itemClick.empty());

    lv_obj_t* list = lv_list_create(parent);
    set_pos(list, x, y);
    if (w != 0) lv_obj_set_width(list, w);
    if (h != 0) lv_obj_set_height(list, h);

    {
        P::String cls = attrs.hasDynamicClass
            ? render_template(attrs.cssClass.c_str())
            : attrs.cssClass;
        Widget{list}.applyCss("list", attrs.id.c_str(), cls.c_str(), std::string_view(astart, static_cast<size_t>(aend - astart)));
    }

    // Inline bg/color on the container; per-item styling stays LVGL default.
    P::String bgcolorAttr = getAttr(astart, aend, "bgcolor");
    P::String colorAttr   = getAttr(astart, aend, "color");
    if (!bgcolorAttr.empty()) Widget{list}.applyBgColor(parse_color(bgcolorAttr.c_str()));
    if (!colorAttr.empty())   Widget{list}.applyColor(parse_color(colorAttr.c_str()));

    ElementDesc d;
    d.id           = attrs.id.c_str();
    d.obj          = list;
    d.bind         = bind.c_str();
    d.itemClick    = itemClick.c_str();
    d.visibleBind  = attrs.hasDynamicVisible  ? attrs.visible.c_str()  : nullptr;
    d.disabledBind = attrs.hasDynamicDisabled ? attrs.disabled.c_str() : nullptr;
    d.zIndex       = attrs.zIndex;
    int elemIdx = g_core.app().addElement(d);
    applyStaticDisabled(list, attrs);

    if (elemIdx >= 0 && elemIdx < (int)g_core.app().elements.size()) {
        g_core.app().elements[elemIdx]->is_list = true;

        if (!bind.empty()) {
            list_rebuild_from_array(list, elemIdx, P::String(bind.c_str()));
        } else {
            parse_static_items(content, list, elemIdx);
        }
    }

    LOG_I(Log::UI, "create_list: id=%s bind=%s items=%d",
          attrs.id.c_str(), bind.c_str(), (int) lv_obj_get_child_cnt(list));
}
