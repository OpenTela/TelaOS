/**
 * LVGL Stub for syntax checking AND UI testing
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <functional>

#ifdef LVGL_MOCK_ENABLED
#include "lvgl_mock.h"
#endif

// ============ Core Types ============

// Forward declarations and basic types
typedef struct _lv_area_t { int x1, y1, x2, y2; } lv_area_t;
typedef struct _lv_point_t { int x, y; } lv_point_t;
typedef struct _lv_color_t { uint32_t full; } lv_color_t;

typedef struct _lv_obj_t {
    void* user_data;
    struct _lv_obj_t* parent;
    lv_area_t coords;  // x1,y1,x2,y2 for position/size
    #ifdef LVGL_MOCK_ENABLED
    LvglMock::Widget* mock_widget;
    #endif
} lv_obj_t;

typedef struct _lv_display_t {} lv_display_t;
typedef struct _lv_indev_t {} lv_indev_t;
typedef struct _lv_group_t {} lv_group_t;
typedef struct _lv_event_t {
    void* target;
    void* user_data;
    int code;
} lv_event_t;
typedef struct _lv_timer_t {} lv_timer_t;
typedef struct _lv_anim_t {} lv_anim_t;
typedef void (*lv_anim_exec_xcb_t)(void*, int32_t);
typedef void (*lv_anim_completed_cb_t)(lv_anim_t*);
inline void lv_anim_init(lv_anim_t*) {}
inline void lv_anim_set_var(lv_anim_t*, void*) {}
inline void lv_anim_set_values(lv_anim_t*, int32_t, int32_t) {}
inline void lv_anim_set_time(lv_anim_t*, uint32_t) {}
inline void lv_anim_set_exec_cb(lv_anim_t*, lv_anim_exec_xcb_t) {}
inline void lv_anim_set_completed_cb(lv_anim_t*, lv_anim_completed_cb_t) {}
inline lv_anim_t* lv_anim_start(lv_anim_t*) { return nullptr; }
typedef void (*lv_anim_path_cb_t)(lv_anim_t*);
inline void lv_anim_set_path_cb(lv_anim_t*, lv_anim_path_cb_t) {}
inline void lv_anim_path_ease_out(lv_anim_t*) {}
inline lv_obj_t* lv_display_get_default() { return nullptr; }
inline uint32_t lv_display_get_inactive_time(lv_obj_t*) { return 0; }
typedef struct _lv_font_t { int line_height; } lv_font_t;
typedef struct _lv_style_t {} lv_style_t;
typedef struct {} lv_obj_class_t;
typedef struct { struct { uint32_t w, h, cf; } header; uint32_t data_size; const uint8_t* data; } lv_image_dsc_t;

// Input device data
typedef struct lv_indev_data_t {
    lv_point_t point;  // x, y
    int state;
} lv_indev_data_t;

typedef void (*lv_event_cb_t)(lv_event_t*);
typedef void (*lv_display_flush_cb_t)(lv_display_t*, const lv_area_t*, uint8_t*);
typedef void (*lv_indev_read_cb_t)(lv_indev_t*, lv_indev_data_t*);
typedef int lv_coord_t;
typedef uint8_t lv_opa_t;

typedef enum {
    LV_DIR_NONE = 0,
    LV_DIR_LEFT = 1,
    LV_DIR_RIGHT = 2,
    LV_DIR_TOP = 4,
    LV_DIR_BOTTOM = 8,
    LV_DIR_HOR = 3,
    LV_DIR_VER = 12,
    LV_DIR_ALL = 15
} lv_dir_t;

// Class stubs
extern const lv_obj_class_t lv_label_class;
extern const lv_obj_class_t lv_textarea_class;
extern const lv_obj_class_t lv_btn_class;
extern const lv_obj_class_t lv_switch_class;
extern const lv_obj_class_t lv_slider_class;
extern const lv_font_t lv_font_montserrat_14;
extern const lv_font_t lv_font_montserrat_16;

// ============ Constants ============

#define LV_OBJ_FLAG_HIDDEN         (1 << 0)
#define LV_OBJ_FLAG_CLICKABLE      (1 << 1)
#define LV_OBJ_FLAG_CLICK_FOCUSABLE (1 << 2)
#define LV_OBJ_FLAG_SCROLLABLE     (1 << 4)
#define LV_OBJ_FLAG_SCROLL_ONE     (1 << 5)
#define LV_OBJ_FLAG_SNAPPABLE      (1 << 12)
#define LV_OBJ_FLAG_PRESS_LOCK     (1 << 13)
#define LV_OBJ_FLAG_GESTURE_BUBBLE (1 << 18)
#define LV_OBJ_FLAG_EVENT_BUBBLE   (1 << 19)

#define LV_STATE_DEFAULT  0
#define LV_STATE_PRESSED  1
#define LV_STATE_CHECKED  2
#define LV_STATE_FOCUSED  4

#define LV_PART_MAIN      0
#define LV_PART_INDICATOR 1
#define LV_PART_KNOB      2
#define LV_PART_ITEMS     3

#define LV_SIZE_CONTENT   (-1)
#define LV_OPA_50         128
#define LV_OPA_70         178

#define LV_INDEV_TYPE_POINTER 1
#define LV_DISPLAY_RENDER_MODE_PARTIAL 1
#define LV_LABEL_LONG_DOT ((lv_label_long_mode_t)3)

inline int lv_pct(int x) { return 2000 + x; }
#define LV_PCT(x) lv_pct(x)

typedef enum {
    LV_ALIGN_DEFAULT = 0,
    LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_MID, LV_ALIGN_TOP_RIGHT,
    LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_MID, LV_ALIGN_BOTTOM_RIGHT,
    LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID, LV_ALIGN_CENTER,
    LV_ALIGN_OUT_TOP_LEFT, LV_ALIGN_OUT_TOP_MID, LV_ALIGN_OUT_TOP_RIGHT,
    LV_ALIGN_OUT_BOTTOM_LEFT, LV_ALIGN_OUT_BOTTOM_MID, LV_ALIGN_OUT_BOTTOM_RIGHT,
    LV_ALIGN_OUT_LEFT_TOP, LV_ALIGN_OUT_LEFT_MID, LV_ALIGN_OUT_LEFT_BOTTOM,
    LV_ALIGN_OUT_RIGHT_TOP, LV_ALIGN_OUT_RIGHT_MID, LV_ALIGN_OUT_RIGHT_BOTTOM
} lv_align_t;

typedef enum {
    LV_TEXT_ALIGN_AUTO = 0,
    LV_TEXT_ALIGN_LEFT,
    LV_TEXT_ALIGN_CENTER,
    LV_TEXT_ALIGN_RIGHT
} lv_text_align_t;

typedef enum {
    LV_EVENT_PRESSED = 1,
    LV_EVENT_CLICKED,
    LV_EVENT_LONG_PRESSED,
    LV_EVENT_VALUE_CHANGED,
    LV_EVENT_RELEASED,
    LV_EVENT_SCROLL,
    LV_EVENT_GESTURE,
    LV_EVENT_KEY,
    LV_EVENT_FOCUSED,
    LV_EVENT_DEFOCUSED,
    LV_EVENT_READY,
    LV_EVENT_PRESSING,
    LV_EVENT_CANCEL,
    LV_EVENT_ALL = 0xFF
} lv_event_code_t;

typedef enum {
    LV_LABEL_LONG_WRAP = 0,
    LV_LABEL_LONG_SCROLL,
    LV_LABEL_LONG_SCROLL_CIRCULAR,
    LV_LABEL_LONG_CLIP
} lv_label_long_mode_t;

typedef enum {
    LV_KEYBOARD_MODE_TEXT_LOWER,
    LV_KEYBOARD_MODE_TEXT_UPPER,
    LV_KEYBOARD_MODE_NUMBER
} lv_keyboard_mode_t;

typedef enum {
    LV_FLEX_FLOW_ROW = 0,
    LV_FLEX_FLOW_COLUMN,
    LV_FLEX_FLOW_ROW_WRAP,
    LV_FLEX_FLOW_COLUMN_WRAP
} lv_flex_flow_t;

typedef enum {
    LV_FLEX_ALIGN_START = 0,
    LV_FLEX_ALIGN_END,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_SPACE_EVENLY,
    LV_FLEX_ALIGN_SPACE_AROUND,
    LV_FLEX_ALIGN_SPACE_BETWEEN
} lv_flex_align_t;

typedef enum {
    LV_GRID_ALIGN_START = 0,
    LV_GRID_ALIGN_CENTER,
    LV_GRID_ALIGN_END,
    LV_GRID_ALIGN_STRETCH,
    LV_GRID_ALIGN_SPACE_EVENLY,
    LV_GRID_ALIGN_SPACE_AROUND,
    LV_GRID_ALIGN_SPACE_BETWEEN
} lv_grid_align_t;

#define LV_COLOR_FORMAT_ARGB8888 1
#define LV_COLOR_FORMAT_RGB565 2

inline lv_color_t lv_color_hex(uint32_t c) { return lv_color_t{c}; }
inline lv_color_t lv_color_white() { return lv_color_t{0xFFFFFF}; }
inline lv_color_t lv_color_black() { return lv_color_t{0x000000}; }

// ============ Object Functions ============

#ifdef LVGL_MOCK_ENABLED
inline lv_obj_t* alloc_obj(const char* type, lv_obj_t* parent) {
    auto* obj = new lv_obj_t();
    obj->parent = parent;
    obj->mock_widget = LvglMock::create(type, parent ? parent->mock_widget : nullptr);
    return obj;
}
#else
inline lv_obj_t* alloc_obj(const char*, lv_obj_t*) { return new lv_obj_t(); }
#endif

inline lv_obj_t* lv_obj_create(lv_obj_t* parent) { return alloc_obj("Container", parent); }
inline lv_obj_t* lv_btn_create(lv_obj_t* parent) { return alloc_obj("Button", parent); }
inline lv_obj_t* lv_label_create(lv_obj_t* parent) { return alloc_obj("Label", parent); }
inline lv_obj_t* lv_slider_create(lv_obj_t* parent) { return alloc_obj("Slider", parent); }
inline lv_obj_t* lv_switch_create(lv_obj_t* parent) { return alloc_obj("Switch", parent); }
inline lv_obj_t* lv_textarea_create(lv_obj_t* parent) { return alloc_obj("TextArea", parent); }
inline lv_obj_t* lv_canvas_create(lv_obj_t* parent) { return alloc_obj("Canvas", parent); }
inline lv_obj_t* lv_img_create(lv_obj_t* parent) { return alloc_obj("Image", parent); }
inline lv_obj_t* lv_keyboard_create(lv_obj_t* parent) { return alloc_obj("Keyboard", parent); }
inline lv_obj_t* lv_bar_create(lv_obj_t* parent) { return alloc_obj("Bar", parent); }
inline lv_obj_t* lv_arc_create(lv_obj_t* parent) { return alloc_obj("Arc", parent); }
inline lv_obj_t* lv_checkbox_create(lv_obj_t* parent) { return alloc_obj("Checkbox", parent); }
inline lv_obj_t* lv_dropdown_create(lv_obj_t* parent) { return alloc_obj("Dropdown", parent); }
inline lv_obj_t* lv_roller_create(lv_obj_t* parent) { return alloc_obj("Roller", parent); }
inline lv_obj_t* lv_table_create(lv_obj_t* parent) { return alloc_obj("Table", parent); }
inline lv_obj_t* lv_chart_create(lv_obj_t* parent) { return alloc_obj("Chart", parent); }
inline lv_obj_t* lv_tabview_create(lv_obj_t* parent) { return alloc_obj("TabView", parent); }
inline lv_obj_t* lv_tileview_create(lv_obj_t* parent) { return alloc_obj("TileView", parent); }
inline lv_obj_t* lv_tileview_add_tile(lv_obj_t* parent, int, int, int) { return alloc_obj("Tile", parent); }

#ifdef LVGL_MOCK_ENABLED
inline lv_obj_t* lv_screen_active() { 
    static lv_obj_t screen_obj;
    if (LvglMock::g_screen) {
        screen_obj.mock_widget = LvglMock::g_screen;
    }
    return &screen_obj; 
}
#else
inline lv_obj_t* lv_screen_active() { return nullptr; }
#endif
inline void lv_scr_load(lv_obj_t*) {}
inline void lv_scr_load_anim(lv_obj_t*, int, int, int, bool) {}

inline void lv_obj_del(lv_obj_t* obj) { delete obj; }
inline void lv_obj_delete(lv_obj_t* obj) { delete obj; }
inline lv_obj_t* lv_layer_top() { return nullptr; }
inline void lv_obj_clean(lv_obj_t*) {}
inline void lv_obj_invalidate(lv_obj_t*) {}
inline bool lv_obj_has_flag(lv_obj_t*, int) { return false; }
inline bool lv_obj_has_state(lv_obj_t*, int) { return false; }

#ifdef LVGL_MOCK_ENABLED
// Flag capture (for visibility)
inline void lv_obj_add_flag(lv_obj_t* obj, int flag) {
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_HIDDEN) {
        obj->mock_widget->hidden = true;
    }
}
inline void lv_obj_clear_flag(lv_obj_t* obj, int flag) {
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_HIDDEN) {
        obj->mock_widget->hidden = false;
    }
}
inline void lv_obj_remove_flag(lv_obj_t* obj, int flag) {
    lv_obj_clear_flag(obj, flag);
}

inline void lv_obj_set_pos(lv_obj_t* obj, int x, int y) {
    if (obj && obj->mock_widget) { obj->mock_widget->x = x; obj->mock_widget->y = y; }
}
inline void lv_obj_set_size(lv_obj_t* obj, int w, int h) {
    if (obj && obj->mock_widget) { obj->mock_widget->w = w; obj->mock_widget->h = h; }
}
inline void lv_obj_set_width(lv_obj_t* obj, int w) { if (obj && obj->mock_widget) obj->mock_widget->w = w; }
inline void lv_obj_set_height(lv_obj_t* obj, int h) { if (obj && obj->mock_widget) obj->mock_widget->h = h; }
inline void lv_obj_set_x(lv_obj_t* obj, int x) { if (obj && obj->mock_widget) { obj->mock_widget->x = x; } }
inline void lv_obj_set_y(lv_obj_t* obj, int y) { if (obj && obj->mock_widget) obj->mock_widget->y = y; }
inline void lv_label_set_text(lv_obj_t* obj, const char* txt) {
    if (obj && obj->mock_widget) obj->mock_widget->text = txt ? txt : "";
}
inline void _unique_id(lv_obj_t* obj, const char* id) { if (obj && obj->mock_widget && id) obj->mock_widget->id = id; }

// Event setters for mock
inline void _set_onclick(lv_obj_t* obj, const char* fn) { if (obj && obj->mock_widget && fn) obj->mock_widget->onclick = fn; }
inline void _set_onhold(lv_obj_t* obj, const char* fn) { if (obj && obj->mock_widget && fn) obj->mock_widget->onhold = fn; }
inline void _set_onenter(lv_obj_t* obj, const char* fn) { if (obj && obj->mock_widget && fn) obj->mock_widget->onenter = fn; }
inline void _set_onblur(lv_obj_t* obj, const char* fn) { if (obj && obj->mock_widget && fn) obj->mock_widget->onblur = fn; }
inline void _set_href(lv_obj_t* obj, const char* href) { if (obj && obj->mock_widget && href) obj->mock_widget->href = href; }
// Style capture
inline void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t c, int) { if (obj && obj->mock_widget) { obj->mock_widget->bgcolor = c.full; obj->mock_widget->hasBgcolor = true; } }
inline void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t c, int) { if (obj && obj->mock_widget) { obj->mock_widget->color = c.full; obj->mock_widget->hasColor = true; } }
inline void lv_obj_set_style_radius(lv_obj_t* obj, int r, int) { if (obj && obj->mock_widget) obj->mock_widget->radius = r; }
inline void lv_obj_set_style_bg_opa(lv_obj_t* obj, int opa, int) { if (obj && obj->mock_widget) obj->mock_widget->opacity = opa; }

// Alignment capture
inline void lv_obj_align(lv_obj_t* obj, lv_align_t align, int x_ofs, int y_ofs) {
    if (obj && obj->mock_widget) {
        obj->mock_widget->alignType = align;
        obj->mock_widget->alignOfsX = x_ofs;
        obj->mock_widget->alignOfsY = y_ofs;
    }
}
inline void lv_obj_set_align(lv_obj_t* obj, lv_align_t align) {
    if (obj && obj->mock_widget) obj->mock_widget->alignType = align;
}
inline void lv_obj_set_style_text_align(lv_obj_t* obj, lv_text_align_t align, int) {
    if (obj && obj->mock_widget) {
        // LV_TEXT_ALIGN_LEFT=1, CENTER=2, RIGHT=3
        obj->mock_widget->textAlignH = (align == LV_TEXT_ALIGN_CENTER) ? 1 : (align == LV_TEXT_ALIGN_RIGHT) ? 2 : 0;
    }
}

// Padding capture (used for vertical text alignment)
inline void lv_obj_set_style_pad_top(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padTop = pad;
}
inline void lv_obj_set_style_pad_all(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padAll = pad;
}
#define lv_obj_set_style_pad_all lv_obj_set_style_pad_all

// Slider/Bar capture
inline void lv_slider_set_range(lv_obj_t* obj, int min, int max) {
    if (obj && obj->mock_widget) { obj->mock_widget->minValue = min; obj->mock_widget->maxValue = max; }
}
inline void lv_slider_set_value(lv_obj_t* obj, int val, int) {
    if (obj && obj->mock_widget) obj->mock_widget->curValue = val;
}
inline void lv_bar_set_range(lv_obj_t* obj, int min, int max) {
    if (obj && obj->mock_widget) { obj->mock_widget->minValue = min; obj->mock_widget->maxValue = max; }
}
inline void lv_bar_set_value(lv_obj_t* obj, int val, int) {
    if (obj && obj->mock_widget) obj->mock_widget->curValue = val;
}

// Switch capture  
inline void lv_obj_add_state(lv_obj_t* obj, int state) {
    if (obj && obj->mock_widget && state == LV_STATE_CHECKED) obj->mock_widget->checked = true;
}
inline void lv_obj_clear_state(lv_obj_t* obj, int state) {
    if (obj && obj->mock_widget && state == LV_STATE_CHECKED) obj->mock_widget->checked = false;
}

// Input capture
inline void lv_textarea_set_placeholder_text(lv_obj_t* obj, const char* txt) {
    if (obj && obj->mock_widget) obj->mock_widget->placeholder = txt ? txt : "";
}
inline void lv_textarea_set_password_mode(lv_obj_t* obj, bool en) {
    if (obj && obj->mock_widget) obj->mock_widget->password = en;
}
inline void lv_textarea_set_text(lv_obj_t* obj, const char* txt) {
    if (obj && obj->mock_widget) obj->mock_widget->text = txt ? txt : "";
}

#define lv_obj_set_style_bg_color lv_obj_set_style_bg_color
#define lv_obj_set_style_text_color lv_obj_set_style_text_color
#define lv_obj_set_style_radius lv_obj_set_style_radius
#define lv_obj_set_style_bg_opa lv_obj_set_style_bg_opa
#define lv_obj_set_style_text_align lv_obj_set_style_text_align
#define lv_obj_align lv_obj_align
#define lv_obj_set_align lv_obj_set_align
#define lv_slider_set_range lv_slider_set_range
#define lv_slider_set_value lv_slider_set_value
#define lv_bar_set_range lv_bar_set_range
#define lv_bar_set_value lv_bar_set_value
#define lv_obj_add_state lv_obj_add_state
#define lv_obj_clear_state lv_obj_clear_state
#define lv_textarea_set_placeholder_text lv_textarea_set_placeholder_text
#define lv_textarea_set_password_mode lv_textarea_set_password_mode
#define lv_textarea_set_text lv_textarea_set_text
#define lv_obj_set_style_pad_top lv_obj_set_style_pad_top
#define lv_obj_add_flag lv_obj_add_flag
#define lv_obj_clear_flag lv_obj_clear_flag
#define _unique_id _unique_id
#define _set_onclick _set_onclick
#define _set_onhold _set_onhold
#define _set_onenter _set_onenter
#define _set_onblur _set_onblur
#define _set_href _set_href
#else
// Non-mock stubs
inline void lv_obj_add_flag(lv_obj_t*, int) {}
inline void lv_obj_clear_flag(lv_obj_t*, int) {}
inline void lv_label_set_text(lv_obj_t*, const char*) {}
inline void lv_obj_set_pos(lv_obj_t*, int, int) {}
inline void lv_obj_set_size(lv_obj_t*, int, int) {}
inline void lv_obj_set_width(lv_obj_t*, int) {}
inline void lv_obj_set_height(lv_obj_t*, int) {}
inline void lv_obj_set_x(lv_obj_t*, int) {}
inline void lv_obj_set_y(lv_obj_t*, int) {}
// _unique_id defined in widget_common.h
#endif

inline int lv_obj_get_x(lv_obj_t*) { return 0; }
inline int lv_obj_get_y(lv_obj_t*) { return 0; }
inline int lv_obj_get_width(lv_obj_t*) { return 100; }
inline int lv_obj_get_height(lv_obj_t*) { return 100; }
inline void lv_obj_get_coords(lv_obj_t* obj, lv_area_t* area) {
    if (area) { area->x1 = 0; area->y1 = 0; area->x2 = 99; area->y2 = 99; }
}
inline lv_obj_t* lv_obj_get_parent(lv_obj_t* obj) { return obj ? obj->parent : nullptr; }
inline lv_obj_t* lv_obj_get_child(lv_obj_t* obj, int idx) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && idx >= 0 && idx < (int)obj->mock_widget->children.size())
        return nullptr; // mock tracks via Widget*, not lv_obj_t*
#endif
    return nullptr;
}
inline int lv_obj_get_child_count(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) return (int)obj->mock_widget->children.size();
#endif
    return 0;
}
inline int lv_obj_get_index(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->parent) {
        auto& siblings = obj->mock_widget->parent->children;
        for (int i = 0; i < (int)siblings.size(); i++) {
            if (siblings[i] == obj->mock_widget) return i;
        }
    }
#endif
    return 0;
}
inline int lv_obj_get_child_cnt(lv_obj_t*) { return 0; }
inline int lv_obj_get_event_count(lv_obj_t*) { return 0; }

#ifndef lv_obj_set_align
inline void lv_obj_set_align(lv_obj_t*, lv_align_t) {}
#endif
#ifndef lv_obj_align
inline void lv_obj_align(lv_obj_t*, lv_align_t, int, int) {}
#endif
inline void lv_obj_set_scroll_dir(lv_obj_t*, lv_dir_t) {}
inline void lv_obj_set_scrollbar_mode(lv_obj_t*, int) {}
inline void lv_obj_scroll_to_view(lv_obj_t*, int) {}
inline void lv_obj_set_tile_id(lv_obj_t*, int, int, int) {}

inline void lv_obj_set_flex_flow(lv_obj_t*, lv_flex_flow_t) {}
inline void lv_obj_set_flex_align(lv_obj_t*, lv_flex_align_t, lv_flex_align_t, lv_flex_align_t) {}
inline void lv_obj_set_flex_grow(lv_obj_t*, int) {}

inline void lv_obj_set_grid_dsc_array(lv_obj_t*, const int*, const int*) {}
inline void lv_obj_set_grid_cell(lv_obj_t*, lv_grid_align_t, int, int, lv_grid_align_t, int, int) {}

inline void* lv_event_get_user_data(lv_event_t* e) { return e ? e->user_data : nullptr; }
inline lv_obj_t* lv_event_get_target(lv_event_t*) { return nullptr; }
inline lv_event_code_t lv_event_get_code(lv_event_t*) { return LV_EVENT_CLICKED; }
inline void lv_obj_add_event_cb(lv_obj_t*, lv_event_cb_t, int, void*) {}
inline void lv_obj_remove_event(lv_obj_t*, int) {}

inline bool lv_obj_check_type(lv_obj_t*, const lv_obj_class_t*) { return false; }
inline lv_group_t* lv_group_get_default() { return nullptr; }
inline lv_obj_t* lv_group_get_focused(lv_group_t*) { return nullptr; }
inline void lv_obj_send_event(lv_obj_t*, uint32_t, void*) {}
inline void lv_obj_remove_style_all(lv_obj_t*) {}


// Style functions (guarded for mock override)
#ifndef lv_obj_set_style_bg_color
inline void lv_obj_set_style_bg_color(lv_obj_t*, lv_color_t, int) {}
#endif
#ifndef lv_obj_set_style_text_color
inline void lv_obj_set_style_text_color(lv_obj_t*, lv_color_t, int) {}
#endif
#ifndef lv_obj_set_style_radius
inline void lv_obj_set_style_radius(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_bg_opa
inline void lv_obj_set_style_bg_opa(lv_obj_t*, int, int) {}
#endif
inline void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, int) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && font) {
        obj->mock_widget->fontSize = font->line_height;
    }
#endif
}
#ifndef lv_obj_set_style_text_align
inline void lv_obj_set_style_text_align(lv_obj_t*, lv_text_align_t, int) {}
#endif
inline void lv_obj_set_style_border_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_border_color(lv_obj_t*, lv_color_t, int) {}
#ifndef lv_obj_set_style_pad_all
inline void lv_obj_set_style_pad_all(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_top
inline void lv_obj_set_style_pad_top(lv_obj_t*, int, int) {}
#endif
inline void lv_obj_set_style_pad_bottom(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_pad_left(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_pad_right(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_pad_column(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_pad_row(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_shadow_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_shadow_color(lv_obj_t*, lv_color_t, int) {}
inline void lv_obj_set_style_shadow_opa(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_outline_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_outline_color(lv_obj_t*, lv_color_t, int) {}
inline int lv_obj_get_style_pad_left(lv_obj_t*, int) { return 0; }
inline int lv_obj_get_style_pad_right(lv_obj_t*, int) { return 0; }
inline const lv_font_t* lv_obj_get_style_text_font(lv_obj_t*, int) { return nullptr; }

// Label
inline void lv_label_set_text_fmt(lv_obj_t*, const char*, ...) {}
inline void lv_label_set_long_mode(lv_obj_t*, lv_label_long_mode_t) {}
inline const char* lv_label_get_text(lv_obj_t*) { return ""; }

// Slider
#ifndef lv_slider_set_value
inline void lv_slider_set_value(lv_obj_t*, int, int) {}
#endif
#ifndef lv_slider_set_range
inline void lv_slider_set_range(lv_obj_t*, int, int) {}
#endif
inline int lv_slider_get_value(lv_obj_t*) { return 0; }

// Switch
inline bool lv_switch_get_state(lv_obj_t*) { return false; }

// TextArea
#ifndef lv_textarea_set_text
inline void lv_textarea_set_text(lv_obj_t*, const char*) {}
#endif
#ifndef lv_textarea_set_placeholder_text
inline void lv_textarea_set_placeholder_text(lv_obj_t*, const char*) {}
#endif
#ifndef lv_textarea_set_password_mode
inline void lv_textarea_set_password_mode(lv_obj_t*, bool) {}
#endif
inline void lv_textarea_set_one_line(lv_obj_t*, bool) {}
inline const char* lv_textarea_get_text(lv_obj_t*) { return ""; }

// Keyboard
inline void lv_keyboard_set_textarea(lv_obj_t*, lv_obj_t*) {}
inline void lv_keyboard_set_mode(lv_obj_t*, lv_keyboard_mode_t) {}

// Image
inline void lv_img_set_src(lv_obj_t*, const void*) {}
inline void lv_img_set_zoom(lv_obj_t*, int) {}

// Canvas
inline void lv_canvas_set_buffer(lv_obj_t*, void*, int, int, int) {}
inline void lv_canvas_fill_bg(lv_obj_t*, lv_color_t, lv_opa_t) {}
inline void lv_canvas_set_px(lv_obj_t*, int, int, lv_color_t, lv_opa_t) {}

// Tabview
inline void lv_tabview_set_tab_bar_position(lv_obj_t*, int) {}
inline void lv_tabview_set_tab_bar_size(lv_obj_t*, int) {}
inline lv_obj_t* lv_tabview_get_tab_bar(lv_obj_t*) { return nullptr; }
inline lv_obj_t* lv_tabview_add_tab(lv_obj_t*, const char*) { return nullptr; }
inline lv_obj_t* lv_tabview_get_content(lv_obj_t*) { return nullptr; }
inline lv_obj_t* lv_tileview_get_tile_act(lv_obj_t*) { return nullptr; }

// Timer
inline lv_timer_t* lv_timer_create(void(*)(lv_timer_t*), int, void*) { return nullptr; }
inline void lv_timer_del(lv_timer_t*) {}
inline void lv_timer_set_repeat_count(lv_timer_t*, int) {}
inline void* lv_timer_get_user_data(lv_timer_t*) { return nullptr; }
inline uint32_t lv_tick_get() { return 0; }

// Display
inline void lv_init() {}
inline lv_display_t* lv_display_create(int, int) { return nullptr; }
inline void lv_display_set_flush_cb(lv_display_t*, lv_display_flush_cb_t) {}
inline void lv_display_set_buffers(lv_display_t*, void*, void*, int, int) {}
inline void lv_display_set_color_format(lv_display_t*, int) {}
inline void lv_display_flush_ready(lv_display_t*) {}
inline void lv_tick_set_cb(uint32_t(*)()) {}
inline void lv_task_handler() {}
inline void lv_refr_now(lv_display_t*) {}

// Input device
inline lv_indev_t* lv_indev_create() { return nullptr; }
inline void lv_indev_set_type(lv_indev_t*, int) {}
inline void lv_indev_set_read_cb(lv_indev_t*, lv_indev_read_cb_t) {}
inline lv_indev_t* lv_indev_active() { return nullptr; }
inline void lv_indev_get_point(lv_indev_t*, lv_point_t*) {}
inline lv_obj_t* lv_indev_get_obj_act() { return nullptr; }
inline lv_dir_t lv_indev_get_gesture_dir(lv_indev_t*) { return LV_DIR_NONE; }

// Font
inline const lv_font_t* lv_font_default() { static lv_font_t f{16}; return &f; }
inline int lv_font_get_line_height(const lv_font_t* f) { return f ? f->line_height : 16; }

// Font format (for compiled .c font files)
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_FONT_DECLARE(name) extern const lv_font_t name
#define LV_VERSION_CHECK(a,b,c) 0

typedef struct {
    uint32_t bitmap_index;
    uint32_t adv_w;
    int8_t box_w, box_h, ofs_x, ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

typedef struct {
    uint32_t range_start, range_length, glyph_id_start;
    const void* unicode_list;
    const void* glyph_id_ofs_list;
    uint32_t list_length;
    uint8_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const uint8_t* glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t* glyph_dsc;
    const lv_font_fmt_txt_cmap_t* cmaps;
    uint32_t cmap_num, bpp;
    uint8_t kern_scale, kern_dsc_size, kern_classes;
    const void* kern_dsc;
    uint8_t bitmap_format;
} lv_font_fmt_txt_dsc_t;

typedef struct {
    int8_t value;
} lv_font_fmt_txt_kern_pair_t;

#define LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL 0
#define LV_FONT_FMT_TXT_CMAP_SPARSE_TINY 3
#define LV_FONT_SUBPX_NONE 0

inline bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t*, void*, uint32_t, uint32_t) { return false; }
inline const uint8_t* lv_font_get_bitmap_fmt_txt(const void*, uint32_t) { return nullptr; }

// Additional stubs
#define LV_OPA_COVER 255
inline lv_obj_t* lv_image_create(lv_obj_t* p) { return lv_img_create(p); }
inline void lv_image_set_src(lv_obj_t* o, const void* s) { lv_img_set_src(o, s); }

inline void lv_obj_center(lv_obj_t*) {}
#define LV_ANIM_OFF 0
#define LV_ANIM_ON 1
#define LV_OPA_TRANSP 0
inline void lv_image_cache_drop(const void*) {}
#define LV_SCROLLBAR_MODE_OFF 0
inline void lv_timer_delete(lv_timer_t*) {}
typedef struct { uint32_t total_size; uint32_t free_size; uint32_t used_cnt; uint8_t frag_pct; } lv_mem_monitor_t;
inline void lv_mem_monitor(lv_mem_monitor_t*) {}
#define LV_EVENT_CANCEL 99
#define LV_EVENT_ALL 0xFF
inline lv_obj_t* lv_keyboard_get_textarea(lv_obj_t*) { return nullptr; }
inline void lv_obj_set_ext_click_area(lv_obj_t*, int) {}
#define LV_SCROLLBAR_MODE_ACTIVE 1
inline void lv_obj_move_foreground(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->parent) {
        auto& siblings = obj->mock_widget->parent->children;
        auto it = std::find(siblings.begin(), siblings.end(), obj->mock_widget);
        if (it != siblings.end()) {
            auto* w = *it;
            siblings.erase(it);
            siblings.push_back(w);
        }
    }
#endif
}
inline void lv_obj_move_background(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->parent) {
        auto& siblings = obj->mock_widget->parent->children;
        auto it = std::find(siblings.begin(), siblings.end(), obj->mock_widget);
        if (it != siblings.end()) {
            auto* w = *it;
            siblings.erase(it);
            siblings.insert(siblings.begin(), w);
        }
    }
#endif
}
inline void lv_obj_move_to_index(lv_obj_t* obj, int new_index) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->parent) {
        auto& siblings = obj->mock_widget->parent->children;
        auto it = std::find(siblings.begin(), siblings.end(), obj->mock_widget);
        if (it != siblings.end()) {
            auto* w = *it;
            siblings.erase(it);
            if (new_index < 0) new_index = 0;
            if (new_index >= (int)siblings.size()) siblings.push_back(w);
            else siblings.insert(siblings.begin() + new_index, w);
        }
    }
#endif
}
#define LV_RADIUS_CIRCLE 0x7FFF
inline void lv_obj_set_style_transform_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_transform_height(lv_obj_t*, int, int) {}
inline int lv_obj_get_style_pad_bottom(lv_obj_t*, int) { return 0; }
inline int lv_obj_get_style_pad_top(lv_obj_t*, int) { return 0; }

// Touch input states
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0

// lv_timer_handler alias
inline uint32_t lv_timer_handler() { return 0; }
inline void lv_obj_set_style_opa(lv_obj_t* obj, int opa, int) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) obj->mock_widget->opacity = opa;
#endif
}
