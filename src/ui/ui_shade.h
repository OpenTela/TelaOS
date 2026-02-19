#pragma once
/**
 * ui_shade.h — System shade (swipe-down overlay)
 *
 * Features:
 *   - Swipe down from top edge to open
 *   - Toggle buttons: BT, Auto-off
 *   - Inactivity tracker: dim@45s, sleep@60s
 */

#include <lvgl.h>

namespace Shade {

void init();          // Call after display_init + LVGL ready (UI only)
void applyConfig();   // Call after systemConfig.load() (reads settings, starts BLE)
void open();
void close();
bool isOpen();

// Inactivity
void setAutoOff(bool enabled);
bool autoOffEnabled();

}  // namespace Shade
