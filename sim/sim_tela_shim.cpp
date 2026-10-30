// sim_tela_shim.cpp — what the ESP32 Arduino core provides on the watch, for the simulator:
// global objects, log_printf, the clock and the flash file system.
#include "sim_tela.h"

#include "Arduino.h"
#include "LittleFS.h"  // TelaCI's in-memory LittleFS: same API, already covered by their tests
#include "Wire.h"
#include "lvgl.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

SerialClass Serial;
TwoWire Wire;
TwoWire Wire1;
EspClass ESP;
LittleFSClass LittleFS;

// LV_STDLIB_CUSTOM: TelaOS's lv_custom_alloc.cpp provides malloc/realloc/free but not the
// monitor/test hooks. On the watch --gc-sections drops their only callers; here they are
// weak no-ops so the link succeeds (and a real TelaOS implementation would win).
extern "C" __attribute__((weak)) void lv_mem_monitor_core(lv_mem_monitor_t* mon) {
    std::memset(mon, 0, sizeof(*mon));
}
extern "C" __attribute__((weak)) lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

extern "C" int log_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int n = std::vprintf(fmt, args);
    va_end(args);
    return n;
}

namespace {
uint32_t g_ms = 0;
time_t g_epoch = 1793354970;  // power-on wall clock: 2026-10-30 10:09:30 UTC
std::function<void(uint32_t)> g_delay_hook;
}  // namespace

// Linked with -Wl,--wrap=time: every time() in TelaOS (launcher clock, Lua os.time) reads
// the virtual clock, so screenshots do not depend on when the simulator was started.
extern "C" time_t __wrap_time(time_t* t) {
    const time_t now = g_epoch + static_cast<time_t>(g_ms / 1000);
    if (t) *t = now;
    return now;
}

uint32_t sim_millis() { return g_ms; }

void sim_delay(uint32_t ms) {
    g_ms += ms;
    if (g_delay_hook) g_delay_hook(ms);
}

namespace SimTela {

uint32_t now() { return g_ms; }

void setDelayHook(std::function<void(uint32_t)> h) { g_delay_hook = std::move(h); }

int flashData(const std::string& data_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    int files = 0;
    for (auto it = fs::recursive_directory_iterator(data_dir, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        const std::string rel = "/" + fs::relative(it->path(), data_dir).generic_string();
        if (it->is_directory()) {
            LittleFS.mkdir(rel.c_str());
        } else if (it->is_regular_file()) {
            std::ifstream in(it->path(), std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            LittleFS.writeFile(rel.c_str(), bytes.data(), bytes.size());
            ++files;
        }
    }
    return ec ? -1 : files;
}

}  // namespace SimTela
