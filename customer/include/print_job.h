#ifndef BUNSHIK_PRINT_JOB_H
#define BUNSHIK_PRINT_JOB_H

#include <stddef.h>

#define PRINT_ORDER_NUMBER_LENGTH 32
#define PRINT_ITEM_NAME_LENGTH    64
#define PRINT_JOB_MAX_ITEMS       20

typedef enum {
    PRINT_TYPE_RECEIPT,
    PRINT_TYPE_ORDER_NUMBER,
    PRINT_TYPE_UNKNOWN
} PrintJobType;

typedef struct {
    char name[PRINT_ITEM_NAME_LENGTH];
    int quantity;
    int price;
} PrintItem;

typedef struct {
    long id;
    PrintJobType type;
    char order_number[PRINT_ORDER_NUMBER_LENGTH];
    PrintItem items[PRINT_JOB_MAX_ITEMS];
    int item_count;
    int total_price;
} PrintJob;

/*
 * Spring Boot의 GET /api/print-jobs/pending 응답 JSON을 파싱합니다.
 * 대기 중인 작업이 없으면 0, 있으면 1, 형식이 잘못되면 -1을 반환합니다.
 */
int print_job_parse_pending(const char *json, PrintJob *job);

#endif