// freertos/task.h — what TelaOS takes from FreeRTOS tasks: the least free stack of the loop task
#pragma once

#include "freertos/FreeRTOS.h"

typedef unsigned int UBaseType_t;

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 0; }
