#include "system_monitor.h"

#include "printer_paper.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>

#define SYSTEM_MONITOR_INTERVAL_MS 60000
#define SYSTEM_MONITOR_STACK_DEPTH 2048

static QueueHandle_t monitored_print_queue;

static void system_monitor_task(void *parameter)
{
    (void)parameter;

    for (;;) {
        size_t free_heap = xPortGetFreeHeapSize();
        size_t minimum_heap = xPortGetMinimumEverFreeHeapSize();
        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        UBaseType_t queued_jobs = uxQueueMessagesWaiting(monitored_print_queue);

        printf("[SystemMonitorTask] Task=%lu Heap=%zuKB 최소=%zuKB "
               "출력Queue=%lu/1 용지=%d매\n",
               (unsigned long)task_count, free_heap / 1024U,
               minimum_heap / 1024U, (unsigned long)queued_jobs,
               printer_paper_remaining());
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_MONITOR_INTERVAL_MS));
    }
}

int system_monitor_start(QueueHandle_t print_queue)
{
    if (print_queue == NULL) return EXIT_FAILURE;
    monitored_print_queue = print_queue;
    return xTaskCreate(system_monitor_task, "SystemMonitorTask",
                       SYSTEM_MONITOR_STACK_DEPTH, NULL, 1, NULL) == pdPASS
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
