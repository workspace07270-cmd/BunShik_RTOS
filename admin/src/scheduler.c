#define _POSIX_C_SOURCE 200809L

#include "scheduler.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct Scheduler {
    pthread_mutex_t mutex;
    pthread_cond_t work_available;
    pthread_cond_t idle;
    pthread_t worker;
    Order orders[SCHEDULER_MAX_ORDERS];
    size_t order_count;
    unsigned int next_id;
    unsigned long next_sequence;
    bool running;
    bool worker_started;
};

static void sleep_ms(unsigned int milliseconds)
{
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000U),
        .tv_nsec = (long)(milliseconds % 1000U) * 1000000L,
    };

    while (nanosleep(&duration, &duration) == -1 && errno == EINTR) {
    }
}

static int next_order_index(const Scheduler *scheduler)
{
    int selected = -1;

    for (size_t index = 0; index < scheduler->order_count; ++index) {
        const Order *candidate = &scheduler->orders[index];
        if (candidate->status != ORDER_RECEIVED) {
            continue;
        }

        if (selected < 0 ||
            candidate->priority > scheduler->orders[selected].priority ||
            (candidate->priority == scheduler->orders[selected].priority &&
             candidate->sequence < scheduler->orders[selected].sequence)) {
            selected = (int)index;
        }
    }

    return selected;
}

static bool is_idle(const Scheduler *scheduler)
{
    for (size_t index = 0; index < scheduler->order_count; ++index) {
        OrderStatus status = scheduler->orders[index].status;
        if (status == ORDER_RECEIVED || status == ORDER_COOKING) {
            return false;
        }
    }
    return true;
}

static void *worker_main(void *context)
{
    Scheduler *scheduler = context;

    pthread_mutex_lock(&scheduler->mutex);
    while (scheduler->running) {
        int index = next_order_index(scheduler);
        while (scheduler->running && index < 0) {
            pthread_cond_broadcast(&scheduler->idle);
            pthread_cond_wait(&scheduler->work_available, &scheduler->mutex);
            index = next_order_index(scheduler);
        }

        if (!scheduler->running) {
            break;
        }

        Order *order = &scheduler->orders[index];
        order->status = ORDER_COOKING;
        unsigned int order_id = order->id;
        unsigned int cook_time_ms = order->cook_time_ms;
        char order_number[ORDER_NUMBER_LENGTH];
        snprintf(order_number, sizeof(order_number), "%s", order->order_number);

        printf("[스케줄러] 주문 %s 조리 시작\n", order_number);
        pthread_mutex_unlock(&scheduler->mutex);
        sleep_ms(cook_time_ms);
        pthread_mutex_lock(&scheduler->mutex);

        for (size_t current = 0; current < scheduler->order_count; ++current) {
            if (scheduler->orders[current].id == order_id &&
                scheduler->orders[current].status == ORDER_COOKING) {
                scheduler->orders[current].status = ORDER_COMPLETED;
                printf("[스케줄러] 주문 %s 조리 완료\n", order_number);
                break;
            }
        }

        if (is_idle(scheduler)) {
            pthread_cond_broadcast(&scheduler->idle);
        }
    }
    pthread_cond_broadcast(&scheduler->idle);
    pthread_mutex_unlock(&scheduler->mutex);
    return NULL;
}

Scheduler *scheduler_create(void)
{
    Scheduler *scheduler = calloc(1, sizeof(*scheduler));
    if (scheduler == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&scheduler->mutex, NULL) != 0 ||
        pthread_cond_init(&scheduler->work_available, NULL) != 0 ||
        pthread_cond_init(&scheduler->idle, NULL) != 0) {
        free(scheduler);
        return NULL;
    }

    scheduler->next_id = 1;
    scheduler->next_sequence = 1;
    return scheduler;
}

void scheduler_destroy(Scheduler *scheduler)
{
    if (scheduler == NULL) {
        return;
    }
    scheduler_stop(scheduler);
    pthread_cond_destroy(&scheduler->idle);
    pthread_cond_destroy(&scheduler->work_available);
    pthread_mutex_destroy(&scheduler->mutex);
    free(scheduler);
}

bool scheduler_start(Scheduler *scheduler)
{
    if (scheduler == NULL || scheduler->worker_started) {
        return false;
    }
    scheduler->running = true;
    if (pthread_create(&scheduler->worker, NULL, worker_main, scheduler) != 0) {
        scheduler->running = false;
        return false;
    }
    scheduler->worker_started = true;
    return true;
}

void scheduler_stop(Scheduler *scheduler)
{
    if (scheduler == NULL || !scheduler->worker_started) {
        return;
    }

    pthread_mutex_lock(&scheduler->mutex);
    scheduler->running = false;
    pthread_cond_broadcast(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->mutex);

    pthread_join(scheduler->worker, NULL);
    scheduler->worker_started = false;
}

int scheduler_submit(Scheduler *scheduler, const char *order_number,
                     const char *menu, int priority,
                     unsigned int cook_time_ms)
{
    if (scheduler == NULL || order_number == NULL || menu == NULL ||
        order_number[0] == '\0' || menu[0] == '\0' || priority < 0 ||
        cook_time_ms == 0) {
        return -1;
    }

    pthread_mutex_lock(&scheduler->mutex);
    if (scheduler->order_count >= SCHEDULER_MAX_ORDERS) {
        pthread_mutex_unlock(&scheduler->mutex);
        return -1;
    }

    Order *order = &scheduler->orders[scheduler->order_count++];
    order->id = scheduler->next_id++;
    order->sequence = scheduler->next_sequence++;
    snprintf(order->order_number, sizeof(order->order_number), "%s",
             order_number);
    snprintf(order->menu, sizeof(order->menu), "%s", menu);
    order->priority = priority;
    order->cook_time_ms = cook_time_ms;
    order->status = ORDER_RECEIVED;
    int order_id = (int)order->id;

    pthread_cond_signal(&scheduler->work_available);
    pthread_mutex_unlock(&scheduler->mutex);
    return order_id;
}

bool scheduler_cancel(Scheduler *scheduler, unsigned int order_id)
{
    if (scheduler == NULL) {
        return false;
    }

    bool canceled = false;
    pthread_mutex_lock(&scheduler->mutex);
    for (size_t index = 0; index < scheduler->order_count; ++index) {
        if (scheduler->orders[index].id == order_id &&
            (scheduler->orders[index].status == ORDER_RECEIVED ||
             scheduler->orders[index].status == ORDER_COOKING)) {
            scheduler->orders[index].status = ORDER_CANCELED;
            canceled = true;
            break;
        }
    }
    if (is_idle(scheduler)) {
        pthread_cond_broadcast(&scheduler->idle);
    }
    pthread_mutex_unlock(&scheduler->mutex);
    return canceled;
}

bool scheduler_start_order(Scheduler *scheduler, unsigned int order_id)
{
    if (scheduler == NULL) return false;
    bool changed = false;
    pthread_mutex_lock(&scheduler->mutex);
    for (size_t index = 0; index < scheduler->order_count; ++index) {
        if (scheduler->orders[index].id == order_id &&
            scheduler->orders[index].status == ORDER_RECEIVED) {
            scheduler->orders[index].status = ORDER_COOKING;
            changed = true;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler->mutex);
    return changed;
}

bool scheduler_complete_order(Scheduler *scheduler, unsigned int order_id)
{
    if (scheduler == NULL) return false;
    bool changed = false;
    pthread_mutex_lock(&scheduler->mutex);
    for (size_t index = 0; index < scheduler->order_count; ++index) {
        if (scheduler->orders[index].id == order_id &&
            scheduler->orders[index].status == ORDER_COOKING) {
            scheduler->orders[index].status = ORDER_COMPLETED;
            changed = true;
            break;
        }
    }
    if (is_idle(scheduler)) pthread_cond_broadcast(&scheduler->idle);
    pthread_mutex_unlock(&scheduler->mutex);
    return changed;
}

bool scheduler_get_order(Scheduler *scheduler, unsigned int order_id,
                         Order *order)
{
    if (scheduler == NULL || order == NULL) return false;
    bool found = false;
    pthread_mutex_lock(&scheduler->mutex);
    for (size_t index = 0; index < scheduler->order_count; ++index) {
        if (scheduler->orders[index].id == order_id) {
            *order = scheduler->orders[index];
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&scheduler->mutex);
    return found;
}

size_t scheduler_snapshot(Scheduler *scheduler, Order *orders, size_t capacity)
{
    if (scheduler == NULL || orders == NULL || capacity == 0) {
        return 0;
    }

    pthread_mutex_lock(&scheduler->mutex);
    size_t count = scheduler->order_count < capacity
                       ? scheduler->order_count
                       : capacity;
    memcpy(orders, scheduler->orders, count * sizeof(*orders));
    pthread_mutex_unlock(&scheduler->mutex);
    return count;
}

bool scheduler_wait_idle(Scheduler *scheduler, unsigned int timeout_ms)
{
    if (scheduler == NULL) {
        return false;
    }

    struct timespec deadline;
    timespec_get(&deadline, TIME_UTC);
    deadline.tv_sec += (time_t)(timeout_ms / 1000U);
    deadline.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&scheduler->mutex);
    int result = 0;
    while (!is_idle(scheduler) && result != ETIMEDOUT) {
        result = pthread_cond_timedwait(&scheduler->idle, &scheduler->mutex,
                                        &deadline);
    }
    bool idle = is_idle(scheduler);
    pthread_mutex_unlock(&scheduler->mutex);
    return idle;
}
