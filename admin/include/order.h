#ifndef BUNSHIK_ORDER_H
#define BUNSHIK_ORDER_H

#include <stddef.h>

#define ORDER_NUMBER_LENGTH 32
#define ORDER_MENU_LENGTH 96

typedef enum {
    ORDER_RECEIVED,
    ORDER_COOKING,
    ORDER_COMPLETED,
    ORDER_CANCELED
} OrderStatus;

typedef struct {
    unsigned int id;
    unsigned long sequence;
    char order_number[ORDER_NUMBER_LENGTH];
    char menu[ORDER_MENU_LENGTH];
    int priority;
    unsigned int cook_time_ms;
    OrderStatus status;
} Order;

const char *order_status_name(OrderStatus status);

#endif
