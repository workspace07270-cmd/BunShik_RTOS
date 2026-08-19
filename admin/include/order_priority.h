#ifndef BUNSHIK_ORDER_PRIORITY_H
#define BUNSHIK_ORDER_PRIORITY_H

#include "backend_client.h"

#include <time.h>

typedef enum {
    ORDER_URGENCY_NORMAL = 0,
    ORDER_URGENCY_DELAYED = 1,
    ORDER_URGENCY_URGENT = 2
} OrderUrgency;

long order_wait_minutes_at(const BackendOrder *order, time_t now);
long order_wait_minutes(const BackendOrder *order);
OrderUrgency order_urgency_at(const BackendOrder *order, time_t now);
OrderUrgency order_urgency(const BackendOrder *order);
const char *order_urgency_name(OrderUrgency urgency);
bool order_should_precede_at(const BackendOrder *candidate,
                             const BackendOrder *current, time_t now);
bool order_should_precede(const BackendOrder *candidate,
                          const BackendOrder *current);

#endif
