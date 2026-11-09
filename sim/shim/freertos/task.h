// freertos/task.h — the loop task's least free stack, from the firmware's stack in the simulator
// (sim_stack.h). Found before TelaCI's stub (ci/stubs/freertos/task.h), which says 0.
#pragma once

#include "freertos/FreeRTOS.h"

typedef unsigned int UBaseType_t;

// The least stack the task has had free since it started, in bytes, as ESP-IDF counts it. Only the
// loop task is here: whatever task is asked about, the answer is its
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
