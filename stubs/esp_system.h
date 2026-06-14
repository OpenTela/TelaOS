#pragma once
#include <cstdint>

class EspClass {
public:
    uint32_t getFreeHeap() { return 100000; }
    uint32_t getMinFreeHeap() { return 80000; }
    uint32_t getPsramSize() { return 8000000; }
    uint32_t getFreePsram() { return 7000000; }
    const char* getChipModel() { return "ESP32-S3"; }
    uint32_t getCpuFreqMHz() { return 240; }
    void restart() {}
};
extern EspClass ESP;

// Free-function reboot used by OTA. Captured for host tests.
inline bool g_esp_restarted = false;
inline void esp_restart() { g_esp_restarted = true; }

#include <sys/time.h>
inline int settimeofday(const struct timeval*, const struct timezone*) { return 0; }
