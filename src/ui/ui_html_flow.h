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

#include "utils/psram_alloc.h"

#include <cstddef>
#include <string_view>

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

// <link rel="stylesheet" href>: the rules of its file are read now and count where the link stands,
// as a <style> there: in <app> for the whole app, on a page for what comes after it. An href from /
// is a file of the watch (/system/tela.css), another is in the app's resources/, as <img src>.
// With media, only if the screen fits it (media="(prefers-color-scheme: dark)"). rel="alternate
// stylesheet", disabled, an address of the network and a file that is not there are left out, as
// a browser leaves out what it does not load
void linkStylesheet(std::string_view rel, std::string_view href, std::string_view media, bool disabled);

// The color the text of host's HTML inherits: what html {} and body {} say (Css::canvas()), in
// place of the white of the dark screen. host: a page, after its content is built
void setTextColor(lv_obj_t* host, uint32_t rgb, uint8_t opa);

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
// A flow box: a block of an HTML page, its children stack top to bottom
bool inFlow(lv_obj_t* box);
// While the flow builds a TelaOS control in its lines (a <button> in a paragraph, <input
// type="submit">): one that flows (no x, y, align) takes a browser's size, before its CSS
bool browserSized();

// <label for="id">: a tap on `label` works the control with that id, as in a browser
void labelFor(lv_obj_t* label, const char* id);

// HTML inside a TelaOS <label> or <button>, as HTML allows it there: phrasing content (<b>, <a>,
// <span>, <img>, <br>...) and, in a label, its control (<input>, <textarea>). Anything else (a
// block, a TelaOS tag) and the text stays as before: <button><label font="32">OK</label></button>.
bool hasMarkup(const char* s, const char* e, bool controls);
// That text rendered into parent as one line box (a label, spans or a row of words), returned;
// its {var} stay live. fit: as wide as its text (a button's label), not as wide as the parent.
lv_obj_t* richText(lv_obj_t* parent, const char* s, const char* e, bool fit);
// The first form control inside o: the control of <label><input type="checkbox"> Remember</label>
lv_obj_t* firstControl(lv_obj_t* o);
// A tap on label works control, as <label for> does by id
void labelControl(lv_obj_t* label, lv_obj_t* control);

// ui.setText() on an element built here (text with spans, a block, a checkbox, <li>, <dialog>).
// false: not ours, the caller handles it.
bool setText(lv_obj_t* obj, const char* text);
// The text obj shows, as DOM textContent reads it: a label's, a paragraph's spans, a box's
// children in order (an <li> without its marker). A field, a checkbox, a slider have none.
P::String getText(lv_obj_t* obj);
// The box obj's content is in: a <li>'s body, a <dialog>'s window; obj itself for the others
lv_obj_t* contentOf(lv_obj_t* obj);
// Around CSS applied again to obj (a script changed a class): the margins this layer wrote into
// it go before, so that CSS's own come back; after, those it has now are its margins
void marginsAside(lv_obj_t* obj);
void restyled(lv_obj_t* obj, const char* tag);
// checked= from a script: a checkbox or a switch takes it, a radio turns the rest of its group
// off. No change event and no bind=, as a browser fires none. false: obj is none of them
bool setChecked(lv_obj_t* obj, bool on);
// New app: forget the objects of the previous one
void reset();

// Content of a TelaOS container (div, td) holding direct text or inline HTML tags:
// before this layer such text was silently dropped.
bool hasFlowContent(const char* html, size_t len);
// dir="rtl" / "ltr" on an element (HTML or a TelaOS container): see docs/HTML_TAGS.md
void applyDir(lv_obj_t* obj, const char* dir);
// Make a TelaOS container stack its children like an HTML block (div without flex=)
void makeFlow(lv_obj_t* container);
void renderContent(const char* html, size_t len, lv_obj_t* container);

}  // namespace HtmlFlow
