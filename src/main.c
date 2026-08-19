#include "admin_mode.h"
#include "customer_mode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *customer_backend_url(void)
{
    const char *url = getenv("BUNSHIK_CUSTOMER_API_BASE_URL");
    if (url != NULL && url[0] != '\0') return url;
    url = getenv("BUNSHIK_API_BASE_URL");
    return url != NULL && url[0] != '\0' ? url : "http://localhost:8080";
}

static void print_usage(const char *program)
{
    printf("사용법:\n");
    printf("  %s admin\n", program);
    printf("  %s customer [백엔드URL]\n", program);
    printf("  %s                 대화형 모드 선택\n", program);
}

int main(int argc, char **argv)
{
    if (argc >= 2) {
        if (strcmp(argv[1], "admin") == 0 && argc == 2) return admin_run();
        if (strcmp(argv[1], "customer") == 0 && argc <= 3)
            return customer_run(argc == 3 ? argv[2] : customer_backend_url());
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    puts("BunShik RTOS");
    puts("1. 관리자 모드");
    puts("2. 고객 출력 모드");
    puts("0. 종료");
    printf("선택> ");
    fflush(stdout);

    char choice[16];
    if (fgets(choice, sizeof(choice), stdin) == NULL) return EXIT_SUCCESS;
    if (strcmp(choice, "1\n") == 0 || strcmp(choice, "1") == 0)
        return admin_run();
    if (strcmp(choice, "2\n") == 0 || strcmp(choice, "2") == 0)
        return customer_run(customer_backend_url());
    if (strcmp(choice, "0\n") == 0 || strcmp(choice, "0") == 0)
        return EXIT_SUCCESS;

    puts("올바른 모드를 선택하세요.");
    return EXIT_FAILURE;
}
