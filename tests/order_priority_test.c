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

    assert(order_urgency_at(&normal, now) == ORDER_URGENCY_NORMAL);
    assert(order_urgency_at(&delayed, now) == ORDER_URGENCY_DELAYED);
    strcpy(delayed.order_status, "조리중");
    assert(order_wait_minutes_at(&delayed, now) == -1);
    assert(order_urgency_at(&delayed, now) == ORDER_URGENCY_NORMAL);

    puts("order priority tests passed");
    return 0;
}
