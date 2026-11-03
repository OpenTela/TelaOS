#pragma once
/**
 * css_box.h — the box of an element from CSS: margin, border, outline, box-shadow, overflow,
 * linear-gradient() backgrounds, transform and transition, put on its LVGL object. What maps to
 * what: docs/CSS.md.
 */

#include "lvgl.h"
#include "ui/css_parser.h"

namespace UI {

// The box properties among an element's winning declarations (cascade order); em is its font size.
// For a state (:active is LV_STATE_PRESSED...) the border, outline, shadow and gradient go on that
// state; margin and overflow belong to the element itself (state 0).
void applyCssBox(lv_obj_t* o, const P::Array<CssDecl>& decls, float em, uint16_t state = 0);

// transform: translate(), scale(), rotate() — LVGL draws them, about the middle of the element
void applyCssTransform(lv_obj_t* o, const P::Array<CssDecl>& decls, float em, uint16_t state = 0);

// transition: the element's own (state 0), on the way into and out of every state it has rules for
void applyCssTransitions(lv_obj_t* o, const P::Array<CssStateDecls>& groups);

// What sticks out of o (a child's outline, shadow, transform) shows, as with overflow: visible in
// a browser, up to kOverflowRoom px: LVGL 9 draws the children of an object that lets them
// overflow within its own drawn margin. false clips them to o again.
constexpr int32_t kOverflowRoom = 24;
void letOverflow(lv_obj_t* o, bool visible = true);

}  // namespace UI
