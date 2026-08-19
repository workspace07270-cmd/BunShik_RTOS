#include "backend_client.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BackendClient {
    char base_url[256];
    char token[2048];
    char error[256];
    BackendErrorKind error_kind;
    int http_status;
};

static void set_error(BackendClient *client, const char *message)
{
    snprintf(client->error, sizeof(client->error), "%s", message);
}

static void clear_error(BackendClient *client)
{
    client->error[0] = '\0';
    client->error_kind = BACKEND_ERROR_NONE;
    client->http_status = 0;
}

static bool request_failed(BackendClient *client)
{
    client->error_kind = BACKEND_ERROR_NETWORK;
    client->http_status = 0;
    return false;
}

static bool json_string(const char *json, const char *key, char *output,
                        size_t output_size)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *position = strstr(json, pattern);
    if (position == NULL) return false;
    position = strchr(position + strlen(pattern), ':');
    if (position == NULL) return false;
    while (*++position == ' ' || *position == '\t') {}
    if (*position != '"') return false;
    ++position;
    size_t used = 0;
    while (*position && *position != '"') {
        char value = *position++;
        if (value == '\\' && *position) value = *position++;
        if (used + 1 < output_size) output[used++] = value;
    }
    output[used] = '\0';
    return *position == '"';
}

static bool json_unsigned(const char *json, const char *key,
                          unsigned int *output)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *position = strstr(json, pattern);
    if (position == NULL) return false;
    position = strchr(position + strlen(pattern), ':');
    if (position == NULL) return false;
    char *end = NULL;
    unsigned long value = strtoul(position + 1, &end, 10);
    if (end == position + 1) return false;
    *output = (unsigned int)value;
    return true;
}

static const char *json_array(const char *json, const char *key)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *position = strstr(json, pattern);
    return position == NULL ? NULL : strchr(position + strlen(pattern), '[');
}

static const char *json_object_end(const char *start)
{
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char *cursor = start; *cursor; ++cursor) {
        if (escaped) { escaped = false; continue; }
        if (in_string && *cursor == '\\') { escaped = true; continue; }
        if (*cursor == '"') { in_string = !in_string; continue; }
        if (!in_string && *cursor == '{') ++depth;
        else if (!in_string && *cursor == '}' && --depth == 0) return cursor;
    }
    return NULL;
}

static char *copy_json_object(const char *start, const char *end)
{
    size_t length = (size_t)(end - start + 1);
    char *object = malloc(length + 1U);
    if (object == NULL) return NULL;
    memcpy(object, start, length);
    object[length] = '\0';
    return object;
}

static void response_error(BackendClient *client, const HttpResponse *response)
{
    client->http_status = response->status_code;
    client->error_kind = response->status_code == 401 || response->status_code == 403
                             ? BACKEND_ERROR_AUTH
                             : BACKEND_ERROR_HTTP;
    char message[192];
    if (response->body && json_string(response->body, "message", message,
                                      sizeof(message))) {
        set_error(client, message);
    } else {
        snprintf(client->error, sizeof(client->error), "백엔드 HTTP 오류: %d",
                 response->status_code);
    }
}

BackendClient *backend_client_create(const char *base_url)
{
    BackendClient *client = calloc(1, sizeof(*client));
    if (client == NULL) return NULL;
    snprintf(client->base_url, sizeof(client->base_url), "%s",
             base_url ? base_url : "http://127.0.0.1:8080");
    return client;
}

void backend_client_destroy(BackendClient *client) { free(client); }

bool backend_login(BackendClient *client, const char *username,
                   const char *password)
{
    if (client == NULL || username == NULL || password == NULL) return false;
    char body[512];
    if (strchr(username, '"') || strchr(password, '"')) {
        set_error(client, "아이디와 비밀번호에 큰따옴표를 사용할 수 없습니다.");
        return false;
    }
    snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}",
             username, password);
    HttpResponse response;
    if (http_request(client->base_url, "POST", "/api/admin/login", NULL,
                     body, &response, client->error, sizeof(client->error)) != 0)
        return request_failed(client);
    bool success = response.status_code >= 200 && response.status_code < 300 &&
                   json_string(response.body, "accessToken", client->token,
                               sizeof(client->token));
    if (!success) response_error(client, &response);
    else clear_error(client);
    http_response_free(&response);
    return success;
}

bool backend_fetch_orders(BackendClient *client, BackendOrder *orders,
                          size_t capacity, size_t *count)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        client->error_kind = BACKEND_ERROR_AUTH;
        return false;
    }
    HttpResponse response;
    if (http_request(client->base_url, "GET", "/api/admin/orders",
                     client->token, NULL, &response, client->error,
                     sizeof(client->error)) != 0) return request_failed(client);
    if (response.status_code < 200 || response.status_code >= 300) {
        response_error(client, &response);
        http_response_free(&response);
        return false;
    }

    bool parsed = backend_parse_orders_json(response.body, orders, capacity,
                                            count, client->error,
                                            sizeof(client->error));
    http_response_free(&response);
    if (parsed) clear_error(client);
    else client->error_kind = BACKEND_ERROR_PARSE;
    return parsed;
}

bool backend_parse_orders_json(const char *json, BackendOrder *orders,
                               size_t capacity, size_t *count,
                               char *error, size_t error_size)
{
    if (json == NULL || orders == NULL || count == NULL || capacity == 0) {
        snprintf(error, error_size, "주문 JSON 파싱 인자가 잘못됐습니다.");
        return false;
    }
    *count = 0;
    const char *cursor = strstr(json, "\"data\"");
    cursor = cursor ? strchr(cursor, '[') : NULL;
    if (cursor == NULL) {
        snprintf(error, error_size, "주문 응답의 data 배열을 찾지 못했습니다.");
        return false;
    }
    ++cursor;
    while (*cursor && *cursor != ']') {
        if (*count >= capacity) {
            snprintf(error, error_size,
                     "주문이 저장 한도 %zu건을 초과했습니다.", capacity);
            return false;
        }
        const char *start = strchr(cursor, '{');
        const char *array_end = strchr(cursor, ']');
        if (start == NULL || (array_end && start > array_end)) break;
        const char *end = json_object_end(start);
        if (end == NULL) {
            snprintf(error, error_size, "주문 JSON 객체가 끝나지 않았습니다.");
            return false;
        }
        char *object = copy_json_object(start, end);
        if (object == NULL) {
            snprintf(error, error_size, "주문 JSON 메모리 할당 실패");
            return false;
        }
        BackendOrder *order = &orders[(*count)++];
        memset(order, 0, sizeof(*order));
        bool valid = json_unsigned(object, "orderId", &order->order_id) &&
                     json_unsigned(object, "totalPrice", &order->total_price) &&
                     json_string(object, "orderNumber", order->order_number, sizeof(order->order_number)) &&
                     json_string(object, "orderType", order->order_type, sizeof(order->order_type)) &&
                     json_string(object, "orderStatus", order->order_status, sizeof(order->order_status));
        json_string(object, "createdAt", order->created_at, sizeof(order->created_at));
        json_string(object, "paymentMethod", order->payment_method, sizeof(order->payment_method));
        free(object);
        if (!valid || order->order_id == 0) {
            snprintf(error, error_size, "필수 주문 필드가 누락됐습니다.");
            return false;
        }
        cursor = end + 1;
    }
    return true;
}

static bool parse_options(const char *item_json, BackendOrderItem *item,
                          char *error, size_t error_size)
{
    const char *cursor = json_array(item_json, "options");
    if (cursor == NULL) return true;
    ++cursor;
    while (*cursor && *cursor != ']') {
        if (item->option_count >= BACKEND_MAX_OPTIONS) {
            snprintf(error, error_size, "주문 항목의 옵션이 %d개를 초과했습니다.",
                     BACKEND_MAX_OPTIONS);
            return false;
        }
        const char *start = strchr(cursor, '{');
        const char *array_end = strchr(cursor, ']');
        if (start == NULL || (array_end && start > array_end)) break;
        const char *end = json_object_end(start);
        if (end == NULL) return false;
        char *object = copy_json_object(start, end);
        if (object == NULL) return false;
        BackendOrderOption *option = &item->options[item->option_count++];
        bool valid = json_unsigned(object, "optionId", &option->option_id) &&
                     json_string(object, "optionName", option->option_name,
                                 sizeof(option->option_name)) &&
                     json_unsigned(object, "optionPrice", &option->option_price);
        free(object);
        if (!valid) {
            snprintf(error, error_size, "주문 옵션 필드가 누락됐습니다.");
            return false;
        }
        cursor = end + 1;
    }
    return true;
}

static bool parse_components(const char *item_json, BackendOrderItem *item,
                             char *error, size_t error_size)
{
    const char *cursor = json_array(item_json, "components");
    if (cursor == NULL) return true;
    ++cursor;
    while (*cursor && *cursor != ']') {
        if (item->component_count >= BACKEND_MAX_COMPONENTS) {
            snprintf(error, error_size, "세트 구성이 %d개를 초과했습니다.",
                     BACKEND_MAX_COMPONENTS);
            return false;
        }
        const char *start = strchr(cursor, '{');
        const char *array_end = strchr(cursor, ']');
        if (start == NULL || (array_end && start > array_end)) break;
        const char *end = json_object_end(start);
        if (end == NULL) return false;
        char *object = copy_json_object(start, end);
        if (object == NULL) return false;
        BackendOrderComponent *component =
            &item->components[item->component_count++];
        bool valid = json_unsigned(object, "componentMenuId",
                                   &component->component_menu_id) &&
                     json_string(object, "componentMenuName",
                                 component->component_menu_name,
                                 sizeof(component->component_menu_name));
        free(object);
        if (!valid) {
            snprintf(error, error_size, "세트 구성 필드가 누락됐습니다.");
            return false;
        }
        cursor = end + 1;
    }
    return true;
}

bool backend_parse_order_detail_json(const char *json,
                                     BackendOrderDetail *detail,
                                     char *error, size_t error_size)
{
    if (json == NULL || detail == NULL) return false;
    if (error != NULL && error_size > 0) error[0] = '\0';
    memset(detail, 0, sizeof(*detail));
    const char *data = strstr(json, "\"data\"");
    data = data ? strchr(data, '{') : NULL;
    if (data == NULL) {
        snprintf(error, error_size, "상세 응답의 data 객체를 찾지 못했습니다.");
        return false;
    }
    BackendOrder *order = &detail->order;
    bool valid = json_unsigned(data, "orderId", &order->order_id) &&
                 json_unsigned(data, "totalPrice", &order->total_price) &&
                 json_string(data, "orderNumber", order->order_number,
                             sizeof(order->order_number)) &&
                 json_string(data, "orderType", order->order_type,
                             sizeof(order->order_type)) &&
                 json_string(data, "orderStatus", order->order_status,
                             sizeof(order->order_status));
    json_string(data, "createdAt", order->created_at, sizeof(order->created_at));
    json_string(data, "paymentMethod", order->payment_method,
                sizeof(order->payment_method));
    if (!valid) {
        snprintf(error, error_size, "상세 주문의 필수 필드가 누락됐습니다.");
        return false;
    }

    const char *cursor = json_array(data, "items");
    if (cursor == NULL) {
        snprintf(error, error_size, "상세 주문의 items 배열이 없습니다.");
        return false;
    }
    ++cursor;
    while (*cursor && *cursor != ']') {
        if (detail->item_count >= BACKEND_MAX_ITEMS) {
            snprintf(error, error_size, "주문 메뉴가 %d개를 초과했습니다.",
                     BACKEND_MAX_ITEMS);
            return false;
        }
        const char *start = strchr(cursor, '{');
        const char *array_end = strchr(cursor, ']');
        if (start == NULL || (array_end && start > array_end)) break;
        const char *end = json_object_end(start);
        if (end == NULL) return false;
        char *object = copy_json_object(start, end);
        if (object == NULL) return false;
        BackendOrderItem *item = &detail->items[detail->item_count++];
        bool item_valid =
            json_unsigned(object, "orderItemId", &item->order_item_id) &&
            json_string(object, "menuName", item->menu_name,
                        sizeof(item->menu_name)) &&
            json_unsigned(object, "quantity", &item->quantity) &&
            json_unsigned(object, "unitPrice", &item->unit_price) &&
            parse_options(object, item, error, error_size) &&
            parse_components(object, item, error, error_size);
        free(object);
        if (!item_valid) {
            if (error[0] == '\0')
                snprintf(error, error_size, "주문 메뉴 필드가 누락됐습니다.");
            return false;
        }
        cursor = end + 1;
    }
    return true;
}

bool backend_fetch_order_detail(BackendClient *client, unsigned int order_id,
                                BackendOrderDetail *detail)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        client->error_kind = BACKEND_ERROR_AUTH;
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/api/admin/orders/%u/detail", order_id);
    HttpResponse response;
    if (http_request(client->base_url, "GET", path, client->token, NULL,
                     &response, client->error, sizeof(client->error)) != 0)
        return request_failed(client);
    if (response.status_code < 200 || response.status_code >= 300) {
        response_error(client, &response);
        http_response_free(&response);
        return false;
    }
    bool parsed = backend_parse_order_detail_json(
        response.body, detail, client->error, sizeof(client->error));
    http_response_free(&response);
    if (parsed) clear_error(client);
    else client->error_kind = BACKEND_ERROR_PARSE;
    return parsed;
}

bool backend_update_status(BackendClient *client, unsigned int order_id,
                           const char *status)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        client->error_kind = BACKEND_ERROR_AUTH;
        return false;
    }
    char path[96], body[96];
    snprintf(path, sizeof(path), "/api/admin/orders/%u/status", order_id);
    snprintf(body, sizeof(body), "{\"orderStatus\":\"%s\"}", status);
    HttpResponse response;
    if (http_request(client->base_url, "PATCH", path, client->token, body,
                     &response, client->error, sizeof(client->error)) != 0)
        return request_failed(client);
    bool success = response.status_code >= 200 && response.status_code < 300;
    if (!success) response_error(client, &response);
    else clear_error(client);
    http_response_free(&response);
    return success;
}

bool backend_cancel_order(BackendClient *client, unsigned int order_id)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        client->error_kind = BACKEND_ERROR_AUTH;
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/api/admin/orders/%u/cancel", order_id);
    HttpResponse response;
    if (http_request(client->base_url, "PATCH", path, client->token, NULL,
                     &response, client->error, sizeof(client->error)) != 0)
        return request_failed(client);
    bool success = response.status_code >= 200 && response.status_code < 300;
    if (!success) response_error(client, &response);
    else clear_error(client);
    http_response_free(&response);
    return success;
}

static bool build_order_ids_json(const unsigned int *order_ids, size_t count,
                                 const char *status, char *body,
                                 size_t body_size)
{
    if (order_ids == NULL || count == 0 || body == NULL || body_size == 0)
        return false;
    size_t used = (size_t)snprintf(body, body_size, "{\"orderIds\":[");
    if (used >= body_size) return false;
    for (size_t index = 0; index < count; ++index) {
        int written = snprintf(body + used, body_size - used, "%s%u",
                               index == 0 ? "" : ",", order_ids[index]);
        if (written < 0 || (size_t)written >= body_size - used) return false;
        used += (size_t)written;
    }
    int written = status == NULL
        ? snprintf(body + used, body_size - used, "]}")
        : snprintf(body + used, body_size - used,
                   "],\"orderStatus\":\"%s\"}", status);
    return written >= 0 && (size_t)written < body_size - used;
}

bool backend_update_bulk_status(BackendClient *client,
                                const unsigned int *order_ids, size_t count,
                                const char *status)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        client->error_kind = BACKEND_ERROR_AUTH;
        return false;
    }
    char body[2048];
    if (!build_order_ids_json(order_ids, count, status, body, sizeof(body))) {
        set_error(client, "다중 주문 요청 생성에 실패했습니다.");
        client->error_kind = BACKEND_ERROR_PARSE;
        return false;
    }
    HttpResponse response;
    if (http_request(client->base_url, "PATCH", "/api/admin/orders/bulk/status",
                     client->token, body, &response, client->error,
                     sizeof(client->error)) != 0)
        return request_failed(client);
    bool success = response.status_code >= 200 && response.status_code < 300;
    if (!success) response_error(client, &response);
    else clear_error(client);
    http_response_free(&response);
    return success;
}

bool backend_cancel_bulk_orders(BackendClient *client,
                                const unsigned int *order_ids, size_t count)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        client->error_kind = BACKEND_ERROR_AUTH;
        return false;
    }
    char body[2048];
    if (!build_order_ids_json(order_ids, count, NULL, body, sizeof(body))) {
        set_error(client, "다중 취소 요청 생성에 실패했습니다.");
        client->error_kind = BACKEND_ERROR_PARSE;
        return false;
    }
    HttpResponse response;
    if (http_request(client->base_url, "PATCH", "/api/admin/orders/bulk/cancel",
                     client->token, body, &response, client->error,
                     sizeof(client->error)) != 0)
        return request_failed(client);
    bool success = response.status_code >= 200 && response.status_code < 300;
    if (!success) response_error(client, &response);
    else clear_error(client);
    http_response_free(&response);
    return success;
}

bool backend_is_authenticated(const BackendClient *client)
{
    return client != NULL && client->token[0] != '\0';
}

void backend_logout(BackendClient *client)
{
    if (client == NULL) return;
    memset(client->token, 0, sizeof(client->token));
}

const char *backend_base_url(const BackendClient *client) { return client->base_url; }
const char *backend_last_error(const BackendClient *client) { return client->error; }
BackendErrorKind backend_error_kind(const BackendClient *client)
{
    return client == NULL ? BACKEND_ERROR_NETWORK : client->error_kind;
}
int backend_last_http_status(const BackendClient *client)
{
    return client == NULL ? 0 : client->http_status;
}
