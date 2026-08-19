#define _POSIX_C_SOURCE 199309L
#include "customer_http_client.h"
#include "print_job.h"
#include "printer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POLL_INTERVAL_MS 1000
#define RESPONSE_BUFFER_SIZE 4096

static void sleep_ms(unsigned int milliseconds) {
    struct timespec duration = {
        .tv_sec = (time_t) (milliseconds / 1000U),
        .tv_nsec = (long) (milliseconds % 1000U) * 1000000L,
    };
    while (nanosleep(&duration, &duration) == -1 && errno == EINTR) {
    }
}

static int fetch_pending(const http_server_t *server, PrintJob *job) {
    char body[RESPONSE_BUFFER_SIZE];
    http_response_t response = {0};

    if (customer_http_request(server, "GET", "/api/print-jobs/pending", NULL,
            body, sizeof(body), &response) != 0 || response.status_code != 200) {
        return -1;
    }
    return print_job_parse_pending(body, job);
}

static void complete_job(const http_server_t *server, long job_id, const char *result) {
    char path[96], json[256], body[512];
    http_response_t response = {0};

    snprintf(path, sizeof(path), "/api/print-jobs/%ld/complete", job_id);
    snprintf(json, sizeof(json), "{\"result\":\"%s\"}", result);

    if (customer_http_request(server, "PATCH", path, json, body, sizeof(body), &response) == 0 &&
            response.status_code == 200) {
        printf("[PrintTask] 완료 신호 전송: id=%ld\n", job_id);
    } else {
        fprintf(stderr, "[PrintTask] 완료 신호 전송 실패: id=%ld\n", job_id);
    }
}

static void handle_job(const http_server_t *server, const PrintJob *job) {
    switch (job->type) {
    case PRINT_TYPE_RECEIPT:
        printf("[PrintTask] 영수증 출력 시작: id=%ld, 주문번호=%s\n", job->id, job->order_number);
        printer_print_receipt(job);
        complete_job(server, job->id, "receipt printed");
        break;
    case PRINT_TYPE_ORDER_NUMBER:
        printf("[PrintTask] 주문번호표 출력 시작: id=%ld, 주문번호=%s\n", job->id, job->order_number);
        printer_print_order_number(job);
        complete_job(server, job->id, "order number printed");
        break;
    default:
        fprintf(stderr, "[PrintTask] 알 수 없는 출력 타입: id=%ld\n", job->id);
        break;
    }
}

int customer_run(const char *url) {
    http_server_t server;

    if (customer_http_server_parse(url, &server) < 0) {
        fprintf(stderr, "서버 주소를 해석할 수 없습니다: %s\n", url);
        return EXIT_FAILURE;
    }

    printf("[BunShik Customer RTOS] Spring Boot 명령 대기: %s\n", url);

    for (;;) {
        PrintJob job;
        int result = fetch_pending(&server, &job);

        if (result == 1) {
            handle_job(&server, &job);
        }
        /* result == 0: 대기 중 작업 없음, result == -1: 조회 실패(다음 폴링에 재시도) */

        sleep_ms(POLL_INTERVAL_MS);
    }

    return EXIT_SUCCESS;
}
