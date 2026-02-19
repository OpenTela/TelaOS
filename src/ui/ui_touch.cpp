#include "ui/ui_touch.h"
#include "utils/log_config.h"
#include <lvgl.h>
#include <Arduino.h>

static const char* TAG = "TouchSim";

namespace TouchSim {

// === State machine ===
enum State { IDLE, PRESSED, RELEASING };

static State s_state = IDLE;
static int   s_x = 0, s_y = 0;           // current point
static int   s_x1 = 0, s_y1 = 0;         // swipe start
static int   s_x2 = 0, s_y2 = 0;         // swipe end
static uint32_t s_startMs = 0;            // start time
static uint32_t s_durationMs = 0;         // total duration
static bool  s_isSwipe = false;           // swipe mode (interpolate)
static lv_indev_t* s_indev = nullptr;

// === LVGL read callback ===
static void sim_read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    if (s_state == IDLE) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    uint32_t elapsed = millis() - s_startMs;
    
    if (s_state == PRESSED) {
        if (elapsed >= s_durationMs) {
            // Time's up — release
            s_state = RELEASING;
            // Final position (end of swipe or same point)
            if (s_isSwipe) {
                s_x = s_x2;
                s_y = s_y2;
            }
            data->point.x = s_x;
            data->point.y = s_y;
            data->state = LV_INDEV_STATE_RELEASED;
            
            LOG_D(Log::UI, "TouchSim: released at %d,%d", s_x, s_y);
            s_state = IDLE;
            return;
        }
        
        // Still pressing — interpolate position for swipe
        if (s_isSwipe && s_durationMs > 0) {
            float t = (float)elapsed / (float)s_durationMs;
            s_x = s_x1 + (int)((s_x2 - s_x1) * t);
            s_y = s_y1 + (int)((s_y2 - s_y1) * t);
        }
        
        data->point.x = s_x;
        data->point.y = s_y;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }
    
    // RELEASING — already handled above, fallback
    data->state = LV_INDEV_STATE_RELEASED;
    s_state = IDLE;
}

// === Public API ===

void init() {
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, sim_read_cb);
    
    LOG_I(Log::UI, "TouchSim: virtual indev created");
}

void tap(int x, int y) {
    s_x = x;
    s_y = y;
    s_x1 = x; s_y1 = y;
    s_x2 = x; s_y2 = y;
    s_isSwipe = false;
    s_durationMs = 50;
    s_startMs = millis();
    s_state = PRESSED;
    
    LOG_D(Log::UI, "TouchSim: tap %d,%d", x, y);
}

void hold(int x, int y, int duration_ms) {
    s_x = x;
    s_y = y;
    s_x1 = x; s_y1 = y;
    s_x2 = x; s_y2 = y;
    s_isSwipe = false;
    s_durationMs = duration_ms;
    s_startMs = millis();
    s_state = PRESSED;
    
    LOG_D(Log::UI, "TouchSim: hold %d,%d for %dms", x, y, duration_ms);
}

void swipe(int x1, int y1, int x2, int y2, int duration_ms) {
    s_x = x1;
    s_y = y1;
    s_x1 = x1; s_y1 = y1;
    s_x2 = x2; s_y2 = y2;
    s_isSwipe = true;
    s_durationMs = duration_ms;
    s_startMs = millis();
    s_state = PRESSED;
    
    LOG_D(Log::UI, "TouchSim: swipe %d,%d -> %d,%d in %dms", x1, y1, x2, y2, duration_ms);
}

bool busy() {
    return s_state != IDLE;
}

} // namespace TouchSim
