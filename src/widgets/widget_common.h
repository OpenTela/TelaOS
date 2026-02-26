#pragma once
/**
 * widget_common.h — Base struct and helpers for widget system
 *
 * Shared by both Native apps and HTML engine.
 * Widget provides common LVGL operations inherited by all widgets.
 * No virtual methods → aggregates preserved → designated init works.
 */

#include <lvgl.h>

// Testing: widget ID for mock capture (no-op in production)
#ifndef _unique_id
inline void _unique_id(lv_obj_t*, const char*) {}
#endif

#include <functional>
#include <vector>
#include <string>
#include <cstdlib>
#include "utils/font.h"
#include "utils/psram_alloc.h"

// ============ Types ============

using Action = std::function<void()>;

// ============ Sentinel ============

constexpr uint32_t NO_COLOR = UINT32_MAX;

// ============ Align constants ============

constexpr lv_align_t top_left     = LV_ALIGN_TOP_LEFT;
constexpr lv_align_t top_mid      = LV_ALIGN_TOP_MID;
constexpr lv_align_t top_right    = LV_ALIGN_TOP_RIGHT;
constexpr lv_align_t left_mid     = LV_ALIGN_LEFT_MID;
constexpr lv_align_t center       = LV_ALIGN_CENTER;
constexpr lv_align_t right_mid    = LV_ALIGN_RIGHT_MID;
constexpr lv_align_t bottom_left  = LV_ALIGN_BOTTOM_LEFT;
constexpr lv_align_t bottom_mid   = LV_ALIGN_BOTTOM_MID;
constexpr lv_align_t bottom_right = LV_ALIGN_BOTTOM_RIGHT;

// ============ Widget ============

struct Widget {
    lv_obj_t* handle = nullptr;

    // ---- Appearance ----

    void applyColor(uint32_t color) {
        if (!handle || color == NO_COLOR) return;
        lv_obj_set_style_text_color(handle, lv_color_hex(color), 0);
    }
    void setColor(uint32_t color) { applyColor(color); }

    void applyBgColor(uint32_t bgcolor) {
        if (!handle || bgcolor == NO_COLOR) return;
        lv_obj_set_style_bg_color(handle, lv_color_hex(bgcolor), 0);
        lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    }
    void setBgColor(uint32_t bgcolor) { applyBgColor(bgcolor); }

    void applyFont(int size) {
        if (!handle) return;
        const lv_font_t* f = (size > 0) ? Font::get(size) : UI::Font::defaultFont();
        if (f) lv_obj_set_style_text_font(handle, f, 0);
    }

    void applyRadius(int radius) {
        if (!handle || radius < 0) return;
        lv_obj_set_style_radius(handle, radius, 0);
    }

    // ---- Layout ----

    void applyLayout(lv_align_t align, int x, int y, int w, int h) {
        if (!handle) return;
        lv_obj_align(handle, align, x, y);
        if (w > 0) lv_obj_set_width(handle, w);
        if (h > 0) lv_obj_set_height(handle, h);
    }

    void applyVisible(bool visible) {
        if (!handle) return;
        if (visible) {
            lv_obj_clear_flag(handle, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN);
        }
    }
    void setVisible(bool visible) { applyVisible(visible); }

    void setX(int x) {
        if (handle) lv_obj_set_x(handle, x);
    }

    void setY(int y) {
        if (handle) lv_obj_set_y(handle, y);
    }

    void setWidth(int w) {
        if (handle) lv_obj_set_width(handle, w);
    }

    void setHeight(int h) {
        if (handle) lv_obj_set_height(handle, h);
    }

    int getX() const {
        return handle ? lv_obj_get_x(handle) : 0;
    }

    int getY() const {
        return handle ? lv_obj_get_y(handle) : 0;
    }

    int getWidth() const {
        return handle ? lv_obj_get_width(handle) : 0;
    }

    int getHeight() const {
        return handle ? lv_obj_get_height(handle) : 0;
    }

    bool isVisible() const {
        return handle ? !lv_obj_has_flag(handle, LV_OBJ_FLAG_HIDDEN) : false;
    }

    const char* getText() const {
        if (!handle) return "";
        if (lv_obj_check_type(handle, &lv_label_class)) {
            return lv_label_get_text(handle);
        } else if (lv_obj_check_type(handle, &lv_textarea_class)) {
            return lv_textarea_get_text(handle);
        }
        return "";
    }

    void setText(const char* text) {
        if (!handle || !text) return;
        if (lv_obj_check_type(handle, &lv_label_class)) {
            lv_label_set_text(handle, text);
        } else if (lv_obj_check_type(handle, &lv_textarea_class)) {
            lv_textarea_set_text(handle, text);
        }
    }

    // ---- Events ----

    /// Bind LVGL event with raw callback + user data pointer
    void on(lv_event_code_t event, lv_event_cb_t cb, void* data = nullptr) {
        if (handle) lv_obj_add_event_cb(handle, cb, event, data);
    }

    /// Bind LVGL event with element index (HTML engine shorthand)
    void on(lv_event_code_t event, lv_event_cb_t cb, int idx) {
        if (handle) lv_obj_add_event_cb(handle, cb, event, (void*)(intptr_t)idx);
    }

    // ---- Button-specific ----

    void applyPressTransform() {
        if (!handle) return;
        lv_obj_set_style_transform_width(handle, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(handle, 0, LV_STATE_PRESSED);
    }

    // ---- CSS styling (implemented in widget_common.cpp) ----

    /// Apply one or more CSS class names (space-separated) — legacy
    void applyStyle(const P::String& classNames);

    /// Apply all matching CSS rules (tag, #id, .class cascade)
    void applyCss(const char* tag, const char* id, const char* classNames);
};

// ============ Callback storage ============

namespace WidgetCallbacks {

    inline std::vector<Action*> s_actions;

    inline void bind(lv_obj_t* obj, lv_event_code_t event, const Action& fn) {
        if (!obj || !fn) return;
        auto* stored = new Action(fn);
        s_actions.push_back(stored);
        lv_obj_add_event_cb(obj, [](lv_event_t* e) {
            auto* f = static_cast<Action*>(lv_event_get_user_data(e));
            if (f && *f) (*f)();
        }, event, stored);
    }

    inline void cleanup() {
        for (auto* a : s_actions) delete a;
        s_actions.clear();
    }

} // namespace WidgetCallbacks
