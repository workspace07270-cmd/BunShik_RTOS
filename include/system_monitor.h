#ifndef BUNSHIK_SYSTEM_MONITOR_H
#define BUNSHIK_SYSTEM_MONITOR_H

#include "FreeRTOS.h"
#include "queue.h"

int system_monitor_start(QueueHandle_t print_queue);

#endif
