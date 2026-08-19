#ifndef BUNSHIK_RTOS_RUNTIME_H
#define BUNSHIK_RTOS_RUNTIME_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <time.h>

typedef SemaphoreHandle_t RtosMutex;
typedef TaskHandle_t RtosTask;

typedef struct {
    TaskHandle_t waiter;
} RtosEvent;

typedef void *(*RtosTaskFunction)(void *);

int rtos_mutex_init(RtosMutex *mutex);
void rtos_mutex_destroy(RtosMutex *mutex);
void rtos_mutex_lock(RtosMutex *mutex);
void rtos_mutex_unlock(RtosMutex *mutex);

void rtos_event_init(RtosEvent *event);
void rtos_event_destroy(RtosEvent *event);
void rtos_event_wait(RtosEvent *event, RtosMutex *mutex);
void rtos_event_wait_until(RtosEvent *event, RtosMutex *mutex,
        const struct timespec *deadline);
void rtos_event_signal(RtosEvent *event);

int rtos_task_create(RtosTask *task, const char *name,
        RtosTaskFunction function, void *argument, UBaseType_t priority,
        configSTACK_DEPTH_TYPE stack_depth);
void rtos_task_stop(RtosTask *task);

int rtos_run(RtosTaskFunction entry, void *argument, const char *name,
        configSTACK_DEPTH_TYPE stack_depth);

#endif
