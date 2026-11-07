#pragma once
/**
 * Device — Hardware Abstraction Layer
 *
 * Abstract base class for board-specific hardware.
 * Each board implements this interface in hal/boards/<board_name>/
 *
 * Design:
 *   - Compile-time constants (#define) live in board_config.h
 *   - Runtime behavior (init, callbacks) live in Device subclass
 *   - Hot-path callbacks (flush, touch) are raw C function pointers —
 *     registered once, called by LVGL directly, zero vtable overhead
 */

#include <lvgl.h>

class Device {
public:
    virtual ~Device() = default;

    // ---- One-time initialization (called during display_init) ----
    virtual bool initPanel() = 0;
    virtual bool initTouch() = 0;
    virtual void setBrightness(uint8_t level) = 0;  // 0=off, 255=max

    // ---- Optional storage (default: board has no SD card) ----
    virtual bool mountSdCard() { return false; }

    // ---- Optional power (default: the board does not light-sleep) ----
    // Light sleep for at most `ms`, or until a touch wakes the chip: false if the board cannot,
    // and the caller waits as usual. Called with the display asleep (Power::rest)
    virtual bool lightSleep(uint32_t ms) { (void)ms; return false; }

    // ---- Callback providers (called once, result stored by LVGL) ----
    virtual lv_display_flush_cb_t  getFlushCallback() = 0;
    virtual lv_indev_read_cb_t     getTouchCallback() = 0;

    // ---- Singleton ----
    static Device& inst();
};
