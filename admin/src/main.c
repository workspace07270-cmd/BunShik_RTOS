#define _POSIX_C_SOURCE 200809L

#include "backend_client.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POLL_SECONDS 10

typedef struct {
    BackendClient *backend;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    BackendOrder orders[BACKEND_MAX_ORDERS];
    size_t order_count;
    bool running;
    bool authenticated;
    bool has_snapshot;
    bool connection_failed;
} AdminMonitor;

static bool is_active(const BackendOrder *order)
{
    return strcmp(order->order_status, "접수") == 0 ||
           strcmp(order->order_status, "조리중") == 0;
}

static bool matches_filter(const BackendOrder *order, const char *filter)
{
    if (filter == NULL || filter[0] == '\0' || strcmp(filter, "active") == 0)
        return is_active(order);
    if (strcmp(filter, "all") == 0) return true;
    if (strcmp(filter, "received") == 0)
        return strcmp(order->order_status, "접수") == 0;
    if (strcmp(filter, "cooking") == 0)
        return strcmp(order->order_status, "조리중") == 0;
    if (strcmp(filter, "completed") == 0)
        return strcmp(order->order_status, "완료") == 0;
    return false;
}

static bool known_order(const AdminMonitor *monitor, unsigned int order_id)
{
    for (size_t index = 0; index < monitor->order_count; ++index) {
        if (monitor->orders[index].order_id == order_id) return true;
    }
    return false;
}

static void print_order(const BackendOrder *order)
{
    printf("%u | %s | %s | %s | %s | %u원 | %s\n",
           order->order_id, order->order_number, order->order_type,
           order->payment_method[0] ? order->payment_method : "미확인",
           order->order_status, order->total_price, order->created_at);
}

/* 호출 시 monitor->mutex를 보유해야 한다. */
static bool refresh_locked(AdminMonitor *monitor, bool announce_new)
{
    BackendOrder fresh[BACKEND_MAX_ORDERS];
    size_t fresh_count = 0;
    if (!backend_fetch_orders(monitor->backend, fresh, BACKEND_MAX_ORDERS,
                              &fresh_count)) {
        if (!monitor->connection_failed) {
            printf("\n[감시] 주문 동기화 실패: %s\nadmin> ",
                   backend_last_error(monitor->backend));
            fflush(stdout);
        }
        monitor->connection_failed = true;
        return false;
    }

    if (announce_new && monitor->has_snapshot) {
        for (size_t index = 0; index < fresh_count; ++index) {
            if (is_active(&fresh[index]) &&
                !known_order(monitor, fresh[index].order_id)) {
                printf("\n[신규 주문] ");
                print_order(&fresh[index]);
                printf("admin> ");
                fflush(stdout);
            }
        }
    }
    memcpy(monitor->orders, fresh, fresh_count * sizeof(*fresh));
    monitor->order_count = fresh_count;
    monitor->has_snapshot = true;
    if (monitor->connection_failed) {
        printf("\n[감시] 백엔드 연결이 복구됐습니다.\nadmin> ");
        fflush(stdout);
    }
    monitor->connection_failed = false;
    return true;
}

static void *monitor_main(void *context)
{
    AdminMonitor *monitor = context;
    pthread_mutex_lock(&monitor->mutex);
    while (monitor->running) {
        while (monitor->running && !monitor->authenticated)
            pthread_cond_wait(&monitor->wake, &monitor->mutex);
        if (!monitor->running) break;

        struct timespec deadline;
        timespec_get(&deadline, TIME_UTC);
        deadline.tv_sec += POLL_SECONDS;
        pthread_cond_timedwait(&monitor->wake, &monitor->mutex, &deadline);
        if (monitor->running && monitor->authenticated)
            refresh_locked(monitor, true);
    }
    pthread_mutex_unlock(&monitor->mutex);
    return NULL;
}

static void print_help(void)
{
    puts("명령:");
    puts("  login <아이디> <비밀번호>");
    puts("  sync                         즉시 백엔드 동기화");
    puts("  list [active|received|cooking|completed|all]");
    puts("  start <백엔드주문ID>         접수 -> 조리중");
    puts("  complete <백엔드주문ID>      조리중 -> 완료");
    puts("  cancel <백엔드주문ID>        주문 취소");
    puts("  server                       연결 대상 표시");
    puts("  help | quit");
}

static void print_cached_orders(AdminMonitor *monitor, const char *filter)
{
    pthread_mutex_lock(&monitor->mutex);
    if (!monitor->has_snapshot) {
        puts("아직 주문을 동기화하지 않았습니다. login 또는 sync를 실행하세요.");
        pthread_mutex_unlock(&monitor->mutex);
        return;
    }
    puts("ID | 주문번호 | 유형 | 결제 | 상태 | 금액 | 주문시각");
    size_t displayed = 0;
    for (size_t index = 0; index < monitor->order_count; ++index) {
        if (matches_filter(&monitor->orders[index], filter)) {
            print_order(&monitor->orders[index]);
            ++displayed;
        }
    }
    printf("표시된 주문: %zu건\n", displayed);
    pthread_mutex_unlock(&monitor->mutex);
}

static bool update_and_refresh(AdminMonitor *monitor, unsigned int id,
                               const char *status)
{
    pthread_mutex_lock(&monitor->mutex);
    bool success = backend_update_status(monitor->backend, id, status);
    if (success) refresh_locked(monitor, false);
    else printf("상태 변경 실패: %s\n", backend_last_error(monitor->backend));
    pthread_mutex_unlock(&monitor->mutex);
    return success;
}

int main(void)
{
    const char *configured_url = getenv("BUNSHIK_API_BASE_URL");
    AdminMonitor monitor = {0};
    monitor.backend = backend_client_create(
        configured_url && configured_url[0] ? configured_url
                                             : "http://127.0.0.1:8080");
    if (monitor.backend == NULL || pthread_mutex_init(&monitor.mutex, NULL) ||
        pthread_cond_init(&monitor.wake, NULL)) {
        fputs("관리자 RTOS를 초기화하지 못했습니다.\n", stderr);
        backend_client_destroy(monitor.backend);
        return EXIT_FAILURE;
    }
    monitor.running = true;
    if (pthread_create(&monitor.thread, NULL, monitor_main, &monitor) != 0) {
        fputs("주문 감시 태스크를 시작하지 못했습니다.\n", stderr);
        backend_client_destroy(monitor.backend);
        return EXIT_FAILURE;
    }

    puts("BunShik 관리자 RTOS - Spring Boot 연동 모드");
    printf("백엔드: %s / 자동 동기화: %d초\n",
           backend_base_url(monitor.backend), POLL_SECONDS);
    print_help();

    char line[256];
    while (printf("admin> "), fflush(stdout), fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;
        if (strcmp(line, "quit") == 0) break;
        if (strcmp(line, "help") == 0) { print_help(); continue; }
        if (strcmp(line, "server") == 0) {
            pthread_mutex_lock(&monitor.mutex);
            printf("%s (%s)\n", backend_base_url(monitor.backend),
                   monitor.authenticated ? "로그인됨" : "로그인 필요");
            pthread_mutex_unlock(&monitor.mutex);
            continue;
        }
        if (strcmp(line, "sync") == 0) {
            pthread_mutex_lock(&monitor.mutex);
            if (refresh_locked(&monitor, false))
                printf("백엔드 주문 %zu건을 동기화했습니다.\n", monitor.order_count);
            pthread_mutex_unlock(&monitor.mutex);
            print_cached_orders(&monitor, "active");
            continue;
        }
        if (strncmp(line, "list", 4) == 0 &&
            (line[4] == '\0' || line[4] == ' ')) {
            const char *filter = line[4] == '\0' ? "active" : line + 5;
            print_cached_orders(&monitor, filter);
            continue;
        }

        char username[64], password[128];
        if (sscanf(line, "login %63s %127s", username, password) == 2) {
            pthread_mutex_lock(&monitor.mutex);
            bool success = backend_login(monitor.backend, username, password);
            if (success) {
                monitor.authenticated = true;
                refresh_locked(&monitor, false);
                pthread_cond_signal(&monitor.wake);
            }
            pthread_mutex_unlock(&monitor.mutex);
            if (success) {
                puts("백엔드 관리자 로그인 성공");
                print_cached_orders(&monitor, "active");
            } else printf("로그인 실패: %s\n", backend_last_error(monitor.backend));
            continue;
        }

        unsigned int id;
        if (sscanf(line, "start %u", &id) == 1) {
            if (update_and_refresh(&monitor, id, "조리중"))
                puts("백엔드 주문을 조리중으로 변경했습니다.");
            continue;
        }
        if (sscanf(line, "complete %u", &id) == 1) {
            if (update_and_refresh(&monitor, id, "완료"))
                puts("백엔드 주문을 완료로 변경했습니다.");
            continue;
        }
        if (sscanf(line, "cancel %u", &id) == 1) {
            pthread_mutex_lock(&monitor.mutex);
            bool success = backend_cancel_order(monitor.backend, id);
            if (success) refresh_locked(&monitor, false);
            else printf("주문 취소 실패: %s\n", backend_last_error(monitor.backend));
            pthread_mutex_unlock(&monitor.mutex);
            if (success) puts("백엔드 주문을 취소했습니다.");
            continue;
        }
        puts("알 수 없는 명령입니다. help를 입력하세요.");
    }

    pthread_mutex_lock(&monitor.mutex);
    monitor.running = false;
    pthread_cond_signal(&monitor.wake);
    pthread_mutex_unlock(&monitor.mutex);
    pthread_join(monitor.thread, NULL);
    pthread_cond_destroy(&monitor.wake);
    pthread_mutex_destroy(&monitor.mutex);
    backend_client_destroy(monitor.backend);
    puts("관리자 RTOS를 종료합니다.");
    return EXIT_SUCCESS;
}
