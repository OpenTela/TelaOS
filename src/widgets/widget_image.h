#pragma once
#include "widgets/widget_common.h"

struct Image : Widget {
    P::String   src;              // "C:/path/to/image.png"
    int         size     = 0;    // square shorthand (sets both w/h)

    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;

    bool        visible  = true;

    Image& create(lv_obj_t* parent) {
        handle = lv_image_create(parent);
        if (!src.empty()) lv_image_set_src(handle, src.c_str());
        if (size > 0) lv_obj_set_size(handle, size, size);
        applyLayout(align, x, y, w, h);
        applyVisible(visible);

        return *this;
    }

    Image& setSrc(const P::String& s) {
        src = s;
        if (handle) lv_image_set_src(handle, src.c_str());
        return *this;
    }
};
