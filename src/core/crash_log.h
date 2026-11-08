#pragma once

#include <array>
#include <cstdint>
#include "utils/psram_alloc.h"

/**
 * CrashLog - the watch remembers how its boots ended.
 *
 * A boot that ended in a crash - an exception or abort() ("panic"), a watchdog ("int_wdt",
 * "task_wdt", "wdt") or a dip in power ("brownout") - becomes one line of /system/crashes.log
 * at the next boot, from esp_reset_reason(). The last kKeep lines are kept, newest last, JSON:
 *
 *   {"reset":"panic","up":183,"app":"weather","fw":"fc686e0","part":"app1"}
 *
 * up, app, fw and part - how long that boot ran in seconds, the app open, its firmware
 * (BuildInfo::id) and the partition it ran from - come from a record of the run in RTC memory.
 * A reset keeps it, a power-off does not: then the line has "reset" only. A boot that the
 * bootloader rolled back (OtaConfirm) is recorded whatever ended it, with "rollback":true.
 *
 * Console: sys crash (the log, why the last boot ended), sys crash clear.
 */
namespace CrashLog {

constexpr const char* kPath = "/system/crashes.log";
constexpr int kKeep = 10;

/// What a run leaves for the next boot (RTC memory: plain bytes, the texts end with '\0')
struct Run {
    uint32_t magic;
    uint32_t uptimeS;          // how long it ran, to the second
    std::array<char, 16> fw;   // BuildInfo::id()
    std::array<char, 8> part;  // the partition it ran from: "app0", "app1"
    std::array<char, 24> app;  // the app open, "" in the launcher
    uint32_t sum;              // of all above: a power-off leaves noise
};

/// First thing in setup(): keep the last run, start this one
void boot();

/// LittleFS mounted: the last boot into the log if it crashed or was rolled back.
/// invalidPart: the partition the bootloader gave up on (OtaConfirm::invalidPartition()), or nullptr
void begin(const char* invalidPart);

/// From loop(): this run's uptime
void tick(uint32_t nowMs);

/// The app open now: an app's path ("/apps/weather/weather.bax"), a native app's name,
/// "" for the launcher
void setApp(const char* app);

/// The line for the last boot: why it reset, its run (null if none kept), whether the
/// bootloader rolled it back. "" when there is nothing to record.
P::String line(int resetReason, const Run* last, bool rolledBack);

/// esp_reset_reason_t by name: "panic", "poweron", ...
const char* resetName(int resetReason);

/// Add a line to the log at `path`, keeping the last `keep`. False if it can't be written.
bool append(const char* path, const P::String& line, int keep = kKeep);

/// The log, "" if none
P::String read();

/// Empty the log
bool clear();

} // namespace CrashLog
