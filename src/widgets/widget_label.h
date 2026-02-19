#pragma once
/**
 * widget_label.h — Label widget
 *
 * Usage:
 *   Label title = { .text = "Hello", .color = 0x00FFFF, .font = 48, .align = center };
 *   title.create(parent);
 *   title.setText("World");
 */

#include "widgets/widget_common.h"

struct Label : Widget {
    // Content
    P::String   text;
    int         font     = 0;

    // Visual
    uint32_t    color    = 0xFFFFFF;
    uint32_t    bgcolor  = NO_COLOR;
    int         radius   = 0;

    // Layout
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;

    // State
    bool        visible  = true;

    // ---- Lifecycle ----

    Label& create(lv_obj_t* parent) {
        handle = lv_label_create(parent);
        lv_label_set_text(handle, text.c_str());
        applyColor(color);
        applyBgColor(bgcolor);
        applyFont(font);
        applyRadius(radius);
        applyLayout(align, x, y, w, h);
        applyVisible(visible);

        if (w > 0 || h > 0) {
            lv_obj_set_style_text_align(handle, LV_TEXT_ALIGN_CENTER, 0);
        }
        return *this;
    }

    // ---- Runtime setters ----

    Label& setText(const P::String& t) {
        text = t;
        if (handle) lv_label_set_text(handle, text.c_str());
        return *this;
    }

    Label& setColor(uint32_t c) {
        color = c;
        applyColor(c);
        return *this;
    }

    Label& setBgColor(uint32_t c) {
        bgcolor = c;
        applyBgColor(c);
        return *this;
    }

    Label& setVisible(bool v) {
        visible = v;
        if (!handle) return *this;
        if (v) lv_obj_clear_flag(handle, LV_OBJ_FLAG_HIDDEN);
        else   lv_obj_add_flag(handle, LV_OBJ_FLAG_HIDDEN);
        return *this;
    }
};
