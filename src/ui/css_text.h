#pragma once
/**
 * css_text.h — the text of an element from CSS: text-decoration, letter-spacing, line-height,
 * white-space: nowrap and text-overflow: ellipsis. LVGL inherits these, so a block's settings
 * reach the text inside it. What maps to what: docs/CSS.md.
 */

#include "lvgl.h"
#include "ui/css_parser.h"

namespace UI {

// The text properties among an element's winning declarations (cascade order); em is its font size
void applyCssText(lv_obj_t* o, const P::Array<CssDecl>& decls, float em);

}  // namespace UI
