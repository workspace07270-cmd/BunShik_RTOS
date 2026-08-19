#define _POSIX_C_SOURCE 199309L
#include "customer_http_client.h"
#include "customer_mode.h"
#include "print_job.h"
#include "printer.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POLL_INTERVAL_MS         1000
#define RESPONSE_BUFFER_SIZE     4096
#define SERVER_CHECK_RETRY_MS    2000

static http_server_t spring_server;
/* 인쇄 태스크가 이미 실행 중이면 새 폴링 결과를 잠시 무시합니다. */
static volatile BaseType_t print_task_running = pdFALSE;

/*
 * 태스크를 만들기 전에 Spring Boot 서버가 응답하는지 확인합니다.
 * pending 목록을 조회해 TCP 연결과 HTTP 응답이 정상인지 함께 검사합니다.
 */
static BaseType_t check_server_connection(void) {
    char body[256];
    http_response_t response = {0};
    return customer_http_request(&spring_server, "GET", "/api/print-jobs/pending", NULL,
            body, sizeof(body), &response) == 0 ? pdTRUE : pdFALSE;
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

    print_task_running = pdFALSE;
    vTaskDelete(NULL);
}

/* 1초 주기로 Spring Boot의 대기 중인 인쇄 작업을 polling합니다. */
static void print_poll_task(void *parameter) {
    (void) parameter;
    for (;;) {
        if (!print_task_running) {
            PrintJob parsed;
            int result = fetch_pending(&parsed);

            if (result == 1) {
                PrintJob *job = pvPortMalloc(sizeof(*job));
                if (job != NULL) {
                    *job = parsed;
                    print_task_running = pdTRUE;
                    if (xTaskCreate(print_task, "PrintTask", 4096,
                            job, 3, NULL) != pdPASS) {
                        print_task_running = pdFALSE;
                        vPortFree(job);
                    }
                }
            }
            /* result == 0: 대기 중 작업 없음, result == -1: 조회 실패(다음 폴링에 재시도) */
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void vApplicationMallocFailedHook(void) { abort(); }

int customer_run(const char *url) {
    if (customer_http_server_parse(url, &spring_server) < 0) {
        fprintf(stderr, "서버 주소를 해석할 수 없습니다: %s\n", url);
        return EXIT_FAILURE;
    }

    /*
     * 스케줄러를 시작하고 태스크를 만들기 전에, 먼저 Spring Boot 서버가
     * 살아있는지 확인합니다. 서버가 아직 준비되지 않았다면 태스크를 만들지
     * 않고 일정 간격으로 재시도합니다.
     */
    printf("[BunShik Customer RTOS] 서버 연결 확인 중: %s\n", url);
    while (check_server_connection() != pdTRUE) {
        fprintf(stderr,
                "[BunShik Customer RTOS] 서버에 연결할 수 없습니다. %d ms 후 재시도합니다: %s\n",
                SERVER_CHECK_RETRY_MS, url);
        struct timespec wait_time = {
            .tv_sec = SERVER_CHECK_RETRY_MS / 1000,
            .tv_nsec = (SERVER_CHECK_RETRY_MS % 1000) * 1000000L,
        };
        nanosleep(&wait_time, NULL);
    }
    printf("[BunShik Customer RTOS] 서버 연결 확인 완료: %s\n", url);

    /* 연결이 확인된 뒤에야 폴링 태스크를 만들고 스케줄러를 시작합니다. */
    configASSERT(xTaskCreate(print_poll_task, "PrintPollTask",
            2048, NULL, 2, NULL) == pdPASS);

    printf("[BunShik Customer RTOS] FreeRTOS 스케줄러 시작\n");
    vTaskStartScheduler();
    return EXIT_FAILURE;
}