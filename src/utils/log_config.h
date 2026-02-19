#pragma once

#include <cstdint>

namespace Log {

enum Cat : uint8_t { 
    UI = 0, 
    LUA, 
    STATE, 
    APP, 
    BLE, 
    COUNT 
};

enum Level : uint8_t { 
    Disabled = 0, 
    Error, 
    Warn, 
    Info, 
    Debug, 
    Verbose 
};

void set(Cat cat, Level lvl);
void setAll(Level lvl);
Level get(Cat cat);

const char* catName(Cat cat);
const char* levelName(Level lvl);

// Команда: "log", "log verbose", "log ui debug"
void command(const char* args);

} // namespace Log

// ============================================================
// OS_LOG* — собственные макросы вывода, без ESP_LOG зависимости.
// Всегда работают, фильтрация только runtime через Log::get().
// Формат: [timestamp][LEVEL][tag] message
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif
#include "esp_timer.h"
int log_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#define OS_LOG(letter, tag, fmt, ...) \
    log_printf("[%6u][" letter "][%s] " fmt "\r\n", \
        (unsigned)(esp_timer_get_time() / 1000ULL), tag, ##__VA_ARGS__)

#define OS_LOGE(tag, fmt, ...) OS_LOG("E", tag, fmt, ##__VA_ARGS__)
#define OS_LOGW(tag, fmt, ...) OS_LOG("W", tag, fmt, ##__VA_ARGS__)
#define OS_LOGI(tag, fmt, ...) OS_LOG("I", tag, fmt, ##__VA_ARGS__)
#define OS_LOGD(tag, fmt, ...) OS_LOG("D", tag, fmt, ##__VA_ARGS__)
#define OS_LOGV(tag, fmt, ...) OS_LOG("V", tag, fmt, ##__VA_ARGS__)

// ============================================================
// LOG_* — категорийные макросы с runtime-фильтрацией.
// Использование: LOG_I(Log::UI, "loaded %d widgets", count);
// ============================================================

#define LOG_E(cat, fmt, ...) do { if (Log::get(cat) >= Log::Error)   OS_LOGE(TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_W(cat, fmt, ...) do { if (Log::get(cat) >= Log::Warn)    OS_LOGW(TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_I(cat, fmt, ...) do { if (Log::get(cat) >= Log::Info)    OS_LOGI(TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_D(cat, fmt, ...) do { if (Log::get(cat) >= Log::Debug)   OS_LOGD(TAG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_V(cat, fmt, ...) do { if (Log::get(cat) >= Log::Verbose) OS_LOGV(TAG, fmt, ##__VA_ARGS__); } while(0)
