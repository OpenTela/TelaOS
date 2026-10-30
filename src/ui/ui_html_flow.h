/**
 * ui_html_flow.h — standard HTML elements (MDN element reference) for the TelaOS UI engine.
 *
 * TelaOS widgets (label, button, div, table...) place themselves with x/y/align.
 * Standard HTML flows instead: blocks stack top to bottom, inline text wraps.
 * This layer adds that flow and handles only tags TelaOS did not know before
 * (they used to be skipped), so existing apps render exactly as before.
 *
 * Full tag table: docs/HTML_TAGS.md
 */
#pragma once

#include "lvgl.h"  // first: maps the TelaOS-style `struct _lv_obj_t` forward declarations

#include <cstddef>

namespace HtmlFlow {

// Tag is rendered by this layer (lower-case name)
bool handles(const char* tag);
// No closing tag: br, hr, img, wbr, col, source, track, area, embed, meta, link, base...
bool isVoid(const char* tag);
// May contain itself (div-like): needs a depth-aware closing tag search
bool isNestable(const char* tag);
// <input> that follows HTML (has type=): checkbox, radio, range, submit... (else TelaOS input)
bool isHtmlInput(const char* astart, const char* aend);
// End tag may be omitted: li, dt, dd, p, td, th, tr, thead, tbody, tfoot, option, rt, rp
bool hasOptionalEnd(const char* tag);
// Where such an element ends without its end tag: at the next sibling that implies it
// (<li>a<li>b, <td>1<td>2, <p>a<div>) or at the parent's end tag. content = after the start tag.
const char* impliedEnd(const char* tag, const char* content, const char* end);

// Render one element. content = inner HTML (empty for void elements).
void create(const char* tag, const char* astart, const char* aend, const char* content, lv_obj_t* parent);

// Direct text and inline HTML (<b>, <a href>, <br>...) at p, up to the next block or TelaOS
// element, rendered as one line box. Returns the end of the run, or p if there is none.
const char* inlineRun(const char* p, const char* end, lv_obj_t* parent);

// Content of a page or TelaOS container holds HTML: direct text or a standard HTML element.
// Such a container flows like a browser page (one implicit root box, blocks top to bottom).
bool hasHtml(const char* html, size_t len);
// TelaOS widget placed by coordinates (x, y, align): it stays where it says, outside the flow
bool isPositioned(const char* tag, const char* astart, const char* aend);
// The flow box of a container: the container itself if it is one, else its implicit root
lv_obj_t* flowBox(lv_obj_t* parent);

// ui.setText() on an element built here (text with spans, a block, a checkbox, <li>, <dialog>).
// false: not ours, the caller handles it.
bool setText(lv_obj_t* obj, const char* text);
// New app: forget the objects of the previous one
void reset();

// Content of a TelaOS container (div, td) holding direct text or inline HTML tags:
// before this layer such text was silently dropped.
bool hasFlowContent(const char* html, size_t len);
// Make a TelaOS container stack its children like an HTML block (div without flex=)
void makeFlow(lv_obj_t* container);
void renderContent(const char* html, size_t len, lv_obj_t* container);

}  // namespace HtmlFlow
