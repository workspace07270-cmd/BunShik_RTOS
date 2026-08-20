#define _POSIX_C_SOURCE 199309L
#include "customer_http_client.h"
#include "customer_mode.h"
#include "print_job.h"
#include "printer.h"
#include "printer_paper.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_INTERVAL_MS 1000
#define RESPONSE_BUFFER_SIZE 4096
#define SERVER_CHECK_RETRY_MS 2000
#define COMPLETE_RETRY_INITIAL_MS 1000
#define COMPLETE_RETRY_MAX_MS 10000
#define PRINTER_PAPER_INITIAL_SHEETS 50

static http_server_t spring_server;
/* 토큰을 가진 폴링 태스크 하나만 새 출력 작업을 시작할 수 있습니다. */
static SemaphoreHandle_t print_slot;
static QueueHandle_t print_jobs;

/*
 * 태스크를 만들기 전에 Spring Boot 서버가 응답하는지 확인합니다.
 * 출력 작업의 DB 상태와 무관한 기존 메뉴 API로 서버 기동 여부를 검사합니다.
 */
static BaseType_t check_server_connection(void)
{
    char body[256];
    http_response_t response = {0};
    int result = customer_http_request(&spring_server, "GET", "/api/menus",
                                       NULL, body, sizeof(body), &response);
    if (result == 0 && response.status_code != 200)
        fprintf(stderr, "[BunShik Customer RTOS] 백엔드 확인 응답 코드: %d\n",
                response.status_code);
    return result == 0 && response.status_code == 200 ? pdTRUE : pdFALSE;
}

static int fetch_pending(PrintJob *job)
{
    char body[RESPONSE_BUFFER_SIZE];
    http_response_t response = {0};

    if (customer_http_request(&spring_server, "GET", "/api/print-jobs/pending", NULL,
                              body, sizeof(body), &response) != 0 ||
        response.status_code != 200)
    {
        return -1;
    }
    return print_job_parse_pending(body, job);
}

static BaseType_t complete_job(long job_id, const char *result)
{
    char path[96], json[256], body[512];
    http_response_t response = {0};

    snprintf(path, sizeof(path), "/api/print-jobs/%ld/complete", job_id);
    snprintf(json, sizeof(json), "{\"result\":\"%s\"}", result);

    if (customer_http_request(&spring_server, "PATCH", path, json, body, sizeof(body), &response) == 0 &&
        response.status_code == 200)
    {
        printf("[PrintTask] 완료 신호 전송: id=%ld\n", job_id);
        return pdTRUE;
    }
    else
    {
        fprintf(stderr, "[PrintTask] 완료 신호 전송 실패: id=%ld, HTTP=%d\n",
                job_id, response.status_code);
        return pdFALSE;
    }
}

/*
 * 인쇄가 끝난 작업은 백엔드가 완료를 확인할 때까지 완료 요청만 재시도합니다.
 * 실패 직후 슬롯을 반납하면 /pending에서 같은 작업을 다시 받아 실제 영수증을
 * 중복 출력할 수 있으므로, 이 작업이 확인되기 전에는 다음 출력을 시작하지
 * 않습니다.
 */
static void complete_job_until_confirmed(long job_id, const char *result)
{
    TickType_t retry_ms = COMPLETE_RETRY_INITIAL_MS;

    while (complete_job(job_id, result) != pdTRUE)
    {
        fprintf(stderr,
                "[PrintTask] 인쇄는 완료됨; 완료 신호만 %lu ms 후 재시도: id=%ld\n",
                (unsigned long)retry_ms, job_id);
        vTaskDelay(pdMS_TO_TICKS(retry_ms));
        if (retry_ms < COMPLETE_RETRY_MAX_MS)
        {
            retry_ms *= 2;
            if (retry_ms > COMPLETE_RETRY_MAX_MS)
                retry_ms = COMPLETE_RETRY_MAX_MS;
        }
    }
}

static const char *handle_job(const PrintJob *job)
{
    switch (job->type)
    {
    case PRINT_TYPE_RECEIPT:
        printf("[PrintTask] 영수증 출력 시작: id=%ld, 주문번호=%s\n", job->id, job->order_number);
        if (printer_print_receipt(job)) return "receipt printed";
        fprintf(stderr, "[PrintTask] 용지 부족으로 영수증 출력 실패: id=%ld\n",
                job->id);
        return "receipt print failed: paper out";
    case PRINT_TYPE_ORDER_NUMBER:
        printf("[PrintTask] 주문번호표 출력 시작: id=%ld, 주문번호=%s\n", job->id, job->order_number);
        if (printer_print_order_number(job)) return "order number printed";
        fprintf(stderr,
                "[PrintTask] 용지 부족으로 주문번호표 출력 실패: id=%ld\n",
                job->id);
        return "order number print failed: paper out";
    default:
        fprintf(stderr, "[PrintTask] 알 수 없는 출력 타입: id=%ld\n", job->id);
        return "unsupported print type";
    }
}

/*
 * 실제 인쇄를 처리하는 영구 Worker Task입니다. 동적 Task를 매번 만들고
 * 삭제하지 않고 Queue에서 작업을 기다리므로 작업 종료 시점의 메모리 충돌을
 * 피할 수 있습니다.
 */
static void print_task(void *parameter)
{
    (void)parameter;
    PrintJob job;

    for (;;)
    {
        if (xQueueReceive(print_jobs, &job, portMAX_DELAY) != pdTRUE)
            continue;

        const char *result = handle_job(&job);
        complete_job_until_confirmed(job.id, result);
        xSemaphoreGive(print_slot);
    }
}

/* 1초 주기로 Spring Boot의 대기 중인 인쇄 작업을 polling합니다. */
static void print_poll_task(void *parameter)
{
    (void)parameter;
    for (;;)
    {
        if (xSemaphoreTake(print_slot, 0) == pdTRUE)
        {
            PrintJob parsed;
            int result = fetch_pending(&parsed);

            if (result == 1)
            {
                if (xQueueSend(print_jobs, &parsed, 0) != pdPASS)
                    xSemaphoreGive(print_slot);
            }
            else
            {
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
    while (check_server_connection() != pdTRUE)
    {
        fprintf(stderr,
                "[BunShik Customer RTOS] 서버에 연결할 수 없습니다. %d ms 후 재시도합니다: %s\n",
                SERVER_CHECK_RETRY_MS, url);
        vTaskDelay(pdMS_TO_TICKS(SERVER_CHECK_RETRY_MS));
    }
    printf("[BunShik Customer RTOS] 서버 연결 확인 완료: %s\n", url);

    printer_paper_init(PRINTER_PAPER_INITIAL_SHEETS);

    /* 연결이 확인된 뒤에야 동기화 객체와 폴링 태스크를 만듭니다. */
    print_slot = xSemaphoreCreateBinary();
    print_jobs = xQueueCreate(1, sizeof(PrintJob));
    configASSERT(print_slot != NULL);
    configASSERT(print_jobs != NULL);
    xSemaphoreGive(print_slot);
    configASSERT(xTaskCreate(print_task, "PrintTask",
                             4096, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(print_poll_task, "PrintPollTask",
                             2048, NULL, 2, NULL) == pdPASS);
    vTaskDelete(NULL);
}

int customer_tasks_start(const char *url)
{
    if (customer_http_server_parse(url, &spring_server) < 0)
    {
        fprintf(stderr, "서버 주소를 해석할 수 없습니다: %s\n", url);
        return EXIT_FAILURE;
    }

    configASSERT(xTaskCreate(customer_boot_task, "CustomerBootTask",
                             2048, (void *)url, 2, NULL) == pdPASS);

    return EXIT_SUCCESS;
}

int customer_run(const char *url)
{
    if (customer_tasks_start(url) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    printf("[BunShik Customer RTOS] FreeRTOS 스케줄러 시작\n");
    vTaskStartScheduler();
    return EXIT_FAILURE;
}
