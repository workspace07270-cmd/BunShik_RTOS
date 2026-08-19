#include "admin_mode.h"
#include "customer_mode.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>

static const char *environment(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

int main(void)
{
    const char *backend_url = environment("BUNSHIK_API_BASE_URL",
            "http://127.0.0.1:8080");
    const char *username = environment("BUNSHIK_ADMIN_USERNAME", "");
    const char *password = environment("BUNSHIK_ADMIN_PASSWORD", "");

    puts("BunShik 통합 FreeRTOS 서비스");
    printf("백엔드: %s\n", backend_url);

    if (username[0] == '\0' || password[0] == '\0') {
        fputs("관리자 자동 로그인 정보가 없습니다. make setup을 실행하세요.\n",
              stderr);
        return EXIT_FAILURE;
    }
    if (admin_tasks_start(backend_url, username, password) != EXIT_SUCCESS) {
        fputs("관리자 RTOS 태스크를 만들지 못했습니다.\n", stderr);
        return EXIT_FAILURE;
    }
    if (customer_tasks_start(backend_url) != EXIT_SUCCESS) {
        fputs("고객 출력 RTOS 태스크를 만들지 못했습니다.\n", stderr);
        return EXIT_FAILURE;
    }

    puts("[RTOS] 관리자 주문 감시와 고객 출력 태스크 시작");
    puts("[RTOS] 종료: Ctrl+C");
    vTaskStartScheduler();
    return EXIT_FAILURE;
}
