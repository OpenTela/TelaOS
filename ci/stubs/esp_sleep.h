#pragma once
// esp_sleep.h — TelaCI stub: light sleep that returns at once, and what it was asked to wake on.
#include <cstdint>
#include "driver/gpio.h"

typedef enum {
    ESP_SLEEP_WAKEUP_UNDEFINED,
    ESP_SLEEP_WAKEUP_ALL,
    ESP_SLEEP_WAKEUP_EXT0,
    ESP_SLEEP_WAKEUP_EXT1,
    ESP_SLEEP_WAKEUP_TIMER,
    ESP_SLEEP_WAKEUP_TOUCHPAD,
    ESP_SLEEP_WAKEUP_ULP,
    ESP_SLEEP_WAKEUP_GPIO,
    ESP_SLEEP_WAKEUP_UART,
} esp_sleep_source_t;

struct SleepMock {
    uint64_t timerUs = 0;  // the wakeup sources on now
    bool gpio = false;
    int sleeps = 0;        // esp_light_sleep_start() calls
    uint64_t sleptTimerUs = 0;  // the sources the last sleep had
    bool sleptGpio = false;
    gpio_int_type_t sleptPinWakeup[49] = {};
    esp_err_t result = ESP_OK;
};
inline SleepMock g_sleep_mock;

inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) {
    g_sleep_mock.timerUs = us;
    return ESP_OK;
}
inline esp_err_t esp_sleep_enable_gpio_wakeup() {
    g_sleep_mock.gpio = true;
    return ESP_OK;
}
inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source) {
    const bool all = source == ESP_SLEEP_WAKEUP_ALL;
    if (all || source == ESP_SLEEP_WAKEUP_TIMER)
        g_sleep_mock.timerUs = 0;
    if (all || source == ESP_SLEEP_WAKEUP_GPIO)
        g_sleep_mock.gpio = false;
    return ESP_OK;
}
inline esp_err_t esp_light_sleep_start() {
    ++g_sleep_mock.sleeps;
    g_sleep_mock.sleptTimerUs = g_sleep_mock.timerUs;
    g_sleep_mock.sleptGpio = g_sleep_mock.gpio;
    for (int i = 0; i < 49; ++i) g_sleep_mock.sleptPinWakeup[i] = g_gpio_mock.wakeup[i];
    return g_sleep_mock.result;
}
