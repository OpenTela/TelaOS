#pragma once
#include "widgets/widget_common.h"

struct Slider : Widget {
    int         min_val  = 0;
    int         max_val  = 100;
    int         value    = 0;

    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;

    bool        visible  = true;
    Action      onChange = nullptr;

    Slider& create(lv_obj_t* parent) {
        handle = lv_slider_create(parent);
        lv_slider_set_range(handle, min_val, max_val);
        lv_slider_set_value(handle, value, LV_ANIM_OFF);
        applyLayout(align, x, y, w, h);
        applyVisible(visible);

        if (onChange) WidgetCallbacks::bind(handle, LV_EVENT_VALUE_CHANGED, onChange);

        return *this;
    }

    int getValue() const {
        return handle ? lv_slider_get_value(handle) : value;
    }

    Slider& setValue(int v) {
        value = v;
        if (handle) lv_slider_set_value(handle, v, LV_ANIM_OFF);
        return *this;
    }
    Slider& setValue(const P::String& s) { return setValue(std::atoi(s.c_str())); }
};
