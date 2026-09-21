#ifndef FREERTOS_H
#define FREERTOS_H
#include "stm32f10x.h"
#include <stdint.h>

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(xTimeInMs) ((TickType_t)(xTimeInMs))
#define pdTRUE  (1)
#define pdFALSE (0)
typedef int32_t BaseType_t;

#endif
