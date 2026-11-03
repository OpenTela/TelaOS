#pragma once

/**
 * System paths — single source of truth.
 *
 * Directory paths always end with '/'.
 * File paths: SYS_APPS "myapp/myapp.bax"
 * LVGL paths: SYS_LVGL_PREFIX SYS_APPS "myapp/icon.png"
 *
 * Use #define (not constexpr) for compile-time string concatenation.
 */

// LVGL filesystem driver prefix
#define SYS_LVGL_PREFIX     "C:"

// App storage directory
#define SYS_APPS            "/apps/"

// System icons directory
#define SYS_ICONS           "/system/resources/icons/"

// Fonts kept as files: the icon font of each size and its names (UI::Font::loadFiles)
#define SYS_FONTS           "/system/fonts/"
