#pragma once
/**
 * ui_touch.h — Virtual touch simulation for remote control
 * 
 * Creates a second LVGL indev (virtual pointer).
 * No board-specific code touched.
 * 
 * Usage:
 *   TouchSim::init();                          // once, after display_init
 *   TouchSim::tap(120, 160);                   // tap at x,y
 *   TouchSim::hold(120, 160, 500);             // hold 500ms
 *   TouchSim::swipe(0, 120, 240, 120, 300);    // swipe over 300ms
 */

#include <cstdint>

namespace TouchSim {

/// Initialize virtual indev. Call once after LVGL display init.
void init();

/// Simulate tap at (x, y). Press 50ms, release.
void tap(int x, int y);

/// Simulate long press at (x, y) for duration_ms.
void hold(int x, int y, int duration_ms = 500);

/// Simulate swipe from (x1,y1) to (x2,y2) over duration_ms.
void swipe(int x1, int y1, int x2, int y2, int duration_ms = 300);

/// Is simulation currently active?
bool busy();

} // namespace TouchSim
