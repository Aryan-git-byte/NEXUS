#pragma once
#include <stdint.h>
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(value) (value)
