// freertos/timers.h — FreeRTOS software timers on the simulator's virtual clock.
//
// An app's <timer> tags are FreeRTOS timers (ScriptManager). On the watch the timer task fires
// them; here delay() does, when the virtual time passes their deadline (sim_timers.cpp). Found
// before TelaCI's stub (ci/stubs/freertos/timers.h), whose timers are never created.
#pragma once

#include "freertos/FreeRTOS.h"

#include <cstddef>
#include <cstdint>

struct SimTimer;
typedef SimTimer* TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);

#define pdMS_TO_TICKS(x) (x)  // a tick is a millisecond, as Arduino-ESP32 builds FreeRTOS
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

TimerHandle_t xTimerCreate(const char* name, uint32_t period, int autoReload, void* id,
                           TimerCallbackFunction_t callback);
int xTimerStart(TimerHandle_t timer, uint32_t ticksToWait);  // from now; again: restarts
int xTimerReset(TimerHandle_t timer, uint32_t ticksToWait);
int xTimerStop(TimerHandle_t timer, uint32_t ticksToWait);
int xTimerChangePeriod(TimerHandle_t timer, uint32_t period, uint32_t ticksToWait);  // and starts
int xTimerDelete(TimerHandle_t timer, uint32_t ticksToWait);
int xTimerIsTimerActive(TimerHandle_t timer);
void* pvTimerGetTimerID(TimerHandle_t timer);

namespace SimTimers {
// Fires every timer due by `now` (virtual ms), in deadline order, a periodic one for each
// period passed: what the timer task would have fired meanwhile. sim_delay() calls it
void run(uint32_t now);
size_t count();  // timers created and not deleted
}  // namespace SimTimers
