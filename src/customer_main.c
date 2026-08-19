#define _POSIX_C_SOURCE 199309L
#include "customer_http_client.h"
#include "customer_mode.h"
#include "print_job.h"
#include "printer.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_INTERVAL_MS         1000
#define RESPONSE_BUFFER_SIZE     4096
#define SERVER_CHECK_RETRY_MS    2000

static http_server_t spring_server;
/* 토큰을 가진 폴링 태스크 하나만 새 출력 작업을 시작할 수 있습니다. */
static SemaphoreHandle_t print_slot;

/*
 * 태스크를 만들기 전에 Spring Boot 서버가 응답하는지 확인합니다.
 * 출력 작업의 DB 상태와 무관한 기존 메뉴 API로 서버 기동 여부를 검사합니다.
 */
static BaseType_t check_server_connection(void) {
    char body[256];
    http_response_t response = {0};
    int result = customer_http_request(&spring_server, "GET", "/api/menus",
            NULL, body, sizeof(body), &response);
    if (result == 0 && response.status_code != 200)
        fprintf(stderr, "[BunShik Customer RTOS] 백엔드 확인 응답 코드: %d\n",
                response.status_code);
    return result == 0 && response.status_code == 200 ? pdTRUE : pdFALSE;
}

static int fetch_pending(PrintJob *job) {
    char body[RESPONSE_BUFFER_SIZE];
    http_response_t response = {0};

    if (customer_http_request(&spring_server, "GET", "/api/print-jobs/pending", NULL,
            body, sizeof(body), &response) != 0 || response.status_code != 200) {
        return -1;
    }
    return print_job_parse_pending(body, job);
}

static void complete_job(long job_id, const char *result) {
    char path[96], json[256], body[512];
    http_response_t response = {0};

    snprintf(path, sizeof(path), "/api/print-jobs/%ld/complete", job_id);
    snprintf(json, sizeof(json), "{\"result\":\"%s\"}", result);

    if (customer_http_request(&spring_server, "PATCH", path, json, body, sizeof(body), &response) == 0 &&
            response.status_code == 200) {
        printf("[PrintTask] 완료 신호 전송: id=%ld\n", job_id);
    } else {
        fprintf(stderr, "[PrintTask] 완료 신호 전송 실패: id=%ld\n", job_id);
    }
}

static void handle_job(const PrintJob *job) {
    switch (job->type) {
    case PRINT_TYPE_RECEIPT:
        printf("[PrintTask] 영수증 출력 시작: id=%ld, 주문번호=%s\n", job->id, job->order_number);
        printer_print_receipt(job);
        complete_job(job->id, "receipt printed");
        break;
    case PRINT_TYPE_ORDER_NUMBER:
        printf("[PrintTask] 주문번호표 출력 시작: id=%ld, 주문번호=%s\n", job->id, job->order_number);
        printer_print_order_number(job);
        complete_job(job->id, "order number printed");
        break;
    default:
        fprintf(stderr, "[PrintTask] 알 수 없는 출력 타입: id=%ld\n", job->id);
        break;
    }
}

/*
 * 실제 인쇄를 처리하는 태스크입니다. PrintPollTask가 새 작업을 찾을 때마다
 * 힙에 복사해서 넘겨주며, 처리가 끝나면 스스로 삭제됩니다(day5의
 * worker_task와 같은 패턴).
 */
static void print_task(void *parameter) {
    PrintJob job = *(PrintJob *) parameter;
    vPortFree(parameter);

    handle_job(&job);

    xSemaphoreGive(print_slot);
    vTaskDelete(NULL);
}

/* 1초 주기로 Spring Boot의 대기 중인 인쇄 작업을 polling합니다. */
static void print_poll_task(void *parameter) {
    (void) parameter;
    for (;;) {
        if (xSemaphoreTake(print_slot, 0) == pdTRUE) {
            PrintJob parsed;
            int result = fetch_pending(&parsed);

            if (result == 1) {
                PrintJob *job = pvPortMalloc(sizeof(*job));
                if (job != NULL) {
                    *job = parsed;
                    if (xTaskCreate(print_task, "PrintTask", 4096,
                            job, 3, NULL) != pdPASS) {
                        vPortFree(job);
                        xSemaphoreGive(print_slot);
                    }
                } else xSemaphoreGive(print_slot);
            } else {
                xSemaphoreGive(print_slot);
            }
            /* result == 0: 대기 중 작업 없음, result == -1: 조회 실패(다음 폴링에 재시도) */
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static void customer_boot_task(void *parameter)
{
    const char *url = parameter;
    printf("[BunShik Customer RTOS] 서버 연결 확인 중: %s\n", url);
    while (check_server_connection() != pdTRUE) {
        fprintf(stderr,
                "[BunShik Customer RTOS] 서버에 연결할 수 없습니다. %d ms 후 재시도합니다: %s\n",
                SERVER_CHECK_RETRY_MS, url);
        vTaskDelay(pdMS_TO_TICKS(SERVER_CHECK_RETRY_MS));
    }
    printf("[BunShik Customer RTOS] 서버 연결 확인 완료: %s\n", url);

    /* 연결이 확인된 뒤에야 동기화 객체와 폴링 태스크를 만듭니다. */
    print_slot = xSemaphoreCreateBinary();
    configASSERT(print_slot != NULL);
    xSemaphoreGive(print_slot);
    configASSERT(xTaskCreate(print_poll_task, "PrintPollTask",
            2048, NULL, 2, NULL) == pdPASS);
    vTaskDelete(NULL);
}

int customer_tasks_start(const char *url) {
    if (customer_http_server_parse(url, &spring_server) < 0) {
        fprintf(stderr, "서버 주소를 해석할 수 없습니다: %s\n", url);
        return EXIT_FAILURE;
    }

    configASSERT(xTaskCreate(customer_boot_task, "CustomerBootTask",
            2048, (void *)url, 2, NULL) == pdPASS);

    return EXIT_SUCCESS;
}

int customer_run(const char *url) {
    if (customer_tasks_start(url) != EXIT_SUCCESS) return EXIT_FAILURE;

    printf("[BunShik Customer RTOS] FreeRTOS 스케줄러 시작\n");
    vTaskStartScheduler();
    return EXIT_FAILURE;
}
