/**
 * ui_html_internal.h - Shared internals between ui_html, ui_engine, ui_canvas
 * 
 * NOT for external consumers — only included by ui_*.cpp files.
 */

#ifndef UI_HTML_INTERNAL_H
#define UI_HTML_INTERNAL_H

#include "ui/ui_types.h"
#include "ui/ui_coords.h"
#include "ui/dynamic_app.h"
#include <cstdint>
#include <string>

// Forward declarations
typedef struct _lv_obj_t lv_obj_t;

// ============ Constants ============

constexpr size_t ATTR_VAL_LEN = 64;
constexpr int KEYBOARD_HEIGHT_PCT = 40;
constexpr int FULL_SIZE_PCT = 100;

// ============ Shared constants ============

constexpr size_t SMALL_BUF_LEN = 16;
constexpr uint32_t SLIDER_THROTTLE_MS = 100;

namespace ClickArea {
    constexpr int BUTTON = 18;
    constexpr int SWITCH = 15;
    constexpr int SLIDER = 20;
    constexpr int IMAGE = 25;
}

// ============ Handler callbacks (set by ScriptManager, survive across apps) ============

extern void (*g_onclick_handler)(const char* func_name);
extern void (*g_ontap_handler)(const char* func_name, int x, int y);
extern void (*g_onhold_handler)(const char* func_name);
extern void (*g_onhold_xy_handler)(const char* func_name, int x, int y);
extern void (*g_state_change_handler)(const char* var_name, const char* value);

// ============ Internal functions ============

void ui_set_text_internal(const char* id, const char* text);
void ui_show_page_internal(const char* path);
int  ui_html_render_internal(const char* html);

const char* get_state_value(const char* name);
P::String extractBindVar(const char* bindStr);
void navigate(const char* href);
void ui_update_bindings(const char* varname, const char* value);
P::String render_template(const char* tpl);

/// Descriptor for registering a UI element
struct ElementDesc {
    const char* id          = nullptr;
    lv_obj_t*   obj         = nullptr;
    const char* href        = nullptr;
    const char* onclick     = nullptr;
    const char* onchange    = nullptr;
    const char* oninput     = nullptr;
    const char* bind        = nullptr;
    const char* tpl         = nullptr;
    bool        is_page     = false;
    const char* classTpl    = nullptr;
    const char* visibleBind = nullptr;
    const char* bgcolorBind = nullptr;
    const char* colorBind   = nullptr;
    int         zIndex      = 0;
};


// ============ Shared helpers ============

/// Convert RGB888 (0xRRGGBB) to native display format (BGRA8888)
static inline uint32_t rgb_to_native(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return (b << 24) | (g << 16) | (r << 8) | 0xFF;
}

/// Parse color string (#RGB, #RRGGBB, or decimal) to RGB888
uint32_t parse_color(const char* s);

// ============ Widget builders (defined in ui_widget_builder.cpp) ============

void create_label(const char* astart, const char* aend, const char* content, lv_obj_t* parent);
void create_button(const char* astart, const char* aend, const char* content, lv_obj_t* parent);
void create_switch(const char* astart, const char* aend, lv_obj_t* parent);
void create_slider(const char* astart, const char* aend, lv_obj_t* parent);
void create_input(const char* astart, const char* aend, const char* content, lv_obj_t* parent);
void create_image(const char* astart, const char* aend, lv_obj_t* parent);
void create_canvas(const char* astart, const char* aend, lv_obj_t* parent);
void create_markdown(const char* astart, const char* aend, const char* content, lv_obj_t* parent);
void create_tabs    (const char* astart, const char* aend, const char* content, lv_obj_t* parent);
void create_select  (const char* astart, const char* aend, const char* content, lv_obj_t* parent);

// Layout: table/tr/td (recursive child parsing)
void parse_children(const char *html, int len, lv_obj_t *parent);

// Keyboard close/enter handler (shared between widget_builder and engine)
void keyboard_event_handler(lv_event_t *e);

#endif // UI_HTML_INTERNAL_H
