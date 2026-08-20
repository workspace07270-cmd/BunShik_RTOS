#ifndef BUNSHIK_BACKEND_CLIENT_H
#define BUNSHIK_BACKEND_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#define BACKEND_TEXT_LENGTH 96
#define BACKEND_MAX_ORDERS 256
#define BACKEND_MAX_ITEMS 64
#define BACKEND_MAX_OPTIONS 32
#define BACKEND_MAX_COMPONENTS 32
#define BACKEND_MAX_CATALOG_ITEMS 256

typedef struct {
    unsigned int order_id;
    char order_number[32];
    char order_type[24];
    unsigned int total_price;
    char order_status[24];
    char created_at[40];
    char payment_method[32];
} BackendOrder;

typedef struct {
    unsigned int option_id;
    char option_name[BACKEND_TEXT_LENGTH];
    unsigned int option_price;
} BackendOrderOption;

typedef struct {
    unsigned int component_menu_id;
    char component_menu_name[BACKEND_TEXT_LENGTH];
} BackendOrderComponent;

typedef struct {
    unsigned int order_item_id;
    char menu_name[BACKEND_TEXT_LENGTH];
    unsigned int quantity;
    unsigned int unit_price;
    BackendOrderOption options[BACKEND_MAX_OPTIONS];
    size_t option_count;
    BackendOrderComponent components[BACKEND_MAX_COMPONENTS];
    size_t component_count;
} BackendOrderItem;

typedef struct {
    BackendOrder order;
    BackendOrderItem items[BACKEND_MAX_ITEMS];
    size_t item_count;
} BackendOrderDetail;

typedef struct {
    unsigned int id;
    char name[BACKEND_TEXT_LENGTH];
    unsigned int price;
    bool available;
    bool visible;
} BackendCatalogItem;

typedef struct BackendClient BackendClient;

typedef enum {
    BACKEND_ERROR_NONE,
    BACKEND_ERROR_NETWORK,
    BACKEND_ERROR_AUTH,
    BACKEND_ERROR_HTTP,
    BACKEND_ERROR_PARSE
} BackendErrorKind;

BackendClient *backend_client_create(const char *base_url);
void backend_client_destroy(BackendClient *client);
bool backend_login(BackendClient *client, const char *username,
                   const char *password);
bool backend_fetch_orders(BackendClient *client, BackendOrder *orders,
                          size_t capacity, size_t *count);
bool backend_fetch_menus(BackendClient *client, BackendCatalogItem *items,
                         size_t capacity, size_t *count);
bool backend_fetch_options(BackendClient *client, BackendCatalogItem *items,
                           size_t capacity, size_t *count);
bool backend_fetch_order_detail(BackendClient *client, unsigned int order_id,
                                BackendOrderDetail *detail);
bool backend_update_status(BackendClient *client, unsigned int order_id,
                           const char *status);
bool backend_cancel_order(BackendClient *client, unsigned int order_id);
bool backend_update_bulk_status(BackendClient *client,
                                const unsigned int *order_ids, size_t count,
                                const char *status);
bool backend_cancel_bulk_orders(BackendClient *client,
                                const unsigned int *order_ids, size_t count);
bool backend_is_authenticated(const BackendClient *client);
void backend_logout(BackendClient *client);
const char *backend_base_url(const BackendClient *client);
const char *backend_last_error(const BackendClient *client);
BackendErrorKind backend_error_kind(const BackendClient *client);
int backend_last_http_status(const BackendClient *client);

/* 백엔드 JSON 응답 파싱용. 네트워크 없이 단위 테스트할 수 있다. */
bool backend_parse_orders_json(const char *json, BackendOrder *orders,
                               size_t capacity, size_t *count,
                               char *error, size_t error_size);
bool backend_parse_order_detail_json(const char *json,
                                     BackendOrderDetail *detail,
                                     char *error, size_t error_size);
bool backend_parse_menus_json(const char *json, BackendCatalogItem *items,
                              size_t capacity, size_t *count,
                              char *error, size_t error_size);
bool backend_parse_options_json(const char *json, BackendCatalogItem *items,
                                size_t capacity, size_t *count,
                                char *error, size_t error_size);

#endif
