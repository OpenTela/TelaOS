#pragma once
/**
 * css_layout.h — where CSS puts an element: flex containers and items (display: flex,
 * flex-direction, flex-wrap, justify-content, align-items, align-content, flex-grow), position
 * with left/top/right/bottom, and visibility. What maps to what: docs/CSS.md.
 */

#include "lvgl.h"
#include "ui/css_parser.h"

namespace UI {

// The layout properties among an element's winning declarations (cascade order); em is its font size
void applyCssLayout(lv_obj_t* o, const P::Array<CssDecl>& decls, float em);

}  // namespace UI
