#define _XOPEN_SOURCE 700

#include "order_priority.h"

#include <string.h>

#define DELAY_MINUTES 10L
#define URGENT_MINUTES 20L

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
    if (minutes >= URGENT_MINUTES) return ORDER_URGENCY_URGENT;
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
    case ORDER_URGENCY_URGENT: return "긴급";
    case ORDER_URGENCY_DELAYED: return "지연";
    default: return "정상";
    }
}

bool order_should_precede_at(const BackendOrder *candidate,
                             const BackendOrder *current, time_t now)
{
    if (candidate == NULL || strcmp(candidate->order_status, "접수") != 0)
        return false;
    if (current == NULL || strcmp(current->order_status, "접수") != 0)
        return true;
    OrderUrgency candidate_urgency = order_urgency_at(candidate, now);
    OrderUrgency current_urgency = order_urgency_at(current, now);
    if (candidate_urgency != current_urgency)
        return candidate_urgency > current_urgency;
    long candidate_wait = order_wait_minutes_at(candidate, now);
    long current_wait = order_wait_minutes_at(current, now);
    if (candidate_wait != current_wait) return candidate_wait > current_wait;
    return candidate->order_id < current->order_id;
}

bool order_should_precede(const BackendOrder *candidate,
                          const BackendOrder *current)
{
    return order_should_precede_at(candidate, current, time(NULL));
}
