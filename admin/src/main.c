#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void)
{
    puts("명령:");
    puts("  add <주문번호> <우선순위> <조리ms> <메뉴명>");
    puts("  list");
    puts("  cancel <내부ID>");
    puts("  wait");
    puts("  help");
    puts("  quit");
}

static void print_orders(Scheduler *scheduler)
{
    Order orders[SCHEDULER_MAX_ORDERS];
    size_t count = scheduler_snapshot(scheduler, orders, SCHEDULER_MAX_ORDERS);

    puts("ID | 주문번호 | 우선순위 | 상태 | 메뉴");
    for (size_t index = 0; index < count; ++index) {
        printf("%u | %s | %d | %s | %s\n", orders[index].id,
               orders[index].order_number, orders[index].priority,
               order_status_name(orders[index].status), orders[index].menu);
    }
    if (count == 0) {
        puts("등록된 주문이 없습니다.");
    }
}

int main(void)
{
    Scheduler *scheduler = scheduler_create();
    if (scheduler == NULL || !scheduler_start(scheduler)) {
        fputs("스케줄러를 시작하지 못했습니다.\n", stderr);
        scheduler_destroy(scheduler);
        return EXIT_FAILURE;
    }

    puts("BunShik 관리자 RTOS 시뮬레이터");
    print_help();

    char line[256];
    while (printf("admin> "), fflush(stdout), fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "quit") == 0) {
            break;
        }
        if (strcmp(line, "help") == 0) {
            print_help();
            continue;
        }
        if (strcmp(line, "list") == 0) {
            print_orders(scheduler);
            continue;
        }
        if (strcmp(line, "wait") == 0) {
            puts(scheduler_wait_idle(scheduler, 30000)
                     ? "모든 주문 처리가 끝났습니다."
                     : "대기 시간이 초과됐습니다.");
            continue;
        }

        unsigned int id;
        if (sscanf(line, "cancel %u", &id) == 1) {
            puts(scheduler_cancel(scheduler, id)
                     ? "주문을 취소했습니다."
                     : "대기 중인 주문만 취소할 수 있습니다.");
            continue;
        }

        char order_number[ORDER_NUMBER_LENGTH];
        char menu[ORDER_MENU_LENGTH];
        int priority;
        unsigned int cook_time_ms;
        if (sscanf(line, "add %31s %d %u %95[^\n]", order_number, &priority,
                   &cook_time_ms, menu) == 4) {
            int new_id = scheduler_submit(scheduler, order_number, menu,
                                          priority, cook_time_ms);
            if (new_id < 0) {
                puts("주문 형식이 잘못됐거나 큐가 가득 찼습니다.");
            } else {
                printf("주문을 등록했습니다. 내부 ID=%d\n", new_id);
            }
            continue;
        }

        puts("알 수 없는 명령입니다. help를 입력하세요.");
    }

    scheduler_destroy(scheduler);
    puts("관리자 시뮬레이터를 종료합니다.");
    return EXIT_SUCCESS;
}
