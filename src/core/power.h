#pragma once

#include <cstdint>

/**
 * Power — the loop's rest between its passes (docs/POWER.md).
 *
 * As always: delay(5). Light sleep instead, when the watch may: the setting power.light_sleep on,
 * the display asleep (nothing to draw), no BLE (its controller keeps time on the main clock), no
 * <timer> of the app running (FreeRTOS timers stand still in light sleep: a stopwatch would stop),
 * and a board that can (Device::lightSleep). For as long as LVGL's next timer allows: the touch is
 * read then, as always, and a tap wakes the screen.
 */
namespace Power {

struct When {
    bool enabled = false;        // power.light_sleep
    bool displayAsleep = false;  // display_is_asleep()
    bool ble = false;            // BLEBridge::isInitialized()
    bool appTimers = false;      // App::Manager::appTimers() > 0
};

constexpr uint32_t kMinSleepMs = 5;     // less: delay(5) as always, not worth a wake-up
constexpr uint32_t kMaxSleepMs = 1000;  // no LVGL timer due: still a look around once a second

/// How long the loop may light-sleep now; 0: not now. untilNextMs: lv_timer_handler()'s answer
uint32_t sleepFor(const When& when, uint32_t untilNextMs);

/// After the loop's pass, with the display unlocked
void rest(uint32_t untilNextMs);

/// power.light_sleep (Shade::applyConfig)
void setLightSleep(bool on);

}  // namespace Power
