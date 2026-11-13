#pragma once
/**
 * Display HAL — public API
 *
 * Board-independent display initialization and control.
 * Uses Device::inst() for hardware-specific operations.
 * Uses SCREEN_WIDTH / SCREEN_HEIGHT from board_config.h for compile-time constants.
 */

#include <lvgl.h>

// Buffer size presets: the lines LVGL draws a pass, fractions of the screen's height. The more
// lines a pass, the faster the screen: BufferMax is the default, and smaller ones are only for
// DRAM running short (decision 24 of docs/ROADMAP.md). 480 lines: 180, 90, 45, 22
enum DisplayBuffer {
    BufferMicro   = SCREEN_HEIGHT * 3 / 64,  // 3/64 — emergency only
    BufferSmall   = SCREEN_HEIGHT * 3 / 32,  // 3/32 — compact, usable with BLE
    BufferOptimal = SCREEN_HEIGHT * 3 / 16,  // 3/16 — an app's <display buffer="optimal">
    BufferMax     = SCREEN_HEIGHT * 3 / 8    // 3/8  — the default: a screen in 3 passes; 2 DRAM does not hold
};

// Initialize display hardware + LVGL (board-independent)
bool display_init();

// Dynamic buffer management
bool display_set_buffer(DisplayBuffer size);
int  display_get_buffer_lines();
int  display_get_boot_lines();    // max achieved at boot (fragmentation ceiling)

// LVGL thread safety
void display_lock();
void display_unlock();

// Framebuffer access (for screenshots; may return nullptr)
void* display_get_framebuffer();

// Brightness control (0=off, 255=max)
void display_set_brightness(uint8_t level);

// Sleep / Wake
// TODO: per-board implementation (light sleep + touch wakeup on ESP32, etc.)
void display_sleep();   // screen off, enter low-power mode
void display_wake();    // restore screen, exit low-power mode
