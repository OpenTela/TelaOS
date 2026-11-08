/**
 * LVGL Stub for syntax checking AND UI testing
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <functional>
#include <algorithm>

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

// A style for a state, or for a part LVGL draws apart (a text field's placeholder, a custom part
// from LV_PART_CUSTOM_FIRST), goes to the mock's stateStyles, not over the element's own
// (lvgl_mock.h). The built-in parts without a state (a bar's indicator) still go into the fields.
inline bool mockState(lv_obj_t* obj, const char* prop, int sel, long long value) {
    if ((sel & 0xFFFF) == 0 && (static_cast<uint32_t>(sel) & 0xFF0000) < 0x080000) return false;
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) obj->mock_widget->stateStyles.push_back({prop, sel, value});
#else
    (void)obj; (void)prop; (void)value;
#endif
    return true;
}

typedef struct _lv_display_t {} lv_display_t;
typedef struct _lv_indev_t {} lv_indev_t;
typedef struct _lv_group_t {} lv_group_t;
typedef struct _lv_event_t {
    void* target;
    void* user_data;
    int code;
    void* current_target = nullptr;
    void* param = nullptr;
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
typedef int32_t (*lv_anim_path_cb_t)(const lv_anim_t*);
inline void lv_anim_set_path_cb(lv_anim_t*, lv_anim_path_cb_t) {}
inline int32_t lv_anim_path_linear(const lv_anim_t*) { return 0; }
inline int32_t lv_anim_path_ease_in(const lv_anim_t*) { return 0; }
inline int32_t lv_anim_path_ease_out(const lv_anim_t*) { return 0; }
inline int32_t lv_anim_path_ease_in_out(const lv_anim_t*) { return 0; }
inline int32_t lv_anim_path_step(const lv_anim_t*) { return 0; }
inline lv_obj_t* lv_display_get_default() { return nullptr; }
inline uint32_t lv_display_get_inactive_time(lv_obj_t*) { return 0; }
typedef struct _lv_font_t { int line_height; int base_line; } lv_font_t;  // base_line: from the line's bottom
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
extern const lv_obj_class_t lv_obj_class;  // a plain object (lv_obj_create)
extern const lv_obj_class_t lv_label_class;
extern const lv_obj_class_t lv_textarea_class;
extern const lv_obj_class_t lv_btn_class;
extern const lv_obj_class_t lv_switch_class;
extern const lv_obj_class_t lv_slider_class;
extern const lv_obj_class_t lv_dropdown_class;
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
#define LV_OBJ_FLAG_OVERFLOW_VISIBLE (1 << 20)
#define LV_OBJ_FLAG_IGNORE_LAYOUT    (1 << 17)
#define LV_OBJ_FLAG_FLOATING         (1 << 21)
typedef uint32_t lv_obj_flag_t;

typedef uint8_t lv_border_side_t;
#define LV_BORDER_SIDE_NONE     0x00
#define LV_BORDER_SIDE_BOTTOM   0x01
#define LV_BORDER_SIDE_TOP      0x02
#define LV_BORDER_SIDE_LEFT     0x04
#define LV_BORDER_SIDE_RIGHT    0x08
#define LV_BORDER_SIDE_FULL     0x0F

typedef enum { LV_GRAD_DIR_NONE, LV_GRAD_DIR_VER, LV_GRAD_DIR_HOR } lv_grad_dir_t;

// As in LVGL 9: states in the low 16 bits, parts above them, so a selector tells them apart
#define LV_STATE_DEFAULT  0x0000
#define LV_STATE_CHECKED  0x0001
#define LV_STATE_FOCUSED  0x0002
#define LV_STATE_HOVERED  0x0010
#define LV_STATE_PRESSED  0x0020
#define LV_STATE_DISABLED 0x0080

typedef uint32_t lv_style_selector_t;  // a part | a state
#define LV_PART_MAIN      0x000000
#define LV_PART_INDICATOR 0x020000
#define LV_PART_KNOB      0x030000
#define LV_PART_ITEMS     0x050000
#define LV_PART_CUSTOM_FIRST 0x080000
#define LV_PART_TEXTAREA_PLACEHOLDER LV_PART_CUSTOM_FIRST

#define LV_SIZE_CONTENT   (-1)
#define LV_DPI_DEF        130  // as in include/lv_conf.h
#define LV_COLOR_DEPTH    16
#define LV_OPA_50         128
#define LV_OPA_70         178

#define LV_INDEV_TYPE_POINTER 1
#define LV_DISPLAY_RENDER_MODE_PARTIAL 1
#define LV_LABEL_LONG_DOT ((lv_label_long_mode_t)4)

typedef uint8_t lv_text_decor_t;
#define LV_TEXT_DECOR_NONE          0x00
#define LV_TEXT_DECOR_UNDERLINE     0x01
#define LV_TEXT_DECOR_STRIKETHROUGH 0x02

inline int lv_pct(int x) { return 10000 + x; }
inline int lv_pct_to_px(int v, int base) {
    if (v >= 10000 && v <= 10100) return (v - 10000) * base / 100;
    return v;
}
#define LV_PCT(x) lv_pct(x)
#ifndef LV_COORD_IS_PCT
#define LV_COORD_IS_PCT(v) ((v) >= 10000 && (v) <= 10100)
#define LV_COORD_GET_PCT(v) ((v) - 10000)
#endif

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

// Direction: base_dir is inherited, as in LVGL
typedef enum { LV_BASE_DIR_LTR = 0x00, LV_BASE_DIR_RTL = 0x01, LV_BASE_DIR_AUTO = 0x02 } lv_base_dir_t;
#ifdef LVGL_MOCK_ENABLED
inline void lv_obj_set_style_base_dir(lv_obj_t* obj, lv_base_dir_t dir, int) {
    if (obj && obj->mock_widget) obj->mock_widget->baseDir = static_cast<int>(dir);
}
inline lv_base_dir_t lv_obj_get_style_base_dir(lv_obj_t* obj, int) {
    for (const LvglMock::Widget* w = obj ? obj->mock_widget : nullptr; w; w = w->parent) {
        if (w->baseDir >= 0) return static_cast<lv_base_dir_t>(w->baseDir);
    }
    return LV_BASE_DIR_LTR;
}
inline void lv_obj_set_style_translate_y(lv_obj_t* obj, int y, int sel) {
    if (mockState(obj, "translate_y", sel, y)) return;
    if (obj && obj->mock_widget) obj->mock_widget->translateY = y;
}
#else
inline void lv_obj_set_style_base_dir(lv_obj_t*, lv_base_dir_t, int) {}
inline lv_base_dir_t lv_obj_get_style_base_dir(lv_obj_t*, int) { return LV_BASE_DIR_LTR; }
inline void lv_obj_set_style_translate_y(lv_obj_t*, int, int) {}
#endif

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
    LV_EVENT_DELETE,
    LV_EVENT_REFR_EXT_DRAW_SIZE,
    LV_EVENT_LAYOUT_CHANGED,
    LV_EVENT_STYLE_CHANGED,
    LV_EVENT_SCROLL_END,
    LV_EVENT_ALL = 0xFF,
    LV_EVENT_PREPROCESS = 0x8000,  // a flag: the callback runs before the others
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
    LV_FLEX_FLOW_COLUMN_WRAP,
    LV_FLEX_FLOW_ROW_REVERSE,
    LV_FLEX_FLOW_COLUMN_REVERSE,
    LV_FLEX_FLOW_ROW_WRAP_REVERSE,
    LV_FLEX_FLOW_COLUMN_WRAP_REVERSE
} lv_flex_flow_t;

#define LV_LAYOUT_NONE 0
#define LV_LAYOUT_FLEX 1

typedef enum {
    LV_FLEX_ALIGN_START = 0,
    LV_FLEX_ALIGN_END,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_SPACE_EVENLY,
    LV_FLEX_ALIGN_SPACE_AROUND,
    LV_FLEX_ALIGN_SPACE_BETWEEN,
    LV_FLEX_ALIGN_STRETCH
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
    obj->mock_widget->lv_obj = obj;  // back-pointer
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
inline lv_obj_t* lv_checkbox_create(lv_obj_t* parent) {
    lv_obj_t* o = alloc_obj("Checkbox", parent);
    if (o->mock_widget) o->mock_widget->padColumn = 8;  // the theme's gap between the box and the text
    return o;
}
inline lv_obj_t* lv_dropdown_create(lv_obj_t* parent) { return alloc_obj("Dropdown", parent); }

// ===== List (LVGL 9.x) =====
lv_obj_t* lv_obj_get_child(lv_obj_t* obj, int idx);  // defined below

inline lv_obj_t* lv_list_create(lv_obj_t* parent) { return alloc_obj("List", parent); }
inline lv_obj_t* lv_list_add_text(lv_obj_t* list, const char* txt) {
    auto* o = alloc_obj("Label", list);
    if (o && o->mock_widget && txt) o->mock_widget->text = txt;
    return o;
}
inline lv_obj_t* lv_list_add_button(lv_obj_t* list, const void*, const char* txt) {
    auto* btn = alloc_obj("Button", list);
    auto* lbl = alloc_obj("Label", btn);
    if (lbl && lbl->mock_widget && txt) lbl->mock_widget->text = txt;
    return btn;
}
inline const char* lv_list_get_button_text(lv_obj_t*, lv_obj_t* btn) {
    if (!btn) return "";
    if (auto* lbl = lv_obj_get_child(btn, 0)) {
        if (lbl->mock_widget) return lbl->mock_widget->text.c_str();
    }
    return "";
}
inline void lv_list_set_button_text(lv_obj_t*, lv_obj_t* btn, const char* txt) {
    if (!btn || !txt) return;
    if (auto* lbl = lv_obj_get_child(btn, 0)) {
        if (lbl->mock_widget) lbl->mock_widget->text = txt;
    }
}
inline lv_obj_t* lv_roller_create(lv_obj_t* parent) { return alloc_obj("Roller", parent); }
inline lv_obj_t* lv_table_create(lv_obj_t* parent) { return alloc_obj("Table", parent); }
inline lv_obj_t* lv_chart_create(lv_obj_t* parent) { return alloc_obj("Chart", parent); }
inline lv_obj_t* lv_tabview_create(lv_obj_t* parent) { return alloc_obj("TabView", parent); }
inline lv_obj_t* lv_tileview_create(lv_obj_t* parent) { return alloc_obj("TileView", parent); }
inline lv_obj_t* lv_tileview_add_tile(lv_obj_t* parent, int, int, int) { return alloc_obj("Tile", parent); }

// MessageBox (LVGL v9 API). Minimal mock: each part is a child widget so the
// mock tree mirrors real structure; return values are usable as lv_obj_t*.
inline lv_obj_t* lv_msgbox_create(lv_obj_t* parent) { return alloc_obj("MsgBox", parent); }
inline lv_obj_t* lv_msgbox_add_title(lv_obj_t* mbox, const char*) { return alloc_obj("MsgBoxTitle", mbox); }
inline lv_obj_t* lv_msgbox_add_text(lv_obj_t* mbox, const char*) { return alloc_obj("MsgBoxText", mbox); }
inline lv_obj_t* lv_msgbox_add_close_button(lv_obj_t* mbox) { return alloc_obj("MsgBoxClose", mbox); }
inline lv_obj_t* lv_msgbox_add_footer_button(lv_obj_t* mbox, const char*) { return alloc_obj("MsgBoxFooterBtn", mbox); }
inline void lv_msgbox_close(lv_obj_t* mbox) { delete mbox; }

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

// A deleted object's widget leaves its parent, as the object leaves the screen
inline void mockDetach(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget || !obj->mock_widget->parent) return;
    auto& ch = obj->mock_widget->parent->children;
    ch.erase(std::remove(ch.begin(), ch.end(), obj->mock_widget), ch.end());
#else
    (void)obj;
#endif
}
// The object moves to the end of another parent's children, as in LVGL
inline void lv_obj_set_parent(lv_obj_t* obj, lv_obj_t* parent) {
    if (!obj || !parent || obj->parent == parent) return;
    mockDetach(obj);
    obj->parent = parent;
#ifdef LVGL_MOCK_ENABLED
    if (obj->mock_widget && parent->mock_widget) {
        obj->mock_widget->parent = parent->mock_widget;
        parent->mock_widget->children.push_back(obj->mock_widget);
    }
#endif
}
inline void lv_obj_del(lv_obj_t* obj) { mockDetach(obj); delete obj; }
inline void lv_obj_delete(lv_obj_t* obj) { mockDetach(obj); delete obj; }
inline void lv_obj_delete_async(lv_obj_t* obj) { mockDetach(obj); delete obj; }
inline lv_obj_t* lv_layer_top() { return nullptr; }
inline void lv_obj_clean(lv_obj_t* obj) {
    if (obj && obj->mock_widget) obj->mock_widget->children.clear();
}
inline void lv_obj_invalidate(lv_obj_t*) {}
// The flags the mock keeps (the rest read as off)
inline bool lv_obj_has_flag(lv_obj_t* obj, int flag) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget) return false;
    const LvglMock::Widget* w = obj->mock_widget;
    switch (flag) {
        case LV_OBJ_FLAG_HIDDEN: return w->hidden;
        case LV_OBJ_FLAG_SCROLLABLE: return w->scrollable;
        case LV_OBJ_FLAG_OVERFLOW_VISIBLE: return w->overflowVisible;
        case LV_OBJ_FLAG_IGNORE_LAYOUT: return w->ignoreLayout;
        case LV_OBJ_FLAG_FLOATING: return w->floating;
        case LV_OBJ_FLAG_CLICKABLE: return w->clickable;
        default: return false;
    }
#else
    (void)obj; (void)flag;
    return false;
#endif
}
// The states the mock keeps (the rest read as off)
inline bool lv_obj_has_state(lv_obj_t* obj, int state) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget) return false;
    if (state == LV_STATE_CHECKED) return obj->mock_widget->checked;
    if (state == LV_STATE_DISABLED) return obj->mock_widget->disabled;
    return false;
#else
    (void)obj; (void)state;
    return false;
#endif
}

#ifdef LVGL_MOCK_ENABLED
// Flag capture (for visibility)
inline void lv_obj_add_flag(lv_obj_t* obj, int flag) {
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_HIDDEN) obj->mock_widget->hidden = true;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_SCROLLABLE) obj->mock_widget->scrollable = true;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_OVERFLOW_VISIBLE) obj->mock_widget->overflowVisible = true;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_IGNORE_LAYOUT) obj->mock_widget->ignoreLayout = true;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_FLOATING) obj->mock_widget->floating = true;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_CLICKABLE) obj->mock_widget->clickable = true;
}
inline void lv_obj_clear_flag(lv_obj_t* obj, int flag) {
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_HIDDEN) obj->mock_widget->hidden = false;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_SCROLLABLE) obj->mock_widget->scrollable = false;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_OVERFLOW_VISIBLE) obj->mock_widget->overflowVisible = false;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_IGNORE_LAYOUT) obj->mock_widget->ignoreLayout = false;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_FLOATING) obj->mock_widget->floating = false;
    if (obj && obj->mock_widget && flag == LV_OBJ_FLAG_CLICKABLE) obj->mock_widget->clickable = false;
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
inline void lv_obj_set_style_min_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_max_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_min_height(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_max_height(lv_obj_t*, int, int) {}
inline void lv_obj_set_x(lv_obj_t* obj, int x) { if (obj && obj->mock_widget) { obj->mock_widget->x = x; } }
inline void lv_obj_set_y(lv_obj_t* obj, int y) { if (obj && obj->mock_widget) obj->mock_widget->y = y; }
inline void lv_label_set_text(lv_obj_t* obj, const char* txt) {
    if (obj && obj->mock_widget) obj->mock_widget->text = txt ? txt : "";
}
inline void lv_checkbox_set_text(lv_obj_t* obj, const char* txt) {
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
inline void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t c, int sel) { if (mockState(obj, "bg_color", sel, c.full)) return; if (obj && obj->mock_widget) { obj->mock_widget->bgcolor = c.full; obj->mock_widget->hasBgcolor = true; } }
inline void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t c, int sel) { if (mockState(obj, "text_color", sel, c.full)) return; if (obj && obj->mock_widget) { obj->mock_widget->color = c.full; obj->mock_widget->hasColor = true; } }
inline void lv_obj_set_style_radius(lv_obj_t* obj, int r, int sel) { if (mockState(obj, "radius", sel, r)) return; if (obj && obj->mock_widget) obj->mock_widget->radius = r; }
inline void lv_obj_set_style_bg_opa(lv_obj_t* obj, int opa, int sel) { if (mockState(obj, "bg_opa", sel, opa)) return; if (obj && obj->mock_widget) { obj->mock_widget->opacity = opa; obj->mock_widget->bgOpa = opa; } }
inline void lv_obj_set_style_text_opa(lv_obj_t* obj, int opa, int sel) { if (mockState(obj, "text_opa", sel, opa)) return; if (obj && obj->mock_widget) obj->mock_widget->textOpa = opa; }

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
inline void lv_obj_set_style_pad_bottom(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padBottom = pad;
}
inline void lv_obj_set_style_pad_left(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padLeft = pad;
}
inline void lv_obj_set_style_pad_right(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padRight = pad;
}
inline void lv_obj_set_style_pad_all(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padAll = pad;
}
#define lv_obj_set_style_pad_all lv_obj_set_style_pad_all
#define lv_obj_set_style_pad_top lv_obj_set_style_pad_top
#define lv_obj_set_style_pad_bottom lv_obj_set_style_pad_bottom
#define lv_obj_set_style_pad_left lv_obj_set_style_pad_left
#define lv_obj_set_style_pad_right lv_obj_set_style_pad_right

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
    if (obj && obj->mock_widget && state == LV_STATE_DISABLED) obj->mock_widget->disabled = true;
}
inline void lv_obj_clear_state(lv_obj_t* obj, int state) {
    if (obj && obj->mock_widget && state == LV_STATE_CHECKED) obj->mock_widget->checked = false;
    if (obj && obj->mock_widget && state == LV_STATE_DISABLED) obj->mock_widget->disabled = false;
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
inline void lv_textarea_set_max_length(lv_obj_t* obj, uint32_t n) {
    if (obj && obj->mock_widget) obj->mock_widget->maxLength = (int)n;
}

#define lv_obj_set_style_bg_color lv_obj_set_style_bg_color
#define lv_obj_set_style_text_color lv_obj_set_style_text_color
#define lv_obj_set_style_radius lv_obj_set_style_radius
#define lv_obj_set_style_bg_opa lv_obj_set_style_bg_opa
#define lv_obj_set_style_text_opa lv_obj_set_style_text_opa
#define lv_obj_set_style_text_align lv_obj_set_style_text_align
#define lv_obj_align lv_obj_align
#define lv_obj_set_align lv_obj_set_align
#define lv_slider_set_range lv_slider_set_range
#define lv_slider_set_value lv_slider_set_value
#define lv_bar_set_range lv_bar_set_range
#define lv_bar_set_value lv_bar_set_value
#define lv_obj_add_state lv_obj_add_state
#define lv_obj_clear_state lv_obj_clear_state
// LVGL 9 name: remove_state == clear_state
inline void lv_obj_remove_state(lv_obj_t* obj, int state) { lv_obj_clear_state(obj, state); }
#define lv_obj_remove_state lv_obj_remove_state
#define lv_textarea_set_placeholder_text lv_textarea_set_placeholder_text
#define lv_textarea_set_password_mode lv_textarea_set_password_mode
#define lv_textarea_set_text lv_textarea_set_text
#define lv_obj_add_flag lv_obj_add_flag
#define lv_obj_clear_flag lv_obj_clear_flag
#define _unique_id _unique_id
#define _set_onclick _set_onclick
#define _set_onhold _set_onhold
#define _set_onenter _set_onenter
#define _set_onblur _set_onblur
#define _set_href _set_href

// Flex capture (mock)
inline void lv_obj_set_flex_flow(lv_obj_t* obj, lv_flex_flow_t flow) {
    if (obj && obj->mock_widget) obj->mock_widget->flexFlow = (int)flow;
}
inline void lv_obj_set_flex_align(lv_obj_t* obj, lv_flex_align_t main, lv_flex_align_t cross, lv_flex_align_t track) {
    if (obj && obj->mock_widget) {
        obj->mock_widget->flexMainAlign = (int)main;
        obj->mock_widget->flexCrossAlign = (int)cross;
        obj->mock_widget->flexTrackAlign = (int)track;
    }
}
inline void lv_obj_set_flex_grow(lv_obj_t* obj, int grow) {
    if (obj && obj->mock_widget) obj->mock_widget->flexGrow = grow;
}
inline int lv_obj_get_style_flex_grow(lv_obj_t* obj, int) { return (obj && obj->mock_widget) ? obj->mock_widget->flexGrow : 0; }
#define lv_obj_set_flex_flow lv_obj_set_flex_flow
#define lv_obj_set_flex_align lv_obj_set_flex_align
#define lv_obj_set_flex_grow lv_obj_set_flex_grow

// Pad row/column capture (mock)
inline void lv_obj_set_style_pad_row(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padRow = pad;
}
inline void lv_obj_set_style_pad_column(lv_obj_t* obj, int pad, int) {
    if (obj && obj->mock_widget) obj->mock_widget->padColumn = pad;
}
#define lv_obj_set_style_pad_row lv_obj_set_style_pad_row
#define lv_obj_set_style_pad_column lv_obj_set_style_pad_column

#else
// Non-mock stubs
inline void lv_obj_add_flag(lv_obj_t*, int) {}
inline void lv_obj_clear_flag(lv_obj_t*, int) {}
inline void lv_label_set_text(lv_obj_t*, const char*) {}
inline void lv_checkbox_set_text(lv_obj_t*, const char*) {}
inline void lv_obj_set_pos(lv_obj_t*, int, int) {}
inline void lv_obj_set_size(lv_obj_t*, int, int) {}
inline void lv_obj_set_width(lv_obj_t*, int) {}
inline void lv_obj_set_height(lv_obj_t*, int) {}
inline void lv_obj_set_style_min_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_max_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_min_height(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_max_height(lv_obj_t*, int, int) {}
inline void lv_obj_set_x(lv_obj_t*, int) {}
inline void lv_obj_set_y(lv_obj_t*, int) {}
// _unique_id defined in widget_common.h
#endif

inline int lv_obj_get_x(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->x : 0; }
inline int lv_obj_get_y(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->y : 0; }
inline int lv_obj_get_width(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->w : 100; }
inline int lv_obj_get_height(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->h : 100; }
inline int lv_obj_get_content_height(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->h : 100; }
inline int lv_obj_get_content_width(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->w : 100; }
// Resolve lv_pct values to pixels from parent chain
inline void lv_obj_update_layout(lv_obj_t* obj) {
    if (!obj || !obj->mock_widget || !obj->parent) return;
    auto* w = obj->mock_widget;
    auto* p = obj->parent->mock_widget;
    if (!p) return;
    // lv_pct(x) = 10000+x in mock — resolve from parent
    if (w->w >= 10000 && w->w <= 10200) {
        int pct = w->w - 10000;
        int pw = (p->w >= 10000) ? 480 : p->w;  // fallback to screen
        w->w = pw * pct / 100;
    }
    if (w->h >= 10000 && w->h <= 10200) {
        int pct = w->h - 10000;
        int ph = (p->h >= 10000) ? 480 : p->h;
        w->h = ph * pct / 100;
    }
}
inline void lv_obj_get_coords(lv_obj_t* obj, lv_area_t* area) {
    if (!area) return;
    if (obj && obj->mock_widget) {
        area->x1 = obj->mock_widget->x; area->y1 = obj->mock_widget->y;
        area->x2 = obj->mock_widget->x + obj->mock_widget->w - 1;
        area->y2 = obj->mock_widget->y + obj->mock_widget->h - 1;
    } else { area->x1 = 0; area->y1 = 0; area->x2 = 99; area->y2 = 99; }
}
inline lv_obj_t* lv_obj_get_parent(lv_obj_t* obj) { return obj ? obj->parent : nullptr; }
inline lv_obj_t* lv_obj_get_child(lv_obj_t* obj, int idx) {
    if (!obj || !obj->mock_widget) return nullptr;
    int n = (int)obj->mock_widget->children.size();
    if (idx < 0) idx = n + idx;
    if (idx >= 0 && idx < n)
        return (lv_obj_t*)obj->mock_widget->children[idx]->lv_obj;
    return nullptr;
}
inline int lv_obj_get_child_count(lv_obj_t* obj) { return (obj && obj->mock_widget) ? (int)obj->mock_widget->children.size() : 0; }
inline int lv_obj_get_child_cnt(lv_obj_t* obj) { return (obj && obj->mock_widget) ? (int)obj->mock_widget->children.size() : 0; }
inline int lv_obj_get_event_count(lv_obj_t*) { return 0; }

#ifndef lv_obj_set_align
inline void lv_obj_set_align(lv_obj_t*, lv_align_t) {}
#endif
#ifndef lv_obj_align
inline void lv_obj_align(lv_obj_t*, lv_align_t, int, int) {}
#endif
inline void lv_obj_set_scroll_dir(lv_obj_t* obj, lv_dir_t d) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) obj->mock_widget->scrollDir = static_cast<int>(d);
#endif
    (void)obj; (void)d;
}
inline void lv_obj_set_scrollbar_mode(lv_obj_t* obj, int m) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) obj->mock_widget->scrollbarMode = m;
#endif
    (void)obj; (void)m;
}
inline void lv_obj_scroll_to_view(lv_obj_t*, int) {}
inline void lv_obj_scroll_to_view_recursive(lv_obj_t*, int) {}
inline void lv_obj_set_tile_id(lv_obj_t*, int, int, int) {}

#ifndef lv_obj_set_flex_flow
inline void lv_obj_set_flex_flow(lv_obj_t*, lv_flex_flow_t) {}
#endif
#ifndef lv_obj_set_flex_align
inline void lv_obj_set_flex_align(lv_obj_t*, lv_flex_align_t, lv_flex_align_t, lv_flex_align_t) {}
#endif
#ifndef lv_obj_set_flex_grow
inline void lv_obj_set_flex_grow(lv_obj_t*, int) {}
#endif

inline void lv_obj_set_grid_dsc_array(lv_obj_t*, const int*, const int*) {}
inline void lv_obj_set_grid_cell(lv_obj_t*, lv_grid_align_t, int, int, lv_grid_align_t, int, int) {}

inline void* lv_event_get_user_data(lv_event_t* e) { return e ? e->user_data : nullptr; }
inline lv_obj_t* lv_event_get_target(lv_event_t* e) { return e ? static_cast<lv_obj_t*>(e->target) : nullptr; }
inline void* lv_event_get_current_target(lv_event_t* e) { return e ? e->current_target : nullptr; }
inline void lv_event_stop_processing(lv_event_t*) {}
inline void* lv_event_get_param(lv_event_t* e) { return e ? e->param : nullptr; }
inline lv_event_code_t lv_event_get_code(lv_event_t* e) {
    return e ? static_cast<lv_event_code_t>(e->code) : LV_EVENT_CLICKED;
}
// The mock keeps the callbacks, and lv_obj_send_event calls them (lvgl_mock.h: Widget::events)
inline void lv_obj_add_event_cb(lv_obj_t* obj, lv_event_cb_t cb, int filter, void* user_data) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && cb) obj->mock_widget->events.push_back({reinterpret_cast<void*>(cb), filter, user_data});
#else
    (void)obj; (void)cb; (void)filter; (void)user_data;
#endif
}
inline bool lv_obj_remove_event_cb(lv_obj_t*, lv_event_cb_t) { return false; }
inline void lv_event_set_ext_draw_size(lv_event_t*, int32_t) {}
inline void lv_obj_refresh_ext_draw_size(lv_obj_t*) {}
inline void lv_obj_remove_event(lv_obj_t*, int) {}

#ifdef LVGL_MOCK_ENABLED
inline bool lv_obj_check_type(lv_obj_t* obj, const lv_obj_class_t* cls) {
    if (!obj || !obj->mock_widget) return false;
    const auto& t = obj->mock_widget->type;
    if (cls == &lv_textarea_class) return t == "TextArea";
    if (cls == &lv_btn_class)      return t == "Button";
    if (cls == &lv_label_class)    return t == "Label";
    if (cls == &lv_switch_class)   return t == "Switch";
    if (cls == &lv_slider_class)   return t == "Slider";
    if (cls == &lv_dropdown_class) return t == "Dropdown";
    if (cls == &lv_obj_class)      return t == "Container";
    return false;
}
#else
inline bool lv_obj_check_type(lv_obj_t*, const lv_obj_class_t*) { return false; }
#endif
inline lv_group_t* lv_group_get_default() { return nullptr; }
inline lv_obj_t* lv_group_get_focused(lv_group_t*) { return nullptr; }
// As LVGL 9 sends an event to an object: its LV_EVENT_PREPROCESS callbacks, then the others, in
// the order they were added. The mock has no class handlers and does not bubble to the parent
inline void lv_obj_send_event(lv_obj_t* obj, uint32_t code, void* param) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget) return;
    lv_event_t e{obj, nullptr, static_cast<int>(code)};
    e.current_target = obj;
    e.param = param;
    const auto callbacks = obj->mock_widget->events;  // one may add others
    for (const bool pre : {true, false}) {
        for (const auto& c : callbacks) {
            if (((c.filter & LV_EVENT_PREPROCESS) != 0) != pre) continue;
            const int filter = c.filter & ~LV_EVENT_PREPROCESS;
            if (filter != LV_EVENT_ALL && filter != static_cast<int>(code)) continue;
            e.user_data = c.userData;
            reinterpret_cast<lv_event_cb_t>(c.cb)(&e);
        }
    }
#else
    (void)obj; (void)code; (void)param;
#endif
}
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
#ifndef lv_obj_set_style_text_opa
inline void lv_obj_set_style_text_opa(lv_obj_t*, int, int) {}
#endif
inline void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, int) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && font) {
        obj->mock_widget->fontSize = font->line_height;
        obj->mock_widget->font = font;
    }
#endif
}
#ifndef lv_obj_set_style_text_align
inline void lv_obj_set_style_text_align(lv_obj_t*, lv_text_align_t, int) {}
#endif
// Box styles: the mock keeps them for CSS tests
#ifdef LVGL_MOCK_ENABLED
#define MOCK_SET(field, value) do { if (obj && obj->mock_widget) obj->mock_widget->field = (value); } while (0)
#else
#define MOCK_SET(field, value) do { (void)obj; (void)(value); } while (0)
#endif
// A style setter: a state's value goes apart (mockState), the element's own into the field
#define MOCK_STYLE(prop, field, value) do { if (mockState(obj, prop, sel, (value))) return; MOCK_SET(field, value); } while (0)
inline void lv_obj_set_style_border_width(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("border_width", borderWidth, v); }
inline void lv_obj_set_style_border_color(lv_obj_t* obj, lv_color_t c, int sel) { MOCK_STYLE("border_color", borderColor, c.full); }
inline void lv_obj_set_style_border_opa(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("border_opa", borderOpa, v); }
inline void lv_obj_set_style_border_side(lv_obj_t* obj, lv_border_side_t v, int sel) { MOCK_STYLE("border_side", borderSide, v); }
inline void lv_obj_set_style_margin_top(lv_obj_t* obj, int v, int) { MOCK_SET(marginTop, v); MOCK_SET(hasMarginTop, true); }
inline void lv_obj_set_style_margin_right(lv_obj_t* obj, int v, int) { MOCK_SET(marginRight, v); }
inline void lv_obj_set_style_margin_bottom(lv_obj_t* obj, int v, int) { MOCK_SET(marginBottom, v); MOCK_SET(hasMarginBottom, true); }
inline void lv_obj_set_style_margin_left(lv_obj_t* obj, int v, int) { MOCK_SET(marginLeft, v); }
inline void lv_obj_set_style_outline_opa(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("outline_opa", outlineOpa, v); }
inline void lv_obj_set_style_outline_pad(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("outline_pad", outlinePad, v); }
inline void lv_obj_set_style_shadow_offset_x(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("shadow_offset_x", shadowX, v); }
inline void lv_obj_set_style_shadow_offset_y(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("shadow_offset_y", shadowY, v); }
inline void lv_obj_set_style_shadow_spread(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("shadow_spread", shadowSpread, v); }
inline void lv_obj_set_style_bg_grad_color(lv_obj_t* obj, lv_color_t c, int) { MOCK_SET(gradColor, c.full); }
inline void lv_obj_set_style_bg_grad_dir(lv_obj_t* obj, lv_grad_dir_t v, int) { MOCK_SET(gradDir, static_cast<int>(v)); }
inline void lv_obj_set_style_bg_main_stop(lv_obj_t* obj, int v, int) { MOCK_SET(mainStop, v); }
inline void lv_obj_set_style_bg_grad_stop(lv_obj_t* obj, int v, int) { MOCK_SET(gradStop, v); }
inline void lv_obj_set_style_bg_main_opa(lv_obj_t* obj, int v, int) { MOCK_SET(mainOpa, v); }
inline void lv_obj_set_style_bg_grad_opa(lv_obj_t* obj, int v, int) { MOCK_SET(gradOpa, v); }
// Layout getters: what the mock was told
inline int lv_obj_get_style_layout(lv_obj_t* obj, int) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->flexFlow >= 0) return LV_LAYOUT_FLEX;
#endif
    (void)obj;
    return LV_LAYOUT_NONE;
}
inline lv_flex_flow_t lv_obj_get_style_flex_flow(lv_obj_t* obj, int) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->flexFlow >= 0) return static_cast<lv_flex_flow_t>(obj->mock_widget->flexFlow);
#endif
    (void)obj;
    return LV_FLEX_FLOW_ROW;
}
#ifdef LVGL_MOCK_ENABLED
#define MOCK_GET(field, def) ((obj && obj->mock_widget) ? obj->mock_widget->field : (def))
#else
#define MOCK_GET(field, def) ((void)obj, (def))
#endif
inline lv_flex_align_t lv_obj_get_style_flex_main_place(lv_obj_t* obj, int) { return static_cast<lv_flex_align_t>(MOCK_GET(flexMainAlign, 0)); }
inline void lv_obj_set_style_flex_main_place(lv_obj_t* obj, lv_flex_align_t v, int) {
    if (obj && obj->mock_widget)
        obj->mock_widget->flexMainAlign = (int)v;
}
// The element's own text-align only: the mock does not inherit styles
inline lv_text_align_t lv_obj_get_style_text_align(lv_obj_t* obj, int) {
    const int h = MOCK_GET(textAlignH, 0);
    return h == 1 ? LV_TEXT_ALIGN_CENTER : h == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_AUTO;
}
inline lv_flex_align_t lv_obj_get_style_flex_cross_place(lv_obj_t* obj, int) { return static_cast<lv_flex_align_t>(MOCK_GET(flexCrossAlign, 0)); }
inline lv_flex_align_t lv_obj_get_style_flex_track_place(lv_obj_t* obj, int) { return static_cast<lv_flex_align_t>(MOCK_GET(flexTrackAlign, 0)); }
inline int lv_obj_get_style_width(lv_obj_t* obj, int) { return MOCK_GET(w, 0); }
inline lv_border_side_t lv_obj_get_style_border_side(lv_obj_t* obj, int) {
    const int side = MOCK_GET(borderSide, -1);
    return static_cast<lv_border_side_t>(side < 0 ? LV_BORDER_SIDE_FULL : side);
}
inline int lv_obj_get_style_border_width(lv_obj_t* obj, int) { return MOCK_GET(borderWidth, 0); }
inline void lv_obj_mark_layout_as_dirty(lv_obj_t*) {}  // the mock lays nothing out
inline int lv_obj_get_style_height(lv_obj_t* obj, int) { return MOCK_GET(h, 0); }
inline void lv_obj_set_style_translate_x(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("translate_x", translateX, v); }
inline void lv_obj_set_style_text_decor(lv_obj_t* obj, lv_text_decor_t v, int sel) { MOCK_STYLE("text_decor", textDecor, static_cast<int>(v)); }
inline void lv_obj_set_style_transform_scale_x(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("transform_scale_x", scaleX, v); }
inline void lv_obj_set_style_transform_scale_y(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("transform_scale_y", scaleY, v); }
inline void lv_obj_set_style_transform_rotation(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("transform_rotation", rotation, v); }
inline void lv_obj_set_style_transform_pivot_x(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("transform_pivot_x", pivotX, v); }
inline void lv_obj_set_style_transform_pivot_y(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("transform_pivot_y", pivotY, v); }

// Style transitions
typedef uint8_t lv_style_prop_t;
enum {
    LV_STYLE_PROP_INV = 0,
    LV_STYLE_WIDTH, LV_STYLE_HEIGHT, LV_STYLE_TRANSLATE_X, LV_STYLE_TRANSLATE_Y,
    LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y, LV_STYLE_TRANSFORM_ROTATION,
    LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, LV_STYLE_BG_GRAD_COLOR,
    LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_OPA, LV_STYLE_BORDER_WIDTH,
    LV_STYLE_OUTLINE_WIDTH, LV_STYLE_OUTLINE_COLOR, LV_STYLE_OUTLINE_OPA,
    LV_STYLE_SHADOW_WIDTH, LV_STYLE_SHADOW_OFFSET_X, LV_STYLE_SHADOW_OFFSET_Y, LV_STYLE_SHADOW_SPREAD,
    LV_STYLE_SHADOW_COLOR, LV_STYLE_SHADOW_OPA,
    LV_STYLE_TEXT_COLOR, LV_STYLE_TEXT_OPA, LV_STYLE_OPA, LV_STYLE_RADIUS,
    LV_STYLE_BORDER_SIDE, LV_STYLE_TEXT_DECOR, LV_STYLE_TEXT_LETTER_SPACE,
    LV_STYLE_LAST_BUILT_IN_PROP = LV_STYLE_TEXT_LETTER_SPACE,
    // set and read one at a time (the margins of HTML blocks); css_undo.cpp's mock walks none of them
    LV_STYLE_MARGIN_TOP, LV_STYLE_MARGIN_BOTTOM, LV_STYLE_PAD_TOP, LV_STYLE_PAD_BOTTOM,
    LV_STYLE_TEXT_FONT  // taken off only (a browser-sized button's label: the button's font)
};
typedef struct {
    const lv_style_prop_t* props;
    void* user_data;
    lv_anim_path_cb_t path_xcb;
    uint32_t time;
    uint32_t delay;
} lv_style_transition_dsc_t;
inline void lv_style_transition_dsc_init(lv_style_transition_dsc_t* tr, const lv_style_prop_t props[],
                                         lv_anim_path_cb_t path_cb, uint32_t time, uint32_t delay, void* user_data) {
    tr->props = props;
    tr->path_xcb = path_cb;
    tr->time = time;
    tr->delay = delay;
    tr->user_data = user_data;
}
inline void lv_obj_set_style_transition(lv_obj_t* obj, const lv_style_transition_dsc_t* dsc, int sel) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget || !dsc) return;
    int n = 0;
    while (dsc->props && dsc->props[n] != LV_STYLE_PROP_INV) ++n;
    obj->mock_widget->transitions.push_back({sel, static_cast<int>(dsc->time), static_cast<int>(dsc->delay), n});
#else
    (void)obj; (void)dsc; (void)sel;
#endif
}
inline void lv_obj_set_style_text_letter_space(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("text_letter_space", letterSpace, v); }
inline void lv_obj_set_style_text_line_space(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("text_line_space", lineSpace, v); }

// The text color an object has: its own or its parent's
inline lv_color_t lv_obj_get_style_text_color(lv_obj_t* obj, int) {
#ifdef LVGL_MOCK_ENABLED
    for (const LvglMock::Widget* w = obj ? obj->mock_widget : nullptr; w; w = w->parent) {
        if (w->hasColor) return lv_color_hex(w->color);
    }
#endif
    (void)obj;
    return lv_color_hex(0xFFFFFF);
}
inline int lv_obj_get_style_text_opa(lv_obj_t*, int) { return 255; }
inline uint32_t lv_color_to_u32(lv_color_t c) { return 0xFF000000u | c.full; }
#ifndef lv_obj_set_style_pad_all
inline void lv_obj_set_style_pad_all(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_top
inline void lv_obj_set_style_pad_top(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_bottom
inline void lv_obj_set_style_pad_bottom(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_left
inline void lv_obj_set_style_pad_left(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_right
inline void lv_obj_set_style_pad_right(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_column
inline void lv_obj_set_style_pad_column(lv_obj_t*, int, int) {}
#endif
#ifndef lv_obj_set_style_pad_row
inline void lv_obj_set_style_pad_row(lv_obj_t*, int, int) {}
#endif
inline void lv_obj_set_style_shadow_width(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("shadow_width", shadowWidth, v); }
inline void lv_obj_set_style_shadow_color(lv_obj_t* obj, lv_color_t c, int sel) { MOCK_STYLE("shadow_color", shadowColor, c.full); }
inline void lv_obj_set_style_shadow_opa(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("shadow_opa", shadowOpa, v); }
inline void lv_obj_set_style_outline_width(lv_obj_t* obj, int v, int sel) { MOCK_STYLE("outline_width", outlineWidth, v); }
inline void lv_obj_set_style_outline_color(lv_obj_t* obj, lv_color_t c, int sel) { MOCK_STYLE("outline_color", outlineColor, c.full); }
inline int lv_obj_get_style_pad_left(lv_obj_t* obj, int) { return (obj && obj->mock_widget) ? obj->mock_widget->padLeft : 0; }
inline int lv_obj_get_style_pad_right(lv_obj_t* obj, int) { return (obj && obj->mock_widget) ? obj->mock_widget->padRight : 0; }
inline int lv_obj_get_style_pad_column(lv_obj_t* obj, int) { return (obj && obj->mock_widget) ? obj->mock_widget->padColumn : 0; }
// The font set on the object itself; the mock does not inherit styles, so none: nullptr
inline const lv_font_t* lv_obj_get_style_text_font(lv_obj_t* obj, int) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) return static_cast<const lv_font_t*>(obj->mock_widget->font);
#endif
    (void)obj;
    return nullptr;
}

// Label
inline void lv_label_set_text_fmt(lv_obj_t*, const char*, ...) {}
inline void lv_label_set_long_mode(lv_obj_t* obj, lv_label_long_mode_t m) { MOCK_SET(longMode, static_cast<int>(m)); }
inline lv_label_long_mode_t lv_label_get_long_mode(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->longMode >= 0) return static_cast<lv_label_long_mode_t>(obj->mock_widget->longMode);
#endif
    (void)obj;
    return LV_LABEL_LONG_WRAP;
}
inline lv_dir_t lv_obj_get_scroll_dir(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->scrollDir >= 0) return static_cast<lv_dir_t>(obj->mock_widget->scrollDir);
#endif
    (void)obj;
    return LV_DIR_ALL;
}

// ===== Local style properties by number (CSS undoes what it set: css_undo.cpp) =====
// The mock keeps a few of them, in its fields; a state's or a part's go to stateStyles
typedef union {
    int32_t num;
    const void* ptr;
    lv_color_t color;
} lv_style_value_t;
typedef enum { LV_STYLE_RES_NOT_FOUND, LV_STYLE_RES_FOUND } lv_style_res_t;

inline const char* mockPropName(lv_style_prop_t prop) {
    switch (prop) {
        case LV_STYLE_TEXT_COLOR: return "text_color";
        case LV_STYLE_TEXT_OPA: return "text_opa";
        case LV_STYLE_BG_COLOR: return "bg_color";
        case LV_STYLE_BG_OPA: return "bg_opa";
        case LV_STYLE_BORDER_WIDTH: return "border_width";
        case LV_STYLE_BORDER_COLOR: return "border_color";
        case LV_STYLE_BORDER_OPA: return "border_opa";
        case LV_STYLE_BORDER_SIDE: return "border_side";
        case LV_STYLE_RADIUS: return "radius";
        case LV_STYLE_TEXT_DECOR: return "text_decor";
        case LV_STYLE_TEXT_LETTER_SPACE: return "text_letter_space";
        default: return nullptr;
    }
}
inline bool mockIsColor(lv_style_prop_t prop) {
    return prop == LV_STYLE_TEXT_COLOR || prop == LV_STYLE_BG_COLOR || prop == LV_STYLE_BORDER_COLOR;
}

#ifdef LVGL_MOCK_ENABLED
// The field of a property of the element as it is, and the value it has when nothing set it
inline int* mockField(LvglMock::Widget* w, lv_style_prop_t prop, int& unset) {
    switch (prop) {
        case LV_STYLE_TEXT_OPA: unset = 255; return &w->textOpa;
        case LV_STYLE_BG_OPA: unset = -1; return &w->bgOpa;
        case LV_STYLE_BORDER_WIDTH: unset = 0; return &w->borderWidth;
        case LV_STYLE_BORDER_OPA: unset = 255; return &w->borderOpa;
        case LV_STYLE_BORDER_SIDE: unset = -1; return &w->borderSide;
        case LV_STYLE_RADIUS: unset = 0; return &w->radius;
        case LV_STYLE_TEXT_DECOR: unset = -1; return &w->textDecor;
        case LV_STYLE_TEXT_LETTER_SPACE: unset = 0; return &w->letterSpace;
        case LV_STYLE_PAD_TOP: unset = 0; return &w->padTop;
        case LV_STYLE_PAD_BOTTOM: unset = 0; return &w->padBottom;
        default: return nullptr;
    }
}
#endif

inline lv_style_res_t lv_obj_get_local_style_prop(lv_obj_t* obj, lv_style_prop_t prop, lv_style_value_t* v,
                                                  lv_style_selector_t sel) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget || !v) return LV_STYLE_RES_NOT_FOUND;
    LvglMock::Widget* w = obj->mock_widget;
    *v = lv_style_value_t{};
    long long value = 0;
    if (sel != LV_PART_MAIN) {
        const char* name = mockPropName(prop);
        const LvglMock::Widget::StateStyle* st = name ? w->stateStyle(name, static_cast<int>(sel)) : nullptr;
        if (!st) return LV_STYLE_RES_NOT_FOUND;
        value = st->value;
    } else if (prop == LV_STYLE_TEXT_COLOR || prop == LV_STYLE_BG_COLOR) {
        const bool set = prop == LV_STYLE_TEXT_COLOR ? w->hasColor : w->hasBgcolor;
        if (!set) return LV_STYLE_RES_NOT_FOUND;
        value = prop == LV_STYLE_TEXT_COLOR ? w->color : w->bgcolor;
    } else if (prop == LV_STYLE_BORDER_COLOR) {
        if (!w->borderColor) return LV_STYLE_RES_NOT_FOUND;
        value = w->borderColor;
    } else if (prop == LV_STYLE_MARGIN_TOP || prop == LV_STYLE_MARGIN_BOTTOM) {
        const bool top = prop == LV_STYLE_MARGIN_TOP;
        if (!(top ? w->hasMarginTop : w->hasMarginBottom)) return LV_STYLE_RES_NOT_FOUND;
        value = top ? w->marginTop : w->marginBottom;
    } else {
        int unset = 0;
        const int* f = mockField(w, prop, unset);
        if (!f || *f == unset) return LV_STYLE_RES_NOT_FOUND;
        value = *f;
    }
    if (mockIsColor(prop)) v->color.full = static_cast<uint32_t>(value);
    else v->num = static_cast<int32_t>(value);
    return LV_STYLE_RES_FOUND;
#else
    (void)obj; (void)prop; (void)v; (void)sel;
    return LV_STYLE_RES_NOT_FOUND;
#endif
}

// The value a property has on the object: the mock knows its own margins and paddings
inline lv_style_value_t lv_obj_get_style_prop(lv_obj_t* obj, int part, lv_style_prop_t prop) {
    lv_style_value_t v{};
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && part == LV_PART_MAIN) {
        const LvglMock::Widget* w = obj->mock_widget;
        if (prop == LV_STYLE_MARGIN_TOP) v.num = w->marginTop;
        else if (prop == LV_STYLE_MARGIN_BOTTOM) v.num = w->marginBottom;
        else if (prop == LV_STYLE_PAD_TOP) v.num = w->padTop;
        else if (prop == LV_STYLE_PAD_BOTTOM) v.num = w->padBottom;
    }
#else
    (void)obj; (void)part; (void)prop;
#endif
    return v;
}

inline bool lv_obj_remove_local_style_prop(lv_obj_t* obj, lv_style_prop_t prop, lv_style_selector_t sel) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget) return false;
    LvglMock::Widget* w = obj->mock_widget;
    if (sel != LV_PART_MAIN) {
        const char* name = mockPropName(prop);
        if (!name) return false;
        auto& ss = w->stateStyles;
        const auto n = ss.size();
        ss.erase(std::remove_if(ss.begin(), ss.end(),
                                [&](const LvglMock::Widget::StateStyle& x) { return x.prop == name && x.selector == static_cast<int>(sel); }),
                 ss.end());
        return ss.size() != n;
    }
    if (prop == LV_STYLE_TEXT_COLOR) { w->hasColor = false; w->color = 0; return true; }
    if (prop == LV_STYLE_BG_COLOR) { w->hasBgcolor = false; w->bgcolor = 0; return true; }
    if (prop == LV_STYLE_BORDER_COLOR) { w->borderColor = 0; return true; }
    if (prop == LV_STYLE_MARGIN_TOP) { w->hasMarginTop = false; w->marginTop = 0; return true; }
    if (prop == LV_STYLE_MARGIN_BOTTOM) { w->hasMarginBottom = false; w->marginBottom = 0; return true; }
    if (prop == LV_STYLE_TEXT_FONT) { w->font = nullptr; w->fontSize = 0; return true; }
    int unset = 0;
    int* f = mockField(w, prop, unset);
    if (!f) return false;
    *f = unset;
    return true;
#else
    (void)obj; (void)prop; (void)sel;
    return false;
#endif
}

inline void lv_obj_set_local_style_prop(lv_obj_t* obj, lv_style_prop_t prop, lv_style_value_t v, lv_style_selector_t sel) {
#ifdef LVGL_MOCK_ENABLED
    if (!obj || !obj->mock_widget) return;
    LvglMock::Widget* w = obj->mock_widget;
    const long long value = mockIsColor(prop) ? static_cast<long long>(v.color.full) : static_cast<long long>(v.num);
    if (sel != LV_PART_MAIN) {
        if (const char* name = mockPropName(prop)) w->stateStyles.push_back({name, static_cast<int>(sel), value});
        return;
    }
    if (prop == LV_STYLE_TEXT_COLOR) { w->hasColor = true; w->color = static_cast<uint32_t>(value); return; }
    if (prop == LV_STYLE_BG_COLOR) { w->hasBgcolor = true; w->bgcolor = static_cast<uint32_t>(value); return; }
    if (prop == LV_STYLE_BORDER_COLOR) { w->borderColor = static_cast<uint32_t>(value); return; }
    if (prop == LV_STYLE_MARGIN_TOP) { w->hasMarginTop = true; w->marginTop = static_cast<int>(value); return; }
    if (prop == LV_STYLE_MARGIN_BOTTOM) { w->hasMarginBottom = true; w->marginBottom = static_cast<int>(value); return; }
    int unset = 0;
    if (int* f = mockField(w, prop, unset)) *f = static_cast<int>(value);
#else
    (void)obj; (void)prop; (void)v; (void)sel;
#endif
}
inline const char* lv_label_get_text(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) return obj->mock_widget->text.c_str();
#endif
    return "";
}

// Slider
#ifndef lv_slider_set_value
inline void lv_slider_set_value(lv_obj_t*, int, int) {}
#endif
#ifndef lv_slider_set_range
inline void lv_slider_set_range(lv_obj_t*, int, int) {}
#endif
inline int lv_slider_get_value(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->curValue : 0; }

// Switch
inline bool lv_switch_get_state(lv_obj_t* obj) { return (obj && obj->mock_widget) ? obj->mock_widget->checked : false; }

// Dropdown
inline void lv_dropdown_set_options(lv_obj_t* obj, const char* opts) {
    if (obj && obj->mock_widget) obj->mock_widget->text = opts ? opts : "";
}
inline void lv_dropdown_get_options(lv_obj_t* obj, char* buf, uint32_t len) {
    if (!buf || len == 0) return;
    if (obj && obj->mock_widget) {
        strncpy(buf, obj->mock_widget->text.c_str(), len - 1);
        buf[len - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
}
inline void lv_dropdown_set_selected(lv_obj_t* obj, uint16_t idx) {
    if (obj && obj->mock_widget) obj->mock_widget->curValue = (int)idx;
}
inline uint16_t lv_dropdown_get_selected(lv_obj_t* obj) {
    return (obj && obj->mock_widget) ? (uint16_t)obj->mock_widget->curValue : 0;
}

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
#ifndef LVGL_MOCK_ENABLED
inline void lv_textarea_set_max_length(lv_obj_t*, uint32_t) {}
#endif
inline void lv_dropdown_open(lv_obj_t*) {}
inline void lv_dropdown_set_symbol(lv_obj_t*, const void*) {}
inline lv_obj_t* lv_dropdown_get_list(lv_obj_t*) { return nullptr; }
inline const char* lv_textarea_get_text(lv_obj_t* obj) {
    if (obj && obj->mock_widget) return obj->mock_widget->text.c_str();
    return "";
}

// Keyboard
inline void lv_keyboard_set_textarea(lv_obj_t*, lv_obj_t*) {}
inline void lv_keyboard_set_mode(lv_obj_t*, lv_keyboard_mode_t) {}

// Image
inline void lv_img_set_src(lv_obj_t*, const void*) {}
inline void lv_img_set_zoom(lv_obj_t*, int) {}

// Canvas
// LVGL sizes a canvas by its buffer (an image sized by its source)
inline void lv_canvas_set_buffer(lv_obj_t* obj, void*, int w, int h, int) { lv_obj_set_size(obj, w, h); }
inline void lv_canvas_fill_bg(lv_obj_t*, lv_color_t, lv_opa_t) {}
inline void lv_canvas_set_px(lv_obj_t*, int, int, lv_color_t, lv_opa_t) {}

// Tabview
inline void lv_tabview_set_tab_bar_position(lv_obj_t*, int) {}
inline void lv_tabview_set_tab_bar_size(lv_obj_t*, int) {}
inline lv_obj_t* lv_tabview_get_tab_bar(lv_obj_t*) { return nullptr; }
inline lv_obj_t* lv_tabview_add_tab(lv_obj_t*, const char*) { return nullptr; }
inline lv_obj_t* lv_tabview_get_content(lv_obj_t*) { return nullptr; }
inline lv_obj_t* lv_tileview_get_tile_act(lv_obj_t*) { return nullptr; }

// Timer — mock with storage for manual firing
struct MockTimer {
    void(*callback)(lv_timer_t*) = nullptr;
    void* user_data = nullptr;
    int period_ms = 0;
    int repeat_count = -1;  // -1 = infinite
    bool deleted = false;
};

#include <vector>
inline std::vector<MockTimer*>& _mock_timers() {
    static std::vector<MockTimer*> timers;
    return timers;
}

inline lv_timer_t* lv_timer_create(void(*cb)(lv_timer_t*), int period, void* user_data) {
    auto* mt = new MockTimer();
    mt->callback = cb;
    mt->user_data = user_data;
    mt->period_ms = period;
    _mock_timers().push_back(mt);
    return (lv_timer_t*)mt;
}
inline void lv_timer_del(lv_timer_t* t) {
    if (t) ((MockTimer*)t)->deleted = true;
}
inline void lv_timer_delete(lv_timer_t* t) { lv_timer_del(t); }
inline void lv_timer_set_repeat_count(lv_timer_t* t, int count) {
    if (t) ((MockTimer*)t)->repeat_count = count;
}
inline void* lv_timer_get_user_data(lv_timer_t* t) {
    return t ? ((MockTimer*)t)->user_data : nullptr;
}
inline lv_timer_t* lv_timer_get_next(lv_timer_t* prev) {
    auto& timers = _mock_timers();
    if (!prev) return timers.empty() ? nullptr : (lv_timer_t*)timers[0];
    for (size_t i = 0; i < timers.size(); i++) {
        if ((lv_timer_t*)timers[i] == prev && i + 1 < timers.size())
            return (lv_timer_t*)timers[i + 1];
    }
    return nullptr;
}
// Fire all pending mock timers (for testing)
inline int lv_mock_fire_timers() {
    int fired = 0;
    auto& timers = _mock_timers();
    // Copy to avoid mutation during iteration
    auto copy = timers;
    for (auto* mt : copy) {
        if (!mt->deleted && mt->callback) {
            mt->callback((lv_timer_t*)mt);
            fired++;
        }
    }
    // Clean up deleted timers
    timers.erase(
        std::remove_if(timers.begin(), timers.end(), [](MockTimer* mt) {
            if (mt->deleted) { delete mt; return true; }
            return false;
        }), timers.end());
    return fired;
}
inline void lv_mock_clear_timers() {
    for (auto* mt : _mock_timers()) delete mt;
    _mock_timers().clear();
}
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
typedef struct { uint32_t total_size; uint32_t free_size; uint32_t used_cnt; uint8_t frag_pct; } lv_mem_monitor_t;
inline void lv_mem_monitor(lv_mem_monitor_t*) {}
#define LV_EVENT_CANCEL 99
#define LV_EVENT_ALL 0xFF
inline lv_obj_t* lv_keyboard_get_textarea(lv_obj_t*) { return nullptr; }
inline void lv_obj_set_ext_click_area(lv_obj_t*, int) {}
#define LV_SCROLLBAR_MODE_ACTIVE 1
#define LV_SCROLLBAR_MODE_ON 2
#define LV_SCROLLBAR_MODE_AUTO 3
inline int lv_obj_get_scrollbar_mode(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget && obj->mock_widget->scrollbarMode >= 0) return obj->mock_widget->scrollbarMode;
#endif
    (void)obj;
    return LV_SCROLLBAR_MODE_AUTO;
}
inline void lv_obj_move_foreground(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    auto* w = obj ? obj->mock_widget : nullptr;
    if (!w || !w->parent) return;
    auto& ch = w->parent->children;
    ch.erase(std::remove(ch.begin(), ch.end(), w), ch.end());
    ch.push_back(w);
#endif
}
inline void lv_obj_move_background(lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    auto* w = obj ? obj->mock_widget : nullptr;
    if (!w || !w->parent) return;
    auto& ch = w->parent->children;
    ch.erase(std::remove(ch.begin(), ch.end(), w), ch.end());
    ch.insert(ch.begin(), w);
#endif
}
inline void lv_obj_move_to_index(lv_obj_t* obj, int32_t index) {
#ifdef LVGL_MOCK_ENABLED
    auto* w = obj ? obj->mock_widget : nullptr;
    if (!w || !w->parent) return;
    auto& ch = w->parent->children;
    ch.erase(std::remove(ch.begin(), ch.end(), w), ch.end());
    if (index < 0 || index >= (int)ch.size()) ch.push_back(w);
    else ch.insert(ch.begin() + index, w);
#endif
}
inline int32_t lv_obj_get_index(const lv_obj_t* obj) {
#ifdef LVGL_MOCK_ENABLED
    auto* w = obj ? obj->mock_widget : nullptr;
    if (!w || !w->parent) return -1;
    auto& ch = w->parent->children;
    for (int i = 0; i < (int)ch.size(); i++) {
        if (ch[i] == w) return i;
    }
#endif
    return -1;
}
#define LV_RADIUS_CIRCLE 0x7FFF
inline void lv_obj_set_style_transform_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_transform_height(lv_obj_t*, int, int) {}
inline int lv_obj_get_style_pad_bottom(lv_obj_t* obj, int) { return (obj && obj->mock_widget) ? obj->mock_widget->padBottom : 0; }
inline int lv_obj_get_style_max_height(lv_obj_t*, int) { return 0; }
inline int lv_obj_get_style_min_height(lv_obj_t*, int) { return 0; }
inline int lv_obj_get_style_max_width(lv_obj_t*, int) { return 0; }
inline int lv_obj_get_style_min_width(lv_obj_t*, int) { return 0; }
inline int lv_obj_get_style_pad_top(lv_obj_t* obj, int) { return (obj && obj->mock_widget) ? obj->mock_widget->padTop : 0; }

// Touch input states
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0

// lv_timer_handler alias
inline uint32_t lv_timer_handler() { return 0; }
inline void lv_obj_set_style_opa(lv_obj_t* obj, int opa, int sel) {
    if (mockState(obj, "opa", sel, opa)) return;
#ifdef LVGL_MOCK_ENABLED
    if (obj && obj->mock_widget) obj->mock_widget->opacity = opa;
#endif
}
