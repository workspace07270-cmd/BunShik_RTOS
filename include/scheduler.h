#ifndef BUNSHIK_SCHEDULER_H
#define BUNSHIK_SCHEDULER_H

#include "order.h"
#include <stdbool.h>
#include <stddef.h>

#define SCHEDULER_MAX_ORDERS 128

typedef struct Scheduler Scheduler;

Scheduler *scheduler_create(void);
void scheduler_destroy(Scheduler *scheduler);

bool scheduler_start(Scheduler *scheduler);
void scheduler_stop(Scheduler *scheduler);

int scheduler_submit(Scheduler *scheduler, const char *order_number,
                     const char *menu, int priority,
                     unsigned int cook_time_ms);
bool scheduler_cancel(Scheduler *scheduler, unsigned int order_id);
bool scheduler_start_order(Scheduler *scheduler, unsigned int order_id);
bool scheduler_complete_order(Scheduler *scheduler, unsigned int order_id);
bool scheduler_get_order(Scheduler *scheduler, unsigned int order_id,
                         Order *order);
size_t scheduler_snapshot(Scheduler *scheduler, Order *orders,
                          size_t capacity);
bool scheduler_wait_idle(Scheduler *scheduler, unsigned int timeout_ms);

#endif
