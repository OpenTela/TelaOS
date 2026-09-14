#pragma once
#include <cstdint>

typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;

#define portMAX_DELAY 0xFFFFFFFF

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (void*)0x1; }
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
