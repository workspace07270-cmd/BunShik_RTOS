#include "backend_client.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BackendClient {
    char base_url[256];
    char token[2048];
    char error[256];
};

static void set_error(BackendClient *client, const char *message)
{
    snprintf(client->error, sizeof(client->error), "%s", message);
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

static void response_error(BackendClient *client, const HttpResponse *response)
{
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
        return false;
    bool success = response.status_code >= 200 && response.status_code < 300 &&
                   json_string(response.body, "accessToken", client->token,
                               sizeof(client->token));
    if (!success) response_error(client, &response);
    http_response_free(&response);
    return success;
}

bool backend_fetch_orders(BackendClient *client, BackendOrder *orders,
                          size_t capacity, size_t *count)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        return false;
    }
    HttpResponse response;
    if (http_request(client->base_url, "GET", "/api/admin/orders",
                     client->token, NULL, &response, client->error,
                     sizeof(client->error)) != 0) return false;
    if (response.status_code < 200 || response.status_code >= 300) {
        response_error(client, &response);
        http_response_free(&response);
        return false;
    }

    *count = 0;
    const char *cursor = strstr(response.body, "\"data\"");
    cursor = cursor ? strchr(cursor, '[') : NULL;
    if (cursor == NULL) {
        set_error(client, "주문 응답의 data 배열을 찾지 못했습니다.");
        http_response_free(&response);
        return false;
    }
    ++cursor;
    while (*cursor && *cursor != ']' && *count < capacity) {
        const char *start = strchr(cursor, '{');
        if (start == NULL) break;
        int depth = 0;
        const char *end = start;
        for (; *end; ++end) {
            if (*end == '{') ++depth;
            else if (*end == '}' && --depth == 0) break;
        }
        if (*end != '}') break;
        size_t length = (size_t)(end - start + 1);
        char *object = malloc(length + 1);
        if (object == NULL) break;
        memcpy(object, start, length);
        object[length] = '\0';
        BackendOrder *order = &orders[(*count)++];
        memset(order, 0, sizeof(*order));
        json_unsigned(object, "orderId", &order->order_id);
        json_unsigned(object, "totalPrice", &order->total_price);
        json_string(object, "orderNumber", order->order_number, sizeof(order->order_number));
        json_string(object, "orderType", order->order_type, sizeof(order->order_type));
        json_string(object, "orderStatus", order->order_status, sizeof(order->order_status));
        json_string(object, "createdAt", order->created_at, sizeof(order->created_at));
        json_string(object, "paymentMethod", order->payment_method, sizeof(order->payment_method));
        free(object);
        cursor = end + 1;
    }
    http_response_free(&response);
    client->error[0] = '\0';
    return true;
}

bool backend_update_status(BackendClient *client, unsigned int order_id,
                           const char *status)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        return false;
    }
    char path[96], body[96];
    snprintf(path, sizeof(path), "/api/admin/orders/%u/status", order_id);
    snprintf(body, sizeof(body), "{\"orderStatus\":\"%s\"}", status);
    HttpResponse response;
    if (http_request(client->base_url, "PATCH", path, client->token, body,
                     &response, client->error, sizeof(client->error)) != 0)
        return false;
    bool success = response.status_code >= 200 && response.status_code < 300;
    if (!success) response_error(client, &response);
    http_response_free(&response);
    return success;
}

bool backend_cancel_order(BackendClient *client, unsigned int order_id)
{
    if (!backend_is_authenticated(client)) {
        set_error(client, "먼저 login 명령으로 로그인하세요.");
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/api/admin/orders/%u/cancel", order_id);
    HttpResponse response;
    if (http_request(client->base_url, "PATCH", path, client->token, NULL,
                     &response, client->error, sizeof(client->error)) != 0)
        return false;
    bool success = response.status_code >= 200 && response.status_code < 300;
    if (!success) response_error(client, &response);
    http_response_free(&response);
    return success;
}

bool backend_is_authenticated(const BackendClient *client)
{
    return client != NULL && client->token[0] != '\0';
}

const char *backend_base_url(const BackendClient *client) { return client->base_url; }
const char *backend_last_error(const BackendClient *client) { return client->error; }
