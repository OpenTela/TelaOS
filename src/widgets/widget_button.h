#pragma once
/**
 * widget_button.h — Button widget
 *
 * Usage (simple):
 *   Button btn = { .text = "OK", .bgcolor = 0x0066FF, .onClick = [&]{ doStuff(); } };
 *   btn.create(parent);
 *
 * Usage (with icon):
 *   Button btn = { .text = "Save", .iconPath = "C:/icons/save.png", .iconSize = 24 };
 *   btn.create(parent);
 */

#include "widgets/widget_common.h"

struct Button : Widget {
    // Content
    P::String   text;
    P::String   iconPath;             // Icon path (e.g. "C:/app/resources/icon.png")
    int         iconSize = 24;        // Icon dimensions (square)
    int         font     = 0;

    // Visual
    uint32_t    color    = NO_COLOR;
    uint32_t    bgcolor  = NO_COLOR;
    int         radius   = 0;

    // Layout
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;

    // State
    bool        visible  = true;

    // Actions
    Action      onClick  = nullptr;
    Action      onHold   = nullptr;

    // Hints
    bool        forceLabel = false;   // Create label even if text is empty (for dynamic bindings)

    // Child widgets (accessible for HTML engine)
    Widget      label;                // Label inside button (if text)
    Widget      icon;                 // Icon image inside button (if iconPath set)

    // ---- Lifecycle ----

    Button& create(lv_obj_t* parent) {
        handle = lv_btn_create(parent);
        applyPressTransform();
        applyBgColor(bgcolor);
        applyRadius(radius);
        applyLayout(align, x, y, w, h);
        applyVisible(visible);

        bool hasIcon = !iconPath.empty();
        bool hasText = !text.empty();

        // If both icon and text — use flex row layout
        if (hasIcon && hasText) {
            lv_obj_set_flex_flow(handle, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(handle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(handle, 6, 0);
        }

        // Create icon if specified
        if (hasIcon) {
            icon.handle = lv_image_create(handle);
            lv_image_set_src(icon.handle, iconPath.c_str());
            lv_obj_set_size(icon.handle, iconSize, iconSize);
            if (!hasText) lv_obj_center(icon.handle);
        }

        // Create label if there's text (or forced for dynamic bindings)
        if (hasText || forceLabel) {
            label.handle = lv_label_create(handle);
            lv_label_set_text(label.handle, text.c_str());
            if (!hasIcon) lv_obj_center(label.handle);
            label.applyFont(font);  // font=0 → Ubuntu_16px default (consistent with Label)
            if (color != NO_COLOR) {
                lv_obj_set_style_text_color(label.handle, lv_color_hex(color), 0);
            }
        }

        if (onClick) WidgetCallbacks::bind(handle, LV_EVENT_CLICKED, onClick);
        if (onHold)  WidgetCallbacks::bind(handle, LV_EVENT_LONG_PRESSED, onHold);

        return *this;
    }

    // ---- Runtime setters ----

    Button& setText(const P::String& t) {
        text = t;
        if (label.handle) lv_label_set_text(label.handle, text.c_str());
        return *this;
    }

    Button& setBgColor(uint32_t c) {
        bgcolor = c;
        applyBgColor(c);
        return *this;
    }

    Button& setVisible(bool v) {
        visible = v;
        if (!handle) return *this;
        if (v) lv_obj_clear_flag(handle, LV_OBJ_FLAG_HIDDEN);
        else   lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN);
        return *this;
    }
};
