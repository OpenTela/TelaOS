#pragma once
/**
 * widget_build.h — ui::build() convenience function
 *
 * Creates all widgets in one call using C++17 fold expressions.
 *
 * Usage:
 *   ui::build(parent, 0x1A1A2E,
 *       title, temp, btnRefresh
 *   );
 */

#include <lvgl.h>

namespace ui {

/// Build widgets on a parent with background color.
template<typename... Widgets>
void build(lv_obj_t* parent, uint32_t bg, Widgets&... widgets) {
    if (!parent) return;
    lv_obj_set_style_bg_color(parent, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    (widgets.create(parent), ...);
}

} // namespace ui
