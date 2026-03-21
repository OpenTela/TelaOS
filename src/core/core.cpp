/**
 * core/core.cpp - Core implementation
 * 
 * Extracted from ui_html.cpp — thin wrappers over internal functions.
 */

#include "lvgl.h"
#include "core/core.h"
#include "ui/ui_html_internal.h"
#include "widgets/widget_methods.h"
#include "core/state_store.h"
#include "utils/string_utils.h"
#include "utils/log_config.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "core/core.h"
static const char* TAG = "core";

using namespace UI::StringUtils;

// Forward declarations from ui_widget_builder.cpp
const char* ui_get_current_page_id();

// Forward declarations from ui_html.cpp
bool ui_trigger_click(const char* id);
bool ui_call_function(const char* funcName);


// Focused textarea (set by focusInput, used by ui type command)
static lv_obj_t* s_focusedTextarea = nullptr;

lv_obj_t* getFocusedTextarea() { return s_focusedTextarea; }

bool getWidgetCenter(const char* id, int& cx, int& cy) {
    lv_obj_t* obj = g_core.app().findElement(id);
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

// ============ Lifecycle ============

void Core::initDynamicApp(const char* appPath) {
    s_focusedTextarea = nullptr;
    m_app.~DynamicApp();
    new (&m_app) DynamicApp();
    m_store.clear();
    m_app.setAppPath(appPath ? appPath : "");
    if (appPath) {
        LOG_I(Log::UI, "Init v%s | path=%s", version(), m_app.appPath().c_str());
    } else {
        LOG_I(Log::UI, "Init v%s | UI reset", version());
    }
}

void Core::resetUI() {
    initDynamicApp(nullptr);
}

int Core::render(const char* html) {
    return ui_html_render_internal(html);
}

// ============ Element access ============

lv_obj_t* Core::get(const char* id) {
    return g_core.app().findElement(id);
}

void Core::setText(const char* id, const char* text) {
    ui_set_text_internal(id, text);
}

void Core::showPage(const char* id) {
    ui_show_page_internal(id);
}

bool Core::hasPage(const char* id) {
    return g_core.app().findPage(id) != INVALID_INDEX;
}

const char* Core::currentPageId() const {
    return ::ui_get_current_page_id();
}

bool Core::isKeyboardVisible() const {
    for (int i = 0; i < g_core.app().pageCount(); i++) {
        if (g_core.app().keyboards[i] && !lv_obj_has_flag(g_core.app().keyboards[i], LV_OBJ_FLAG_HIDDEN)) {
            return true;
        }
    }
    return false;
}

void Core::dismissKeyboard() {
    for (int i = 0; i < g_core.app().pageCount(); i++) {
        if (g_core.app().keyboards[i]) {
            lv_obj_add_flag(g_core.app().keyboards[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ============ Timer getters ============

int Core::timerCount() const {
    return static_cast<int>(g_core.app().timerCount());
}

int Core::timerInterval(int i) const {
    return (i < static_cast<int>(g_core.app().timerCount())) ? g_core.app().timer(i).interval_ms : 0;
}

const char* Core::timerCallback(int i) const {
    return (i < static_cast<int>(g_core.app().timerCount())) ? g_core.app().timer(i).callback.c_str() : nullptr;
}

// ============ Script getters ============

const char* Core::scriptCode() const {
    return g_core.app().scriptCode().c_str();
}

const char* Core::scriptLang() const {
    return g_core.app().scriptLang().c_str();
}

// ============ App metadata ============

const char* Core::appVersion() const {
    return g_core.app().appVersion().c_str();
}

const char* Core::appOsRequirement() const {
    return g_core.app().appOsRequirement().c_str();
}

const char* Core::appIcon() const {
    return g_core.app().appIcon().c_str();
}

bool Core::appReadonly() const {
    return g_core.app().appReadonly();
}

// ============ State getters ============

int Core::stateCount() const {
    return static_cast<int>(g_core.store().count());
}

const char* Core::stateVarName(int i) const {
    static char buf[ATTR_VAL_LEN];
    auto name = g_core.store().nameAt(i);
    strncpy(buf, name.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

const char* Core::stateVarType(int i) const {
    return Store::typeToString(g_core.store().typeAt(i));
}

const char* Core::stateVarDefault(int i) const {
    static char buf[ATTR_VAL_LEN];
    auto def = g_core.store().defaultAt(i);
    strncpy(buf, def.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

// ============ Handler setters ============

void Core::setOnClickHandler(OnClickHandler handler) {
    g_onclick_handler = handler;
}

void Core::setOnTapHandler(OnTapHandler handler) {
    g_ontap_handler = handler;
}

void Core::setOnHoldHandler(OnHoldHandler handler) {
    g_onhold_handler = handler;
}

void Core::setOnHoldXYHandler(OnHoldXYHandler handler) {
    g_onhold_xy_handler = handler;
}

void Core::setStateChangeHandler(StateChangeHandler handler) {
    g_state_change_handler = handler;
}

// ============ Widget sync ============

void Core::syncWidgetValues() {
    g_core.app().beginBinding();
    
    for (size_t i = 0; i < g_core.app().elements.size(); i++) {
        if (g_core.app().elements[i]->bind.empty()) continue;
        
        const char *value = get_state_value(g_core.app().elements[i]->bind.c_str());
        if (!value) continue;
        
        lv_obj_t *obj = g_core.app().elements[i]->obj();
        
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
    
    g_core.app().endBinding();
    LOG_I(Log::UI, "Widget values synced from state");
}

// ============ FREE FUNCTIONS FOR LUA API ============

lv_obj_t* getElementById(const char* id) {
    return g_core.app().findElement(id);
}

bool UI::focusInput(const char* id) {
    lv_obj_t* obj = g_core.app().findElement(id);
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
        for (int i = 0; i < g_core.app().pageCount(); i++) {
            if (g_core.app().pageObj(i) == parent) {
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
    if (!g_core.app().keyboards[page_idx]) {
        g_core.app().keyboards[page_idx] = lv_keyboard_create(parent);
        lv_obj_set_size(g_core.app().keyboards[page_idx], lv_pct(FULL_SIZE_PCT), lv_pct(KEYBOARD_HEIGHT_PCT));
        lv_obj_align(g_core.app().keyboards[page_idx], LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(g_core.app().keyboards[page_idx], keyboard_event_handler, LV_EVENT_ALL, nullptr);
        lv_obj_add_flag(g_core.app().keyboards[page_idx], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(g_core.app().keyboards[page_idx], LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    
    lv_keyboard_set_textarea(g_core.app().keyboards[page_idx], obj);
    lv_obj_clear_flag(g_core.app().keyboards[page_idx], LV_OBJ_FLAG_HIDDEN);
    s_focusedTextarea = obj;
    
    LOG_I(Log::UI, "focusInput: focused '%s'", id);
    return true;
}

bool UI::setWidgetAttr(const char* id, const char* attr, const char* value) {
    UI::Element* elem = nullptr;
    for (const auto& el : g_core.app().elements) {
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

P::String UI::getWidgetAttr(const char* id, const char* attr) {
    UI::Element* elem = nullptr;
    for (const auto& el : g_core.app().elements) {
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

