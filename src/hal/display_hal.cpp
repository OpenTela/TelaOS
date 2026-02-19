/**
 * Display HAL — board-independent LVGL initialization.
 *
 * All hardware-specific work is delegated to Device::inst().
 * This file only knows about LVGL, buffers, and mutexes.
 */

#include "display_hal.h"
#include "device.h"
#include "ui/ui_touch.h"
#include <Arduino.h>
#include "esp_heap_caps.h"

// ============================================
// State
// ============================================
static lv_color_t*      s_buf       = nullptr;
static size_t            s_bufSize   = 0;
static int               s_bufLines  = 0;
static int               s_bootLines = 0;      // max achieved at boot (fragmentation ceiling)
static lv_display_t*     s_display   = nullptr;
static SemaphoreHandle_t s_mutex     = nullptr;

// ============================================
// display_init
// ============================================
bool display_init() {
    Serial.println("\n[Display] Initializing...");

    auto& dev = Device::inst();

    // 1. Backlight off during init
    dev.setBrightness(0);

    // 2. Panel controller init (ST7701 / ILI9341 / etc.)
    if (!dev.initPanel()) {
        Serial.println("[Display] ERROR: initPanel() failed!");
        return false;
    }

    // 3. Touch controller init
    if (!dev.initTouch()) {
        Serial.println("[Display] WARNING: initTouch() failed");
        // Non-fatal: display works without touch
    }

    // 4. Backlight on
    dev.setBrightness(255);
    Serial.println("[Backlight] ON (100%)");

    // 5. LVGL core init
    Serial.println("[LVGL] Init...");
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    // PNG decoder status
    #if LV_USE_LODEPNG
    Serial.println("[LVGL] lodepng decoder ready");
    #elif LV_USE_PNG
    Serial.println("[LVGL] PNG decoder ready");
    #else
    Serial.println("[LVGL] WARNING: No PNG decoder!");
    #endif

    // Filesystem
    #if LV_USE_FS_STDIO
    Serial.println("[LVGL] FS STDIO ready (C: -> /littlefs)");
    #endif

    // 6. Allocate LVGL draw buffer (DRAM, fallback chain)
    const DisplayBuffer fallback[] = { BufferMax, BufferOptimal, BufferSmall, BufferMicro };
    s_buf = nullptr;

    for (int i = 0; i < 4 && !s_buf; i++) {
        s_bufLines = static_cast<int>(fallback[i]);
        s_bufSize  = SCREEN_WIDTH * s_bufLines * sizeof(lv_color_t);
        s_buf = (lv_color_t*)heap_caps_malloc(s_bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_buf) {
            Serial.printf("[LVGL] Buffer %d lines (%u bytes) — not enough DRAM\n",
                          s_bufLines, (unsigned)s_bufSize);
        }
    }

    if (!s_buf) {
        Serial.println("[LVGL] FATAL: Cannot allocate ANY buffer!");
        return false;
    }

    Serial.printf("[LVGL] Buffer: %d lines x 1 (%u bytes)\n", s_bufLines, (unsigned)s_bufSize);
    s_bootLines = s_bufLines;  // remember ceiling for later resize

    // 7. Create LVGL display — flush callback from Device (raw function pointer)
    s_display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_display, dev.getFlushCallback());
    lv_display_set_buffers(s_display, s_buf, nullptr, s_bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 8. Create LVGL input device — touch callback from Device (raw function pointer)
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, dev.getTouchCallback());

    // 9. Virtual touch device for remote control (tap/swipe simulation)
    TouchSim::init();

    // 10. Mutex
    s_mutex = xSemaphoreCreateMutex();

    Serial.println("[LVGL] OK");
    Serial.println("[Display] Ready!");
    return true;
}

// ============================================
// Thread safety
// ============================================
void display_lock() {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void display_unlock() {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

// ============================================
// Framebuffer (for screenshots)
// ============================================
void* display_get_framebuffer() {
    return nullptr;
}

void display_set_brightness(uint8_t level) {
    Device::inst().setBrightness(level);
}

void display_sleep() {
    // TODO: per-board light sleep with touch wakeup
    //   ESP4848:   esp_light_sleep_start() + GPIO wakeup on touch INT
    //   TWatch:    AXP power off display + ext0 wakeup
    //   ESP8048:   TBD
    Device::inst().setBrightness(0);
}

void display_wake() {
    // TODO: per-board wake sequence (re-init panel if needed)
    Device::inst().setBrightness(255);
}

// ============================================
// Dynamic buffer management
// ============================================
int display_get_buffer_lines() {
    return s_bufLines;
}

int display_get_boot_lines() {
    return s_bootLines;
}

bool display_set_buffer(DisplayBuffer size) {
    if (!s_display) {
        Serial.println("[Display] Error: display not initialized");
        return false;
    }

    int targetLines = static_cast<int>(size);

    if (s_bufLines == targetLines) {
        Serial.printf("[Display] Buffer already %d lines\n", s_bufLines);
        return true;
    }

    size_t newSize = SCREEN_WIDTH * targetLines * sizeof(lv_color_t);
    size_t oldSize = s_bufSize;

    Serial.printf("[Display] Resize: %d -> %d lines (need %u bytes)\n",
                  s_bufLines, targetLines, (unsigned)newSize);
    Serial.printf("[Display] DRAM before free: %u bytes\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Free old buffer first to make room
    lv_color_t* oldBuf = s_buf;
    s_buf = nullptr;

    if (oldBuf) {
        heap_caps_free(oldBuf);
        Serial.printf("[Display] Freed old buffer (%u bytes), DRAM now: %u\n",
                      (unsigned)oldSize,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }

    // Allocate new
    lv_color_t* newBuf = (lv_color_t*)heap_caps_malloc(newSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!newBuf) {
        Serial.printf("[Display] FAILED: not enough DRAM for %d lines\n", targetLines);
        // Restore old size as fallback
        newBuf = (lv_color_t*)heap_caps_malloc(oldSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (newBuf) {
            s_buf = newBuf;
            lv_display_set_buffers(s_display, s_buf, nullptr, s_bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
            Serial.println("[Display] Restored previous buffer size");
        } else {
            Serial.println("[Display] CRITICAL: Cannot allocate ANY buffer!");
        }
        return false;
    }

    s_buf      = newBuf;
    s_bufSize  = newSize;
    s_bufLines = targetLines;

    lv_display_set_buffers(s_display, s_buf, nullptr, s_bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("[Display] OK: buffer now %d lines, DRAM free: %u\n",
                  s_bufLines, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.flush();
    return true;
}
