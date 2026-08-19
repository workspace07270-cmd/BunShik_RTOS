#define _POSIX_C_SOURCE 200809L

#include "rtos_runtime.h"

#include <stdio.h>
#include <stdlib.h>

void vApplicationMallocFailedHook(void) { abort(); }

typedef struct {
    RtosTaskFunction function;
    void *argument;
} TaskStart;

static void task_trampoline(void *parameter)
{
    TaskStart start = *(TaskStart *)parameter;
    vPortFree(parameter);
    (void)start.function(start.argument);
    vTaskDelete(NULL);
}

int rtos_mutex_init(RtosMutex *mutex)
{
    *mutex = xSemaphoreCreateMutex();
    return *mutex == NULL ? -1 : 0;
}

void rtos_mutex_destroy(RtosMutex *mutex)
{
    if (*mutex != NULL) vSemaphoreDelete(*mutex);
    *mutex = NULL;
}

void rtos_mutex_lock(RtosMutex *mutex)
{
    configASSERT(xSemaphoreTake(*mutex, portMAX_DELAY) == pdTRUE);
}

void rtos_mutex_unlock(RtosMutex *mutex)
{
    configASSERT(xSemaphoreGive(*mutex) == pdTRUE);
}

void rtos_event_init(RtosEvent *event) { event->waiter = NULL; }
void rtos_event_destroy(RtosEvent *event) { event->waiter = NULL; }

static void wait_ticks(RtosEvent *event, RtosMutex *mutex, TickType_t ticks)
{
    event->waiter = xTaskGetCurrentTaskHandle();
    rtos_mutex_unlock(mutex);
    (void)ulTaskNotifyTake(pdTRUE, ticks);
    rtos_mutex_lock(mutex);
    event->waiter = NULL;
}

void rtos_event_wait(RtosEvent *event, RtosMutex *mutex)
{
    wait_ticks(event, mutex, portMAX_DELAY);
}

void rtos_event_wait_until(RtosEvent *event, RtosMutex *mutex,
        const struct timespec *deadline)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    long long milliseconds = (long long)(deadline->tv_sec - now.tv_sec) * 1000
            + (deadline->tv_nsec - now.tv_nsec) / 1000000;
    if (milliseconds < 0) milliseconds = 0;
    wait_ticks(event, mutex, pdMS_TO_TICKS((unsigned long)milliseconds));
}

void rtos_event_signal(RtosEvent *event)
{
    if (event->waiter != NULL) xTaskNotifyGive(event->waiter);
}

int rtos_task_create(RtosTask *task, const char *name,
        RtosTaskFunction function, void *argument, UBaseType_t priority,
        configSTACK_DEPTH_TYPE stack_depth)
{
    TaskStart *start = pvPortMalloc(sizeof(*start));
    if (start == NULL) return -1;
    start->function = function;
    start->argument = argument;
    if (xTaskCreate(task_trampoline, name, stack_depth, start, priority,
            task) != pdPASS) {
        vPortFree(start);
        return -1;
    }
    return 0;
}

void rtos_task_stop(RtosTask *task)
{
    if (*task != NULL) vTaskDelete(*task);
    *task = NULL;
}

typedef struct {
    RtosTaskFunction entry;
    void *argument;
    int result;
} RuntimeStart;

static void runtime_entry(void *parameter)
{
    RuntimeStart *start = parameter;
    start->result = (int)(long)start->entry(start->argument);
    /* POSIX 포트의 실행 프로세스 자체가 RTOS 인스턴스이므로 CLI 종료 시
       호스트 프로세스를 끝낸다. 커널 태스크와 힙은 운영체제가 회수한다. */
    fflush(NULL);
    exit(start->result);
}

int rtos_run(RtosTaskFunction entry, void *argument, const char *name,
        configSTACK_DEPTH_TYPE stack_depth)
{
    RuntimeStart start = {.entry = entry, .argument = argument, .result = -1};
    if (xTaskCreate(runtime_entry, name, stack_depth, &start, 2, NULL) != pdPASS)
        return -1;
    vTaskStartScheduler();
    return start.result;
}
