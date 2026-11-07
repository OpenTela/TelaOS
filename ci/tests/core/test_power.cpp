/**
 * Test: Power — when the loop light-sleeps, and how the T-Watch 2020 sleeps (docs/POWER.md).
 *
 * TelaCI builds the firmware for BOARD_TWATCH_2020: Device::inst() is the T-Watch. Its
 * esp_light_sleep_start() (ci/stubs/esp_sleep.h) returns at once and keeps what it was asked to
 * wake on; driver/gpio.h gives the touch INT the level a test sets.
 */
#include <cstdio>
#include <string>
#include <LittleFS.h>
#include "core/app_manager.h"
#include "core/power.h"
#include "console/console.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "hal/device.h"
#include "hal/boards/twatch2020/twatch2020_config.h"
#include "utils/log_config.h"

// display_hal.h wants the board's screen size: these two are all the test takes from it
void display_sleep();
void display_wake();

#define TEST(name) printf("  %-56s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

static Power::When may() {
    Power::When w;
    w.enabled = true;
    w.displayAsleep = true;
    return w;
}

int main() {
    printf("=== Power Tests ===\n\n");
    int passed = 0, total = 0;
    Log::setAll(Log::Disabled);

    printf("sleepFor:\n");
    TEST("all allows: until LVGL's next timer");
    {
        if (Power::sleepFor(may(), 33) == 33) PASS();
        else FAIL("");
    }
    TEST("not without the setting, the display asleep");
    {
        Power::When off = may(), awake = may();
        off.enabled = false;
        awake.displayAsleep = false;
        if (Power::sleepFor(off, 33) == 0 && Power::sleepFor(awake, 33) == 0) PASS();
        else FAIL("");
    }
    TEST("not with BLE, not with the app's <timer> running");
    {
        Power::When ble = may(), timers = may();
        ble.ble = true;
        timers.appTimers = true;
        if (Power::sleepFor(ble, 33) == 0 && Power::sleepFor(timers, 33) == 0) PASS();
        else FAIL("");
    }
    TEST("a timer due in less than 5 ms: delay as always");
    {
        if (Power::sleepFor(may(), 4) == 0 && Power::sleepFor(may(), 5) == 5) PASS();
        else FAIL("");
    }
    TEST("no timer due: at most a second");
    {
        if (Power::sleepFor(may(), 0xFFFFFFFFu) == Power::kMaxSleepMs) PASS();
        else FAIL("");
    }

    printf("\nT-Watch 2020 lightSleep:\n");
    TEST("wakes on the timer and on the touch INT going low");
    {
        g_sleep_mock = SleepMock{};
        g_gpio_mock.level[PIN_TOUCH_INT] = 1;
        const bool ok = Device::inst().lightSleep(30);
        if (ok && g_sleep_mock.sleeps == 1 && g_sleep_mock.sleptTimerUs == 30000 && g_sleep_mock.sleptGpio &&
            g_sleep_mock.sleptPinWakeup[PIN_TOUCH_INT] == GPIO_INTR_LOW_LEVEL)
            PASS();
        else FAIL("");
    }
    TEST("after it: no wakeup source left on");
    {
        if (g_sleep_mock.timerUs == 0 && !g_sleep_mock.gpio && g_gpio_mock.wakeup[PIN_TOUCH_INT] == GPIO_INTR_DISABLE)
            PASS();
        else FAIL("");
    }
    TEST("INT already low (a finger on the glass): the timer only");
    {
        g_sleep_mock = SleepMock{};
        g_gpio_mock.level[PIN_TOUCH_INT] = 0;
        Device::inst().lightSleep(30);
        if (g_sleep_mock.sleeps == 1 && g_sleep_mock.sleptTimerUs == 30000 && !g_sleep_mock.sleptGpio &&
            g_sleep_mock.sleptPinWakeup[PIN_TOUCH_INT] == GPIO_INTR_DISABLE)
            PASS();
        else FAIL("");
        g_gpio_mock.level[PIN_TOUCH_INT] = 1;
    }
    TEST("a sleep that failed says so: the loop waits as always");
    {
        g_sleep_mock = SleepMock{};
        g_sleep_mock.result = -1;
        if (!Device::inst().lightSleep(30)) PASS();
        else FAIL("");
        g_sleep_mock.result = ESP_OK;
    }

    printf("\nrest and sys config:\n");
    LittleFS.reset();
    LittleFS.mkdir("/apps");
    LittleFS.mkdir("/system");
    App::Manager::instance().init();  // defines the settings, power.light_sleep off

    TEST("the setting off: the display asleep, no light sleep");
    {
        g_sleep_mock = SleepMock{};
        display_sleep();
        Power::rest(33);
        if (g_sleep_mock.sleeps == 0) PASS();
        else FAIL("");
    }
    TEST("sys config power.light_sleep true: set, saved, applied");
    {
        auto r = Console::exec("sys config power.light_sleep true");
        g_sleep_mock = SleepMock{};
        Power::rest(33);
        const bool saved = LittleFS.readFile("/system/config.yml").find("light_sleep: true") != std::string::npos;
        if (r.success && App::Manager::instance().systemConfig.getBool("power.light_sleep") && saved &&
            g_sleep_mock.sleeps == 1 && g_sleep_mock.sleptTimerUs == 33000)
            PASS();
        else FAIL("");
    }
    TEST("the display awake: no light sleep");
    {
        display_wake();
        g_sleep_mock = SleepMock{};
        Power::rest(33);
        if (g_sleep_mock.sleeps == 0) PASS();
        else FAIL("");
    }
    TEST("sys config <key> without a value: the value, nothing set");
    {
        auto r = Console::exec("sys config power.sleep_timeout");
        if (r.success && App::Manager::instance().systemConfig.getInt("power.sleep_timeout") == 60) PASS();
        else FAIL("");
    }
    TEST("sys config: an unknown key, no key");
    {
        auto unknown = Console::exec("sys config power.warp_drive on");
        auto none = Console::exec("sys config");
        if (!unknown.success && unknown.errorCode == "not_found" && !none.success && none.errorCode == "invalid")
            PASS();
        else FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d POWER TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d POWER TESTS PASSED ===\n", passed, total);
    return 1;
}
