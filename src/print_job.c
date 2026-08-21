#include "print_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *json_value(const char *json, const char *key) {
    static char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *value = strstr(json, pattern);
    return value == NULL ? NULL : value + strlen(pattern);
}

static int json_string(const char *json, const char *key, char *out, size_t size) {
    const char *value = json_value(json, key);
    if (value == NULL) return -1;
    while (*value == ' ') value++;
    if (*value++ != '\"') return -1;
    size_t i = 0;
    while (*value && *value != '\"' && i + 1 < size) out[i++] = *value++;
    out[i] = '\0';
    return *value == '\"' ? 0 : -1;
}

static int json_long(const char *json, const char *key, long *out) {
    const char *value = json_value(json, key);
    if (value == NULL) return -1;
    char *end;
    *out = strtol(value, &end, 10);
    return end == value ? -1 : 0;
}

static int json_int(const char *json, const char *key, int *out) {
    long value;
    if (json_long(json, key, &value) != 0) return -1;
    *out = (int) value;
    return 0;
}

static PrintJobType parse_type(const char *type_text) {
    if (strcmp(type_text, "RECEIPT") == 0) return PRINT_TYPE_RECEIPT;
    if (strcmp(type_text, "ORDER_NUMBER") == 0) return PRINT_TYPE_ORDER_NUMBER;
    return PRINT_TYPE_UNKNOWN;
}

/* "items":[{"menu_name":"...","quantity":N,"price":N}, ...] 파싱 */
static int parse_items(const char *json, PrintJob *job) {
    job->item_count = 0;

    const char *cursor = json_value(json, "items");
    if (cursor == NULL) return -1;

    cursor = strchr(cursor, '[');
    if (cursor == NULL) return -1;
    cursor++;

    while (*cursor != ']' && job->item_count < PRINT_JOB_MAX_ITEMS) {
        const char *object_start = strchr(cursor, '{');
        if (object_start == NULL) return -1;
        const char *object_end = strchr(object_start, '}');
        if (object_end == NULL) return -1;

        /* 이번 품목 객체만 잘라서 파싱 (다음 품목의 같은 키와 섞이지 않도록) */
        size_t length = (size_t) (object_end - object_start) + 1;
        char buffer[PRINT_ITEM_NAME_LENGTH + 96];
        if (length >= sizeof(buffer)) return -1;
        memcpy(buffer, object_start, length);
        buffer[length] = '\0';

        PrintItem *item = &job->items[job->item_count];
        if (json_string(buffer, "menu_name", item->name, sizeof(item->name)) != 0) return -1;
        if (json_int(buffer, "quantity", &item->quantity) != 0) return -1;
        if (json_int(buffer, "price", &item->price) != 0) return -1;
        job->item_count++;

        cursor = object_end + 1;
        while (*cursor == ',' || *cursor == ' ') cursor++;
    }

    return 0;
}

int print_job_parse_pending(const char *json, PrintJob *job) {
    const char *data = json_value(json, "data");
    if (data == NULL) return -1;

    while (*data == ' ') data++;
    if (strncmp(data, "[]", 2) == 0) return 0; /* 대기 중인 작업 없음 */

    long id;
    if (json_long(data, "id", &id) != 0) return -1;
    job->id = id;

    char type_text[16];
    if (json_string(data, "type", type_text, sizeof(type_text)) != 0) return -1;
    job->type = parse_type(type_text);

    if (json_string(data, "order_number", job->order_number, sizeof(job->order_number)) != 0)
        return -1;

    if (json_string(data, "order_type", job->order_type, sizeof(job->order_type)) != 0)
        return -1;

    if (json_string(data, "payment_method", job->payment_method, sizeof(job->payment_method)) != 0)
        return -1;

    if (json_int(data, "total_price", &job->total_price) != 0) return -1;

    if (job->type == PRINT_TYPE_RECEIPT) {
        if (parse_items(data, job) != 0) return -1;
    } else {
        job->item_count = 0;
    }

    return 1;
}
