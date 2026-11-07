#include "core/crash_log.h"
#include "core/build_info.h"
#include "utils/log_config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <cstddef>
#include <cstring>
#include <string>

static const char* TAG = "CrashLog";

namespace CrashLog {

static constexpr uint32_t kMagic = 0x52554e31;  // "RUN1"

// This run, kept through a reset in RTC memory (not initialised at boot: boot() checks it)
static RTC_NOINIT_ATTR Run s_run;
static Run s_last;           // the run before, if s_haveLast
static bool s_haveLast = false;

static uint32_t sumOf(const Run& r) {  // FNV-1a of all but sum
    const auto* p = reinterpret_cast<const uint8_t*>(&r);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(Run, sum); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void seal() { s_run.sum = sumOf(s_run); }

// Into dst of n bytes: letters, digits, "._-"; anything else as "_" (the line stays plain JSON)
static void copyName(char* dst, size_t n, const char* src) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; i++) {
        const char c = src[i];
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '.' || c == '_' || c == '-' || c == '+';
        dst[i] = plain ? c : '_';
    }
    dst[i] = '\0';
}

void boot() {
    s_haveLast = s_run.magic == kMagic && s_run.sum == sumOf(s_run);
    if (s_haveLast) {
        s_last = s_run;
        s_last.fw[sizeof(s_last.fw) - 1] = s_last.part[sizeof(s_last.part) - 1] =
            s_last.app[sizeof(s_last.app) - 1] = '\0';
    }
    memset(&s_run, 0, sizeof(s_run));
    s_run.magic = kMagic;
    copyName(s_run.fw, sizeof(s_run.fw), BuildInfo::id());
    const esp_partition_t* running = esp_ota_get_running_partition();
    copyName(s_run.part, sizeof(s_run.part), running ? running->label : "");
    seal();
}

void tick(uint32_t nowMs) {
    const uint32_t s = nowMs / 1000;
    if (s == s_run.uptimeS) return;
    s_run.uptimeS = s;
    seal();
}

void setApp(const char* app) {
    // "/apps/weather/weather.bax" -> "weather": the folder of the file
    const char* name = app ? app : "";
    size_t len = strlen(name);
    if (const char* last = strrchr(name, '/')) {
        const char* first = last;
        while (first > name && first[-1] != '/') first--;
        if (first == last) { name = last + 1; len = strlen(name); }  // "/weather"
        else { len = static_cast<size_t>(last - first); name = first; }
    }
    char buf[sizeof(s_run.app)];
    const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, name, n);
    buf[n] = '\0';
    copyName(s_run.app, sizeof(s_run.app), buf);
    seal();
}

const char* resetName(int r) {
    switch (r) {
        case ESP_RST_POWERON:   return "poweron";
        case ESP_RST_EXT:       return "ext";
        case ESP_RST_SW:        return "sw";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "int_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

static bool isCrash(int r) {
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
           r == ESP_RST_BROWNOUT;
}

P::String line(int resetReason, const Run* last, bool rolledBack) {
    if (!isCrash(resetReason) && !rolledBack) return {};
    P::String out = "{\"reset\":\"";
    out += resetName(resetReason);
    out += '"';
    if (last) {
        out += ",\"up\":";
        out += std::to_string(last->uptimeS).c_str();
        if (last->app[0]) { out += ",\"app\":\""; out += last->app; out += '"'; }
        out += ",\"fw\":\""; out += last->fw;
        out += "\",\"part\":\""; out += last->part; out += '"';
    }
    if (rolledBack) out += ",\"rollback\":true";
    out += '}';
    return out;
}

static P::String readFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return {};
    P::String s(f.size(), '\0');
    const size_t n = f.readBytes(&s[0], s.size());
    f.close();
    s.resize(n);
    return s;
}

bool append(const char* path, const P::String& line, int keep) {
    // The lines there, the newest last: keep the last keep - 1, then this one
    const P::String old = readFile(path);
    P::Array<P::String> lines;
    for (size_t at = 0; at < old.size();) {
        size_t end = old.find('\n', at);
        if (end == P::String::npos) end = old.size();
        if (end > at) lines.emplace_back(old.substr(at, end - at));
        at = end + 1;
    }
    const size_t drop = lines.size() + 1 > static_cast<size_t>(keep) ? lines.size() + 1 - keep : 0;

    if (const char* slash = strrchr(path, '/'); slash && slash != path) {
        const P::String dir(path, slash - path);
        if (!LittleFS.exists(dir.c_str())) LittleFS.mkdir(dir.c_str());
    }
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    for (size_t i = drop; i < lines.size(); i++) {
        f.write(reinterpret_cast<const uint8_t*>(lines[i].c_str()), lines[i].size());
        f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
    }
    f.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.size());
    f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
    f.close();
    return true;
}

P::String read() { return readFile(kPath); }

bool clear() { return !LittleFS.exists(kPath) || LittleFS.remove(kPath); }

void begin(const char* invalidPart) {
    const int reason = static_cast<int>(esp_reset_reason());
    // Rolled back: the last run's partition is the one the bootloader gave up on, not ours
    const esp_partition_t* running = esp_ota_get_running_partition();
    const bool rolledBack = s_haveLast && invalidPart && running && strcmp(s_last.part, invalidPart) == 0 &&
                            strcmp(s_last.part, running->label) != 0;

    const P::String l = line(reason, s_haveLast ? &s_last : nullptr, rolledBack);
    if (l.empty()) {
        LOG_I(Log::APP, "Last reset: %s", resetName(reason));
        return;
    }
    LOG_W(Log::APP, "Last boot: %s", l.c_str());
    if (!append(kPath, l)) LOG_E(Log::APP, "Can't write %s", kPath);
}

} // namespace CrashLog
