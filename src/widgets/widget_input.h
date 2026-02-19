#pragma once
#include "widgets/widget_common.h"

struct Input : Widget {
    P::String   placeholder;
    P::String   text;
    bool        password    = false;
    bool        multiline   = false;

    lv_align_t  align       = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;

    bool        visible     = true;
    Action      onEnter     = nullptr;

    Input& create(lv_obj_t* parent) {
        handle = lv_textarea_create(parent);
        lv_textarea_set_one_line(handle, !multiline);
        lv_textarea_set_placeholder_text(handle, placeholder.c_str());
        if (!text.empty()) lv_textarea_set_text(handle, text.c_str());
        if (password) lv_textarea_set_password_mode(handle, true);
        applyLayout(align, x, y, w, h);
        applyFont(0);  // Default Ubuntu_16px (textarea has no font= attr typically)
        applyVisible(visible);

        return *this;
    }

    const char* getText() const {
        return handle ? lv_textarea_get_text(handle) : text.c_str();
    }

    Input& setText(const P::String& t) {
        text = t;
        if (handle) lv_textarea_set_text(handle, text.c_str());
        return *this;
    }
};
