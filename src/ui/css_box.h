#pragma once
/**
 * css_box.h — the box of an element from CSS: margin, border, outline, box-shadow, overflow and
 * linear-gradient() backgrounds, put on its LVGL object. What maps to what: docs/CSS.md.
 */

#include "lvgl.h"
#include "ui/css_parser.h"

namespace UI {

// The box properties among an element's winning declarations (cascade order); em is its font size
void applyCssBox(lv_obj_t* o, const P::Array<CssDecl>& decls, float em);

}  // namespace UI
