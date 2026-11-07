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

// Why the chip reset (ESP-IDF 4.4 esp_system.h); tests set g_mock_reset_reason
typedef enum {
    ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO,
} esp_reset_reason_t;
inline esp_reset_reason_t g_mock_reset_reason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return g_mock_reset_reason; }

#include <sys/time.h>
inline int settimeofday(const struct timeval*, const struct timezone*) { return 0; }
