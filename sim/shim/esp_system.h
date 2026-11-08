// esp_system.h for the simulator: ESP's memory figures come from the watch's heaps
// (sim_heap.cpp), as Arduino-ESP32 takes them from heap_caps. TelaCI keeps its own stub.
#pragma once

#include <cstdint>

#include "esp_heap_caps.h"

class EspClass {
public:
    uint32_t getFreeHeap() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
    uint32_t getMinFreeHeap() { return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL); }
    uint32_t getHeapSize() { return heap_caps_get_total_size(MALLOC_CAP_INTERNAL); }
    uint32_t getMaxAllocHeap() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL); }
    uint32_t getPsramSize() { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM); }
    uint32_t getFreePsram() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
    uint32_t getMinFreePsram() { return heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM); }
    uint32_t getMaxAllocPsram() { return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
    const char* getChipModel() { return "ESP32-S3"; }
    uint32_t getCpuFreqMHz() { return 240; }
    void restart() {}
};
extern EspClass ESP;

// Free-function reboot used by OTA. Captured for host tests.
inline bool g_esp_restarted = false;
inline void esp_restart() { g_esp_restarted = true; }

// Why the chip reset (ESP-IDF 4.4 esp_system.h): the simulator is always just switched on
typedef enum {
    ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO,
} esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

#include <sys/time.h>
inline int settimeofday(const struct timeval*, const struct timezone*) { return 0; }
