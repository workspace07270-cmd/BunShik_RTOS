#define _POSIX_C_SOURCE 200809L

#include "order_priority.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static BackendOrder received_at(time_t timestamp)
{
    BackendOrder order = {0};
    order.order_id = 1;
    strcpy(order.order_status, "접수");
    struct tm local_time;
    localtime_r(&timestamp, &local_time);
    strftime(order.created_at, sizeof(order.created_at),
             "%Y-%m-%dT%H:%M:%S", &local_time);
    return order;
}

int main(void)
{
    time_t now = time(NULL);
    BackendOrder normal = received_at(now - 9 * 60);
    BackendOrder delayed = received_at(now - 10 * 60);
    BackendOrder urgent = received_at(now - 20 * 60);

    assert(order_urgency_at(&normal, now) == ORDER_URGENCY_NORMAL);
    assert(order_urgency_at(&delayed, now) == ORDER_URGENCY_DELAYED);
    assert(order_urgency_at(&urgent, now) == ORDER_URGENCY_URGENT);
    assert(order_wait_minutes_at(&urgent, now) == 20);
    assert(order_should_precede_at(&urgent, &delayed, now));
    assert(!order_should_precede_at(&normal, &urgent, now));

    BackendOrder older_normal = received_at(now - 8 * 60);
    BackendOrder newer_normal = received_at(now - 3 * 60);
    older_normal.order_id = 10;
    newer_normal.order_id = 11;
    assert(order_should_precede_at(&older_normal, &newer_normal, now));

    strcpy(urgent.order_status, "조리중");
    assert(order_wait_minutes_at(&urgent, now) == -1);
    assert(order_urgency_at(&urgent, now) == ORDER_URGENCY_NORMAL);
    assert(!order_should_precede_at(&urgent, &normal, now));

    puts("order priority tests passed");
    return 0;
}
