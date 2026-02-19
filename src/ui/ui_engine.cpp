/**
 * ui_engine.cpp - UI::Engine facade and Lua API free functions
 * 
 * Extracted from ui_html.cpp — thin wrappers over internal functions.
 */

#include "lvgl.h"
#include "ui/ui_engine.h"
#include "ui/ui_html_internal.h"
#include "widgets/widget_methods.h"
#include "core/state_store.h"
#include "utils/string_utils.h"
#include "utils/log_config.h"

#include <cstdlib>
#include <cstring>
#include <string>

static const char* TAG = "ui_engine";

using namespace UI::StringUtils;

// Forward declarations from ui_widget_builder.cpp
int32_t parse_coord_w(const char* s);
int32_t parse_coord_h(const char* s);
const char* ui_get_current_page_id();

// Forward declarations from ui_html.cpp
bool ui_trigger_click(const char* id);
bool ui_call_function(const char* funcName);

namespace UI {

// Focused textarea (set by focusInput, used by ui type command)
static lv_obj_t* s_focusedTextarea = nullptr;

lv_obj_t* getFocusedTextarea() { return s_focusedTextarea; }

bool getWidgetCenter(const char* id, int& cx, int& cy) {
    lv_obj_t* obj = ui_get_internal(id);
    if (!obj) return false;
    
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    cx = (coords.x1 + coords.x2) / 2;
    cy = (coords.y1 + coords.y2) / 2;
    return true;
}

bool triggerClick(const char* widgetId) {
    return ui_trigger_click(widgetId);
}

bool callFunction(const char* funcName) {
    return ui_call_function(funcName);
}

// ============ Singleton ============

Engine& Engine::instance() {
    static Engine engine;
    return engine;
}

// ============ Lifecycle ============

void Engine::init() {
    ui_html_init_internal();
}

int Engine::render(const char* html) {
    return ui_html_render_internal(html);
}

void Engine::clear() {
    s_focusedTextarea = nullptr;
    ui_clear_internal();
}

// ============ Element access ============

lv_obj_t* Engine::get(const char* id) {
    return ui_get_internal(id);
}

void Engine::setText(const char* id, const char* text) {
    ui_set_text_internal(id, text);
}

void Engine::showPage(const char* id) {
    ui_show_page_internal(id);
}

bool Engine::hasPage(const char* id) {
    return find_page_index(id) != INVALID_INDEX;
}

const char* Engine::currentPageId() const {
    return ::ui_get_current_page_id();
}

// ============ Timer getters ============

int Engine::timerCount() const {
    return static_cast<int>(timers.size());
}

int Engine::timerInterval(int i) const {
    return (i < static_cast<int>(timers.size())) ? timers[i].interval_ms : 0;
}

const char* Engine::timerCallback(int i) const {
    return (i < static_cast<int>(timers.size())) ? timers[i].callback.c_str() : nullptr;
}

// ============ Script getters ============

const char* Engine::scriptCode() const {
    return script_code.c_str();
}

const char* Engine::scriptLang() const {
    return script_lang.c_str();
}

// ============ App metadata ============

const char* Engine::appVersion() const {
    return app_version.c_str();
}

const char* Engine::appOsRequirement() const {
    return app_os_requirement.c_str();
}

const char* Engine::appIcon() const {
    return app_icon.c_str();
}

bool Engine::appReadonly() const {
    return app_readonly;
}

void Engine::setAppPath(const char* path) {
    app_path = path ? path : "";
    LOG_I(Log::UI, "App path set to: %s", app_path.c_str());
}

const char* Engine::appPath() const {
    return app_path.c_str();
}

// ============ State getters ============

int Engine::stateCount() const {
    return static_cast<int>(State::store().count());
}

const char* Engine::stateVarName(int i) const {
    static char buf[ATTR_VAL_LEN];
    auto name = State::store().nameAt(i);
    strncpy(buf, name.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

const char* Engine::stateVarType(int i) const {
    return Store::typeToString(State::store().typeAt(i));
}

const char* Engine::stateVarDefault(int i) const {
    static char buf[ATTR_VAL_LEN];
    auto def = State::store().defaultAt(i);
    strncpy(buf, def.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

// ============ Handler setters ============

void Engine::setOnClickHandler(OnClickHandler handler) {
    g_onclick_handler = handler;
}

void Engine::setOnTapHandler(OnTapHandler handler) {
    g_ontap_handler = handler;
}

void Engine::setOnHoldHandler(OnHoldHandler handler) {
    g_onhold_handler = handler;
}

void Engine::setOnHoldXYHandler(OnHoldXYHandler handler) {
    g_onhold_xy_handler = handler;
}

void Engine::setStateChangeHandler(StateChangeHandler handler) {
    g_state_change_handler = handler;
}

// ============ Widget sync ============

void Engine::syncWidgetValues() {
    g_updating_from_binding = true;
    
    for (size_t i = 0; i < elements.size(); i++) {
        if (elements[i]->bind.empty()) continue;
        
        const char *value = get_state_value(elements[i]->bind.c_str());
        if (!value) continue;
        
        lv_obj_t *obj = elements[i]->obj();
        
        if (lv_obj_check_type(obj, &lv_switch_class)) {
            bool checked = toBool(value);
            if (checked) {
                lv_obj_add_state(obj, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(obj, LV_STATE_CHECKED);
            }
        } else if (lv_obj_check_type(obj, &lv_slider_class)) {
            lv_slider_set_value(obj, atoi(value), LV_ANIM_OFF);
        } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
            lv_textarea_set_text(obj, value);
        }
    }
    
    g_updating_from_binding = false;
    LOG_I(Log::UI, "Widget values synced from state");
}

// ============ FREE FUNCTIONS FOR LUA API ============

lv_obj_t* getElementById(const char* id) {
    return ui_get_internal(id);
}

bool focusInput(const char* id) {
    lv_obj_t* obj = ui_get_internal(id);
    if (!obj) {
        LOG_W(Log::UI, "focusInput: widget '%s' not found", id);
        return false;
    }
    
    if (!lv_obj_check_type(obj, &lv_textarea_class)) {
        LOG_W(Log::UI, "focusInput: widget '%s' is not an input", id);
        return false;
    }
    
    // Walk up parent chain to find page
    int page_idx = -1;
    lv_obj_t* parent = lv_obj_get_parent(obj);
    while (parent) {
        for (int i = 0; i < page_count; i++) {
            if (page_objs[i] == parent) {
                page_idx = i;
                break;
            }
        }
        if (page_idx >= 0) break;
        parent = lv_obj_get_parent(parent);
    }
    
    if (page_idx < 0 || !parent) {
        LOG_W(Log::UI, "focusInput: page not found for widget '%s'", id);
        return false;
    }
    
    // Create keyboard if needed
    if (!g_keyboards[page_idx]) {
        g_keyboards[page_idx] = lv_keyboard_create(parent);
        lv_obj_set_size(g_keyboards[page_idx], lv_pct(FULL_SIZE_PCT), lv_pct(KEYBOARD_HEIGHT_PCT));
        lv_obj_align(g_keyboards[page_idx], LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    
    lv_keyboard_set_textarea(g_keyboards[page_idx], obj);
    lv_obj_clear_flag(g_keyboards[page_idx], LV_OBJ_FLAG_HIDDEN);
    s_focusedTextarea = obj;
    
    LOG_I(Log::UI, "focusInput: focused '%s'", id);
    return true;
}

bool setWidgetAttr(const char* id, const char* attr, const char* value) {
    Element* elem = nullptr;
    for (const auto& el : elements) {
        if (el->id == id) {
            elem = el.get();
            break;
        }
    }
    
    if (!elem || !elem->obj()) {
        LOG_W(Log::UI, "setWidgetAttr: widget '%s' not found", id);
        return false;
    }
    
    if (strcmp(attr, "bgcolor") == 0) {
        UI::setBgColor(elem->box(), parse_color(value));
    } else if (strcmp(attr, "color") == 0) {
        UI::setColor(elem->w, parse_color(value));
    } else if (strcmp(attr, "text") == 0) {
        UI::setText(elem->w, value);
    } else if (strcmp(attr, "visible") == 0) {
        bool visible = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        UI::setVisible(elem->box(), visible);
    } else if (strcmp(attr, "x") == 0) {
        UI::setX(elem->box(), parse_coord_w(value));
    } else if (strcmp(attr, "y") == 0) {
        UI::setY(elem->box(), parse_coord_h(value));
    } else if (strcmp(attr, "w") == 0) {
        UI::setWidth(elem->box(), parse_coord_w(value));
    } else if (strcmp(attr, "h") == 0) {
        UI::setHeight(elem->box(), parse_coord_h(value));
    } else if (strcmp(attr, "z-index") == 0) {
        lv_obj_t* target = elem->parentObj ? elem->parentObj : elem->w.handle;
        int z = atoi(value);
        if (z > 0) lv_obj_move_foreground(target);
        else if (z < 0) lv_obj_move_to_index(target, 0);
    } else {
        LOG_W(Log::UI, "setWidgetAttr: unknown attribute '%s'", attr);
        return false;
    }
    
    LOG_D(Log::UI, "setWidgetAttr: %s.%s = %s", id, attr, value);
    return true;
}

P::String getWidgetAttr(const char* id, const char* attr) {
    Element* elem = nullptr;
    for (const auto& el : elements) {
        if (el->id == id) {
            elem = el.get();
            break;
        }
    }
    
    if (!elem || !elem->obj()) {
        LOG_W(Log::UI, "getWidgetAttr: widget '%s' not found", id);
        return "";
    }
    
    char buf[64];
    
    if (strcmp(attr, "text") == 0) {
        return UI::getText(elem->w);
    } else if (strcmp(attr, "visible") == 0) {
        return UI::isVisible(elem->box()) ? "true" : "false";
    } else if (strcmp(attr, "x") == 0) {
        snprintf(buf, sizeof(buf), "%d", UI::getX(elem->box()));
        return buf;
    } else if (strcmp(attr, "y") == 0) {
        snprintf(buf, sizeof(buf), "%d", UI::getY(elem->box()));
        return buf;
    } else if (strcmp(attr, "w") == 0) {
        snprintf(buf, sizeof(buf), "%d", UI::getWidth(elem->box()));
        return buf;
    } else if (strcmp(attr, "h") == 0) {
        snprintf(buf, sizeof(buf), "%d", UI::getHeight(elem->box()));
        return buf;
    } else if (strcmp(attr, "z-index") == 0) {
        lv_obj_t* target = elem->parentObj ? elem->parentObj : elem->w.handle;
        snprintf(buf, sizeof(buf), "%d", lv_obj_get_index(target));
        return buf;
    }
    
    LOG_W(Log::UI, "getWidgetAttr: unknown or unsupported attribute '%s'", attr);
    return "";
}

} // namespace UI
