#include "core/power.h"

#include "ble/ble_bridge.h"
#include "core/app_manager.h"
#include "hal/device.h"
#include "hal/display_hal.h"
#include "utils/log_config.h"

#include <Arduino.h>
#include <algorithm>

namespace Power {

namespace {

const char* TAG = "Power";
constexpr uint32_t kRestMs = 5;  // the loop's pause, as it always was

bool s_enabled = false;

// One stretch of the display asleep: how much of it the chip slept, for the log on waking
bool s_wasAsleep = false;
uint32_t s_asleepSince = 0;
uint32_t s_sleptMs = 0;
uint32_t s_sleeps = 0;

void watchDisplay(bool asleep) {
    if (asleep == s_wasAsleep) return;
    s_wasAsleep = asleep;
    if (asleep) {
        s_asleepSince = millis();
        s_sleptMs = 0;
        s_sleeps = 0;
    } else if (s_sleeps) {
        LOG_I(Log::APP, "display asleep %u ms, light sleep %u ms in %u naps",
              (unsigned)(millis() - s_asleepSince), (unsigned)s_sleptMs, (unsigned)s_sleeps);
    }
}

}  // namespace

uint32_t sleepFor(const When& when, uint32_t untilNextMs) {
    if (!when.enabled || !when.displayAsleep || when.ble || when.appTimers) return 0;
    const uint32_t ms = std::min(untilNextMs, kMaxSleepMs);
    return ms >= kMinSleepMs ? ms : 0;
}

void rest(uint32_t untilNextMs) {
    const bool asleep = display_is_asleep();
    watchDisplay(asleep);
    When when;
    when.enabled = s_enabled;
    when.displayAsleep = asleep;
    when.ble = BLEBridge::isInitialized();
    when.appTimers = App::Manager::instance().appTimers() > 0;
    const uint32_t ms = sleepFor(when, untilNextMs);
    if (ms) {
        const uint32_t start = millis();
        if (Device::inst().lightSleep(ms)) {
            s_sleptMs += millis() - start;
            ++s_sleeps;
            delay(1);  // the other tasks' turn: the idle task feeds the watchdog
            return;
        }
    }
    delay(kRestMs);
}

void setLightSleep(bool on) { s_enabled = on; }

}  // namespace Power
