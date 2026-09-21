#ifndef INC_TASK_H
#define INC_TASK_H
#include "FreeRTOS.h"

extern volatile uint32_t g_bare_tick;

#define xTaskGetTickCount() (g_bare_tick)

#endif
