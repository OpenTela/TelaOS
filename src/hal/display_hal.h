#pragma once
/**
 * Display HAL — public API
 *
 * Board-independent display initialization and control.
 * Uses Device::inst() for hardware-specific operations.
 * Uses SCREEN_WIDTH / SCREEN_HEIGHT from board_config.h for compile-time constants.
 */

#include <lvgl.h>

// Buffer size presets: fractions of screen height (power-of-2 divisors)
enum DisplayBuffer {
    BufferMicro   = SCREEN_HEIGHT / 32,   // 1/32 — emergency only
    BufferSmall   = SCREEN_HEIGHT / 16,   // 1/16 — compact, usable with BLE
    BufferOptimal = SCREEN_HEIGHT / 8,    // 1/8  — default, LVGL recommended
    BufferMax     = SCREEN_HEIGHT / 4     // 1/4  — max performance
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
