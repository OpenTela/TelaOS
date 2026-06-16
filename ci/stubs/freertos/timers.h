#pragma once
#include "FreeRTOS.h"
typedef void* TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
inline TimerHandle_t xTimerCreate(const char*, uint32_t, int, void*, TimerCallbackFunction_t) { return nullptr; }
inline int xTimerStart(TimerHandle_t, int) { return pdPASS; }
inline int xTimerStop(TimerHandle_t, int) { return pdPASS; }
inline int xTimerDelete(TimerHandle_t, int) { return pdPASS; }
inline void* pvTimerGetTimerID(TimerHandle_t) { return nullptr; }
