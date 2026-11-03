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
// For a state (:active...) the decoration and the spacings go on that state; one line (nowrap,
// ellipsis) belongs to the element itself (state 0)
void applyCssText(lv_obj_t* o, const P::Array<CssDecl>& decls, float em, uint16_t state = 0);

// ::placeholder: color (with its alpha), opacity, letter-spacing and text-decoration of the hint a
// text field shows while it is empty (LV_PART_TEXTAREA_PLACEHOLDER)
void applyCssPlaceholder(lv_obj_t* o, const P::Array<CssDecl>& decls, float em);

}  // namespace UI
