#include "scheduler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void)
{
    puts("명령:");
    puts("  receive <주문번호> <우선순위> <예상조리ms> <메뉴명>");
    puts("  start <내부ID>");
    puts("  complete <내부ID>");
    puts("  cancel <내부ID>");
    puts("  detail <내부ID>");
    puts("  list [all|received|cooking|completed|canceled]");
    puts("  help");
    puts("  quit");
}

static bool status_matches(OrderStatus status, const char *filter)
{
    if (filter == NULL || filter[0] == '\0' || strcmp(filter, "all") == 0) {
        return true;
    }
    return (strcmp(filter, "received") == 0 && status == ORDER_RECEIVED) ||
           (strcmp(filter, "cooking") == 0 && status == ORDER_COOKING) ||
           (strcmp(filter, "completed") == 0 && status == ORDER_COMPLETED) ||
           (strcmp(filter, "canceled") == 0 && status == ORDER_CANCELED);
}

static void print_order(const Order *order)
{
    printf("%u | %s | %d | %s | %u ms | %s\n", order->id,
           order->order_number, order->priority,
           order_status_name(order->status), order->cook_time_ms, order->menu);
}

static void print_orders(Scheduler *scheduler, const char *filter)
{
    Order orders[SCHEDULER_MAX_ORDERS];
    size_t count = scheduler_snapshot(scheduler, orders, SCHEDULER_MAX_ORDERS);
    size_t displayed = 0;

    puts("ID | 주문번호 | 우선순위 | 상태 | 예상시간 | 메뉴");
    for (size_t index = 0; index < count; ++index) {
        if (status_matches(orders[index].status, filter)) {
            print_order(&orders[index]);
            ++displayed;
        }
    }
    if (displayed == 0) puts("조건에 맞는 주문이 없습니다.");
}

static void print_detail(Scheduler *scheduler, unsigned int id)
{
    Order order;
    if (!scheduler_get_order(scheduler, id, &order)) {
        puts("주문을 찾을 수 없습니다.");
        return;
    }
    printf("내부 ID: %u\n주문번호: %s\n메뉴: %s\n", order.id,
           order.order_number, order.menu);
    printf("우선순위: %d\n예상 조리시간: %u ms\n현재 상태: %s\n",
           order.priority, order.cook_time_ms,
           order_status_name(order.status));
}

static char *skip_spaces(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    return text;
}

int main(void)
{
    Scheduler *scheduler = scheduler_create();
    if (scheduler == NULL) {
        fputs("주문 관리자를 초기화하지 못했습니다.\n", stderr);
        return EXIT_FAILURE;
    }

    puts("BunShik 관리자 RTOS 시뮬레이터");
    puts("주문 상태는 관리자의 명령으로 변경됩니다.");
    print_help();

    char line[256];
    while (printf("admin> "), fflush(stdout), fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "quit") == 0) break;
        if (strcmp(line, "help") == 0) {
            print_help();
            continue;
        }
        if (strncmp(line, "list", 4) == 0 &&
            (line[4] == '\0' || isspace((unsigned char)line[4]))) {
            print_orders(scheduler, skip_spaces(line + 4));
            continue;
        }

        unsigned int id;
        if (sscanf(line, "detail %u", &id) == 1) {
            print_detail(scheduler, id);
            continue;
        }
        if (sscanf(line, "start %u", &id) == 1) {
            puts(scheduler_start_order(scheduler, id)
                     ? "조리를 시작했습니다."
                     : "접수 상태의 주문만 조리를 시작할 수 있습니다.");
            continue;
        }
        if (sscanf(line, "complete %u", &id) == 1) {
            puts(scheduler_complete_order(scheduler, id)
                     ? "조리를 완료했습니다."
                     : "조리 중인 주문만 완료할 수 있습니다.");
            continue;
        }
        if (sscanf(line, "cancel %u", &id) == 1) {
            puts(scheduler_cancel(scheduler, id)
                     ? "주문을 취소했습니다."
                     : "접수 또는 조리 중인 주문만 취소할 수 있습니다.");
            continue;
        }

        char order_number[ORDER_NUMBER_LENGTH];
        char menu[ORDER_MENU_LENGTH];
        int priority;
        unsigned int cook_time_ms;
        if (sscanf(line, "receive %31s %d %u %95[^\n]", order_number,
                   &priority, &cook_time_ms, menu) == 4) {
            int new_id = scheduler_submit(scheduler, order_number, menu,
                                          priority, cook_time_ms);
            if (new_id < 0) {
                puts("주문 형식이 잘못됐거나 저장 공간이 가득 찼습니다.");
            } else {
                printf("주문을 접수했습니다. 내부 ID=%d\n", new_id);
            }
            continue;
        }
        puts("알 수 없는 명령입니다. help를 입력하세요.");
    }

    scheduler_destroy(scheduler);
    puts("관리자 시뮬레이터를 종료합니다.");
    return EXIT_SUCCESS;
}
