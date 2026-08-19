#ifndef BUNSHIK_BACKEND_CLIENT_H
#define BUNSHIK_BACKEND_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#define BACKEND_TEXT_LENGTH 96
#define BACKEND_MAX_ORDERS 256

typedef struct {
    unsigned int order_id;
    char order_number[32];
    char order_type[24];
    unsigned int total_price;
    char order_status[24];
    char created_at[40];
    char payment_method[32];
} BackendOrder;

typedef struct BackendClient BackendClient;

BackendClient *backend_client_create(const char *base_url);
void backend_client_destroy(BackendClient *client);
bool backend_login(BackendClient *client, const char *username,
                   const char *password);
bool backend_fetch_orders(BackendClient *client, BackendOrder *orders,
                          size_t capacity, size_t *count);
bool backend_update_status(BackendClient *client, unsigned int order_id,
                           const char *status);
bool backend_cancel_order(BackendClient *client, unsigned int order_id);
bool backend_is_authenticated(const BackendClient *client);
const char *backend_base_url(const BackendClient *client);
const char *backend_last_error(const BackendClient *client);

#endif
