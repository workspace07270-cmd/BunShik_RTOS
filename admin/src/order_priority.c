#define _XOPEN_SOURCE 700

#include "order_priority.h"

#include <string.h>

#define DELAY_MINUTES 10L

long order_wait_minutes_at(const BackendOrder *order, time_t now)
{
    if (order == NULL || strcmp(order->order_status, "접수") != 0 ||
        order->created_at[0] == '\0') return -1;

    struct tm created = {0};
    if (strptime(order->created_at, "%Y-%m-%dT%H:%M:%S", &created) == NULL &&
        strptime(order->created_at, "%Y-%m-%d %H:%M:%S", &created) == NULL)
        return -1;
    created.tm_isdst = -1;
    time_t created_time = mktime(&created);
    if (created_time == (time_t)-1 || now < created_time) return 0;
    return (long)(difftime(now, created_time) / 60.0);
}

long order_wait_minutes(const BackendOrder *order)
{
    return order_wait_minutes_at(order, time(NULL));
}

OrderUrgency order_urgency_at(const BackendOrder *order, time_t now)
{
    long minutes = order_wait_minutes_at(order, now);
    if (minutes >= DELAY_MINUTES) return ORDER_URGENCY_DELAYED;
    return ORDER_URGENCY_NORMAL;
}

OrderUrgency order_urgency(const BackendOrder *order)
{
    return order_urgency_at(order, time(NULL));
}

const char *order_urgency_name(OrderUrgency urgency)
{
    switch (urgency) {
    case ORDER_URGENCY_DELAYED: return "지연";
    default: return "정상";
    }
}
