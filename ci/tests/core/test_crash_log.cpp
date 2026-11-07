/**
 * Test: CrashLog — how boots ended, in /system/crashes.log.
 *
 *   - line(): what is recorded and what is not (a reboot, a power-on)
 *   - append(): the last 10 lines, newest last
 *   - boot() / setApp() / tick() / begin(): the run of the boot before, through a "reset"
 *     (the host keeps s_run in memory as the watch keeps it in RTC memory)
 *   - a rollback: the last run's partition is the one the bootloader gave up on
 *   - sys crash, sys crash clear
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "core/crash_log.h"
#include "console/console.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "LittleFS.h"
#include "utils/log_config.h"

#define TEST(name) printf("  %-56s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

template <size_t N>
static void put(std::array<char, N>& a, const char* s) { snprintf(a.data(), N, "%s", s); }

static CrashLog::Run run(uint32_t up, const char* app, const char* fw, const char* part) {
    CrashLog::Run r = {};
    r.uptimeS = up;
    put(r.app, app);
    put(r.fw, fw);
    put(r.part, part);
    return r;
}

static const esp_partition_t kApp0 = { 0x10000, "app0" };
static const esp_partition_t kApp1 = { 0x310000, "app1" };

// A boot of the firmware in `part` that ran `ms` with `app` open, then a reset for `reason`
static void bootThenReset(const esp_partition_t& part, const char* app, uint32_t ms, esp_reset_reason_t reason) {
    OtaMock::running = part;
    CrashLog::boot();
    CrashLog::setApp(app);
    CrashLog::tick(ms);
    g_mock_reset_reason = reason;
}

// The boot after: the bootloader's rollback, if any, as OtaConfirm::begin() finds it
static void nextBoot() {
    CrashLog::boot();
    CrashLog::begin(OtaMock::invalid ? OtaMock::invalid->label : nullptr);
}

static int lines() {
    const std::string s = LittleFS.readFile(CrashLog::kPath);
    return (int)std::count(s.begin(), s.end(), '\n');
}

static bool expectLog(const char* want) {
    const std::string got = LittleFS.readFile(CrashLog::kPath);
    if (got == want) return true;
    printf("\n      got  '%s'\n      want '%s'\n", got.c_str(), want);
    return false;
}

int main() {
    printf("=== CrashLog Tests ===\n\n");
    int passed = 0, total = 0;
    Log::setAll(Log::Disabled);  // begin() logs; the harness reads output for "FAIL"

    printf("line:\n");

    TEST("power-on, reboot: nothing to record");
    {
        auto r = run(10, "", "dev", "app0");
        if (CrashLog::line(ESP_RST_POWERON, &r, false).empty() && CrashLog::line(ESP_RST_SW, &r, false).empty() &&
            CrashLog::line(ESP_RST_UNKNOWN, nullptr, false).empty())
            PASS();
        else FAIL("");
    }

    TEST("panic with no run kept: the reason only");
    {
        if (CrashLog::line(ESP_RST_PANIC, nullptr, false) == "{\"reset\":\"panic\"}") PASS();
        else FAIL(CrashLog::line(ESP_RST_PANIC, nullptr, false).c_str());
    }

    TEST("panic: uptime, app, firmware, partition");
    {
        auto r = run(183, "weather", "fc686e0", "app1");
        const P::String l = CrashLog::line(ESP_RST_PANIC, &r, false);
        if (l == "{\"reset\":\"panic\",\"up\":183,\"app\":\"weather\",\"fw\":\"fc686e0\",\"part\":\"app1\"}") PASS();
        else FAIL(l.c_str());
    }

    TEST("watchdogs and brownout are recorded");
    {
        auto r = run(5, "", "dev", "app0");
        bool ok = true;
        for (auto reason : {ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_BROWNOUT}) {
            const P::String l = CrashLog::line(reason, &r, false);
            const std::string want = std::string("{\"reset\":\"") + CrashLog::resetName(reason) + "\"";
            ok = ok && l.compare(0, want.size(), want.c_str()) == 0;
        }
        if (ok && std::string(CrashLog::resetName(ESP_RST_TASK_WDT)) == "task_wdt") PASS();
        else FAIL("");
    }

    TEST("in the launcher: no app");
    {
        auto r = run(7, "", "dev", "app0");
        const P::String l = CrashLog::line(ESP_RST_PANIC, &r, false);
        if (l == "{\"reset\":\"panic\",\"up\":7,\"fw\":\"dev\",\"part\":\"app0\"}") PASS();
        else FAIL(l.c_str());
    }

    TEST("a reboot that was rolled back is recorded");
    {
        auto r = run(12, "", "9a8b7c6", "app1");
        const P::String l = CrashLog::line(ESP_RST_SW, &r, true);
        if (l == "{\"reset\":\"sw\",\"up\":12,\"fw\":\"9a8b7c6\",\"part\":\"app1\",\"rollback\":true}") PASS();
        else FAIL(l.c_str());
    }

    printf("\nappend:\n");

    TEST("into a new file, the folder made");
    {
        LittleFS.reset();
        if (CrashLog::append(CrashLog::kPath, "{\"reset\":\"panic\"}") && expectLog("{\"reset\":\"panic\"}\n")) PASS();
        else FAIL("");
    }

    TEST("the last 10 kept, newest last");
    {
        LittleFS.reset();
        for (int i = 1; i <= 12; i++) CrashLog::append(CrashLog::kPath, ("{\"n\":" + std::to_string(i) + "}").c_str());
        std::string want;
        for (int i = 3; i <= 12; i++) want += "{\"n\":" + std::to_string(i) + "}\n";
        if (expectLog(want.c_str()) && lines() == 10) PASS();
        else FAIL("");
    }

    TEST("a line without its newline (cut short) is kept as a line");
    {
        LittleFS.reset();
        LittleFS.writeFile(CrashLog::kPath, "{\"a\":1}\n{\"b\":2}");
        CrashLog::append(CrashLog::kPath, "{\"c\":3}");
        if (expectLog("{\"a\":1}\n{\"b\":2}\n{\"c\":3}\n")) PASS();
        else FAIL("");
    }

    printf("\nthe boot before:\n");

    TEST("panic in an app: its run recorded at the next boot");
    {
        LittleFS.reset();
        OtaMock::reset();
        bootThenReset(kApp0, "/apps/weather/weather.bax", 183400, ESP_RST_PANIC);
        nextBoot();
        if (expectLog("{\"reset\":\"panic\",\"up\":183,\"app\":\"weather\",\"fw\":\"dev\",\"part\":\"app0\"}\n")) PASS();
        else FAIL("");
    }

    TEST("a reboot or power-on: nothing added");
    {
        bootThenReset(kApp0, "", 5000, ESP_RST_SW);
        nextBoot();
        bootThenReset(kApp0, "", 5000, ESP_RST_POWERON);
        nextBoot();
        if (lines() == 1) PASS();
        else FAIL("");
    }

    TEST("a native app by its name; back in the launcher, none");
    {
        LittleFS.reset();
        bootThenReset(kApp0, "counter", 2000, ESP_RST_TASK_WDT);
        nextBoot();
        bootThenReset(kApp0, "counter", 1000, ESP_RST_PANIC);
        CrashLog::setApp("");
        nextBoot();
        if (expectLog("{\"reset\":\"task_wdt\",\"up\":2,\"app\":\"counter\",\"fw\":\"dev\",\"part\":\"app0\"}\n"
                      "{\"reset\":\"panic\",\"up\":1,\"fw\":\"dev\",\"part\":\"app0\"}\n"))
            PASS();
        else FAIL("");
    }

    TEST("an app name stays plain JSON");
    {
        LittleFS.reset();
        bootThenReset(kApp0, "/apps/my \"app\"/main.bax", 3000, ESP_RST_PANIC);
        nextBoot();
        if (expectLog("{\"reset\":\"panic\",\"up\":3,\"app\":\"my__app_\",\"fw\":\"dev\",\"part\":\"app0\"}\n")) PASS();
        else FAIL("");
    }

    printf("\nrollback:\n");

    TEST("new firmware in app1 reset while pending: rolled back");
    {
        LittleFS.reset();
        OtaMock::reset();
        bootThenReset(kApp1, "", 5000, ESP_RST_SW);  // the bootloader gives up on app1:
        OtaMock::invalid = &kApp1;                     // the previous firmware, app0, runs
        OtaMock::running = kApp0;
        nextBoot();
        if (expectLog("{\"reset\":\"sw\",\"up\":5,\"fw\":\"dev\",\"part\":\"app1\",\"rollback\":true}\n")) PASS();
        else FAIL("");
    }

    TEST("an old invalid partition is not this boot's rollback");
    {
        LittleFS.reset();
        OtaMock::invalid = &kApp1;  // from long ago
        bootThenReset(kApp0, "", 60000, ESP_RST_PANIC);
        nextBoot();
        bootThenReset(kApp0, "", 60000, ESP_RST_SW);
        nextBoot();
        if (expectLog("{\"reset\":\"panic\",\"up\":60,\"fw\":\"dev\",\"part\":\"app0\"}\n")) PASS();
        else FAIL("");
        OtaMock::reset();
    }

    printf("\nsys crash:\n");

    TEST("sys crash: the log as text, the number of lines");
    {
        LittleFS.reset();
        LittleFS.writeFile(CrashLog::kPath, "{\"reset\":\"panic\"}\n{\"reset\":\"brownout\"}\n");
        auto r = Console::exec("sys crash");
        const std::string text = r.payload ? std::string((const char*)r.payload, r.payloadSize) : "";
        free(r.payload);
        if (r.success && r.type == Console::ResponseType::StringData &&
            text == "{\"reset\":\"panic\"}\n{\"reset\":\"brownout\"}\n")
            PASS();
        else FAIL("");
    }

    TEST("sys crash with none: ok, no payload");
    {
        LittleFS.reset();
        auto r = Console::exec("sys crash");
        if (r.success && !r.payload && r.payloadSize == 0) PASS();
        else FAIL("");
    }

    TEST("sys crash clear: the log removed");
    {
        LittleFS.writeFile(CrashLog::kPath, "{\"reset\":\"panic\"}\n");
        auto r = Console::exec("sys crash clear");
        if (r.success && !LittleFS.exists(CrashLog::kPath) && lines() == 0) PASS();
        else FAIL("");
    }

    TEST("sys crash <other>: invalid");
    {
        auto r = Console::exec("sys crash now");
        if (!r.success && r.errorCode == Console::ErrorCode::INVALID) PASS();
        else FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CRASHLOG TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d CRASHLOG TESTS PASSED ===\n", passed, total);
    return 1;
}
