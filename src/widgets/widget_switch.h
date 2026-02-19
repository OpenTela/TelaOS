#pragma once
#include "widgets/widget_common.h"

struct Switch : Widget {
    bool        checked  = false;

    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0;

    bool        visible  = true;
    Action      onChange = nullptr;

    Switch& create(lv_obj_t* parent) {
        handle = lv_switch_create(parent);
        if (checked) lv_obj_add_state(handle, LV_STATE_CHECKED);
        applyLayout(align, x, y, 0, 0);
        applyVisible(visible);

        if (onChange) WidgetCallbacks::bind(handle, LV_EVENT_VALUE_CHANGED, onChange);

        return *this;
    }

    bool isChecked() const {
        return handle ? lv_obj_has_state(handle, LV_STATE_CHECKED) : checked;
    }

    Switch& setChecked(bool v) {
        checked = v;
        if (!handle) return *this;
        if (v) lv_obj_add_state(handle, LV_STATE_CHECKED);
        else   lv_obj_clear_state(handle, LV_STATE_CHECKED);
        return *this;
    }
    Switch& setChecked(const P::String& s) { return setChecked(s == "true" || s == "1"); }
};
