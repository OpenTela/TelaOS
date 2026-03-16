#pragma once
/**
 * ui_coords.h — Coordinate & size parsing (px / %)
 *
 * Position (x, y): % from screen size (absolute positioning)
 * Size (w, h):     % via lv_pct() — LVGL resolves from parent at layout time
 */

#include <lvgl.h>
#include <cstdint>

/// Position: % from screen (for x, y)
int32_t parse_coord_w(const char* s, lv_obj_t* parent = nullptr);
int32_t parse_coord_h(const char* s, lv_obj_t* parent = nullptr);

/// Size: % via lv_pct() — works inside flex/table containers (for w, h)
int32_t parse_size(const char* s);
