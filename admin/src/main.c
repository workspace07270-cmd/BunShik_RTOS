#define _POSIX_C_SOURCE 200809L

#include "backend_client.h"
#include "order_priority.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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
    unsigned int warned_order_ids[BACKEND_MAX_ORDERS];
    size_t warned_order_count;
    time_t last_successful_sync;
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
    if (strcmp(filter, "delayed") == 0)
        return order_urgency(order) >= ORDER_URGENCY_DELAYED;
    return false;
}

static bool was_warned(const AdminMonitor *monitor, unsigned int order_id)
{
    for (size_t index = 0; index < monitor->warned_order_count; ++index) {
        if (monitor->warned_order_ids[index] == order_id) return true;
    }
    return false;
}

static void mark_warned(AdminMonitor *monitor, unsigned int order_id)
{
    if (!was_warned(monitor, order_id) &&
        monitor->warned_order_count < BACKEND_MAX_ORDERS)
        monitor->warned_order_ids[monitor->warned_order_count++] = order_id;
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
    long wait_minutes = order_wait_minutes(order);
    OrderUrgency urgency = order_urgency(order);
    char wait_text[32];
    if (wait_minutes >= 0)
        snprintf(wait_text, sizeof(wait_text), "%ld분/%s", wait_minutes,
                 order_urgency_name(urgency));
    else snprintf(wait_text, sizeof(wait_text), "-/정상");
    printf("%u | %s | %s | %s | %s | %u원 | %s | %s\n",
           order->order_id, order->order_number, order->order_type,
           order->payment_method[0] ? order->payment_method : "미확인",
           order->order_status, order->total_price, wait_text,
           order->created_at);
}

/* 호출 시 monitor->mutex를 보유해야 한다. */
static void retry_pause(unsigned int seconds)
{
    struct timespec delay = {.tv_sec = (time_t)seconds, .tv_nsec = 0};
    while (nanosleep(&delay, &delay) == -1) {}
}

static bool refresh_locked(AdminMonitor *monitor, bool announce_new,
                           unsigned int retries)
{
    BackendOrder fresh[BACKEND_MAX_ORDERS];
    size_t fresh_count = 0;
    bool fetched = false;
    for (unsigned int attempt = 0; attempt <= retries; ++attempt) {
        fetched = backend_fetch_orders(monitor->backend, fresh,
                                       BACKEND_MAX_ORDERS, &fresh_count);
        if (fetched) break;
        if (backend_error_kind(monitor->backend) == BACKEND_ERROR_AUTH) {
            backend_logout(monitor->backend);
            monitor->authenticated = false;
            printf("\n[인증 만료] 다시 login 명령으로 로그인해 주세요.\nadmin> ");
            fflush(stdout);
            return false;
        }
        if (backend_error_kind(monitor->backend) != BACKEND_ERROR_NETWORK ||
            attempt == retries) break;
        unsigned int delay = 1U << attempt;
        printf("\n[네트워크] 주문 조회 실패, %u초 후 재시도합니다.\nadmin> ",
               delay);
        fflush(stdout);
        retry_pause(delay);
    }
    if (!fetched) {
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
    if (announce_new) {
        for (size_t index = 0; index < fresh_count; ++index) {
            OrderUrgency urgency = order_urgency(&fresh[index]);
            if (urgency >= ORDER_URGENCY_DELAYED &&
                !was_warned(monitor, fresh[index].order_id)) {
                printf("\n[%s 경고] 주문 %s이(가) 접수 후 %ld분 대기 중입니다.\n",
                       order_urgency_name(urgency), fresh[index].order_number,
                       order_wait_minutes(&fresh[index]));
                printf("admin> ");
                fflush(stdout);
                mark_warned(monitor, fresh[index].order_id);
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
    monitor->last_successful_sync = time(NULL);
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
            refresh_locked(monitor, true, 3);
    }
    pthread_mutex_unlock(&monitor->mutex);
    return NULL;
}

static void print_help(void)
{
    puts("명령:");
    puts("  login <아이디>               비밀번호는 숨김 입력");
    puts("  sync                         즉시 백엔드 동기화");
    puts("  list [active|received|cooking|delayed|completed|all]");
    puts("  detail <백엔드주문ID>        메뉴·옵션·세트 상세 조회");
    puts("  next                         다음 처리 추천 주문");
    puts("  start-next                   추천 주문 조리 시작");
    puts("  start <백엔드주문ID>         접수 -> 조리중");
    puts("  complete <백엔드주문ID>      조리중 -> 완료");
    puts("  cancel <백엔드주문ID>        주문 취소");
    puts("  server                       연결 대상 표시");
    puts("  connection                   연결·인증·동기화 상태");
    puts("  help | quit");
}

static void print_detail(const BackendOrderDetail *detail)
{
    const BackendOrder *order = &detail->order;
    printf("\n주문번호: %s (ID %u)\n", order->order_number, order->order_id);
    printf("유형: %s | 상태: %s | 결제: %s\n", order->order_type,
           order->order_status,
           order->payment_method[0] ? order->payment_method : "미확인");
    printf("총액: %u원 | 주문시각: %s\n", order->total_price,
           order->created_at);
    puts("주문 메뉴:");
    for (size_t index = 0; index < detail->item_count; ++index) {
        const BackendOrderItem *item = &detail->items[index];
        printf("- %s x %u (단가 %u원)\n", item->menu_name, item->quantity,
               item->unit_price);
        for (size_t option = 0; option < item->option_count; ++option) {
            printf("  + 옵션: %s (+%u원)\n",
                   item->options[option].option_name,
                   item->options[option].option_price);
        }
        for (size_t component = 0; component < item->component_count;
             ++component) {
            printf("  + 세트 구성: %s\n",
                   item->components[component].component_menu_name);
        }
    }
    if (detail->item_count == 0) puts("- 주문 메뉴 없음");
    putchar('\n');
}

static void clear_secret(char *secret, size_t length)
{
    volatile char *cursor = (volatile char *)secret;
    while (length-- > 0) *cursor++ = '\0';
}

static bool read_password(char *password, size_t capacity)
{
    struct termios old_settings;
    bool hidden = isatty(STDIN_FILENO) &&
                  tcgetattr(STDIN_FILENO, &old_settings) == 0;
    if (hidden) {
        struct termios new_settings = old_settings;
        new_settings.c_lflag &= (tcflag_t)~ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_settings);
    }
    printf("비밀번호: ");
    fflush(stdout);
    bool success = fgets(password, (int)capacity, stdin) != NULL;
    if (hidden) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_settings);
        putchar('\n');
    }
    if (success) password[strcspn(password, "\n")] = '\0';
    return success && password[0] != '\0';
}

static bool confirm_change(const char *action, unsigned int id)
{
    char answer[16];
    printf("실제 주문 %u을(를) %s할까요? (yes/no): ", id, action);
    fflush(stdout);
    if (fgets(answer, sizeof(answer), stdin) == NULL) return false;
    answer[strcspn(answer, "\n")] = '\0';
    return strcmp(answer, "yes") == 0;
}

static void print_cached_orders(AdminMonitor *monitor, const char *filter)
{
    pthread_mutex_lock(&monitor->mutex);
    if (!monitor->has_snapshot) {
        puts("아직 주문을 동기화하지 않았습니다. login 또는 sync를 실행하세요.");
        pthread_mutex_unlock(&monitor->mutex);
        return;
    }
    const BackendOrder *selected[BACKEND_MAX_ORDERS];
    size_t displayed = 0;
    for (size_t index = 0; index < monitor->order_count; ++index) {
        if (matches_filter(&monitor->orders[index], filter))
            selected[displayed++] = &monitor->orders[index];
    }
    for (size_t left = 0; left < displayed; ++left) {
        for (size_t right = left + 1; right < displayed; ++right) {
            OrderUrgency left_urgency = order_urgency(selected[left]);
            OrderUrgency right_urgency = order_urgency(selected[right]);
            long left_wait = order_wait_minutes(selected[left]);
            long right_wait = order_wait_minutes(selected[right]);
            if (right_urgency > left_urgency ||
                (right_urgency == left_urgency && right_wait > left_wait)) {
                const BackendOrder *temporary = selected[left];
                selected[left] = selected[right];
                selected[right] = temporary;
            }
    }
    }
    puts("ID | 주문번호 | 유형 | 결제 | 상태 | 금액 | 대기/우선순위 | 주문시각");
    for (size_t index = 0; index < displayed; ++index)
        print_order(selected[index]);
    printf("표시된 주문: %zu건\n", displayed);
    pthread_mutex_unlock(&monitor->mutex);
}

static bool get_next_order(AdminMonitor *monitor, BackendOrder *result)
{
    bool found = false;
    pthread_mutex_lock(&monitor->mutex);
    for (size_t index = 0; index < monitor->order_count; ++index) {
        if (!found || order_should_precede(&monitor->orders[index], result)) {
            if (strcmp(monitor->orders[index].order_status, "접수") == 0) {
                *result = monitor->orders[index];
                found = true;
            }
        }
    }
    pthread_mutex_unlock(&monitor->mutex);
    return found;
}

static void show_recommended_order(AdminMonitor *monitor,
                                   const BackendOrder *order)
{
    printf("\n다음 처리 추천 주문: %s (ID %u)\n", order->order_number,
           order->order_id);
    printf("대기시간: %ld분 | 우선순위: %s | 금액: %u원\n",
           order_wait_minutes(order), order_urgency_name(order_urgency(order)),
           order->total_price);

    BackendOrderDetail detail;
    pthread_mutex_lock(&monitor->mutex);
    bool success = backend_fetch_order_detail(monitor->backend,
                                               order->order_id, &detail);
    if (!success)
        printf("상세 조회 실패: %s\n", backend_last_error(monitor->backend));
    pthread_mutex_unlock(&monitor->mutex);
    if (success) print_detail(&detail);
}

static bool update_and_refresh(AdminMonitor *monitor, unsigned int id,
                               const char *status)
{
    pthread_mutex_lock(&monitor->mutex);
    bool success = backend_update_status(monitor->backend, id, status);
    if (success) refresh_locked(monitor, false, 0);
    else if (backend_error_kind(monitor->backend) == BACKEND_ERROR_AUTH) {
        backend_logout(monitor->backend);
        monitor->authenticated = false;
        puts("[인증 만료] 다시 로그인해 주세요.");
    } else if (backend_error_kind(monitor->backend) == BACKEND_ERROR_NETWORK) {
        puts("상태 변경 응답이 불확실하여 실제 주문 상태를 다시 확인합니다.");
        BackendOrderDetail detail;
        for (unsigned int attempt = 0; attempt < 3; ++attempt) {
            if (attempt > 0) retry_pause(1U << (attempt - 1));
            if (backend_fetch_order_detail(monitor->backend, id, &detail)) {
                success = strcmp(detail.order.order_status, status) == 0;
                break;
            }
        }
        if (success) {
            puts("백엔드에서 상태 변경이 반영된 것을 확인했습니다.");
            refresh_locked(monitor, false, 0);
        } else printf("상태 확인 실패: %s\n", backend_last_error(monitor->backend));
    } else printf("상태 변경 실패: %s\n", backend_last_error(monitor->backend));
    pthread_mutex_unlock(&monitor->mutex);
    return success;
}

static bool cancel_and_refresh(AdminMonitor *monitor, unsigned int id)
{
    pthread_mutex_lock(&monitor->mutex);
    bool success = backend_cancel_order(monitor->backend, id);
    if (success) {
        refresh_locked(monitor, false, 0);
    } else if (backend_error_kind(monitor->backend) == BACKEND_ERROR_AUTH) {
        backend_logout(monitor->backend);
        monitor->authenticated = false;
        puts("[인증 만료] 다시 로그인해 주세요.");
    } else if (backend_error_kind(monitor->backend) == BACKEND_ERROR_NETWORK) {
        puts("취소 응답이 불확실하여 실제 주문 상태를 다시 확인합니다.");
        BackendOrderDetail detail;
        for (unsigned int attempt = 0; attempt < 3; ++attempt) {
            if (attempt > 0) retry_pause(1U << (attempt - 1));
            if (backend_fetch_order_detail(monitor->backend, id, &detail)) {
                success = strcmp(detail.order.order_status, "취소") == 0;
                break;
            }
        }
        if (success) {
            puts("백엔드에서 주문 취소가 반영된 것을 확인했습니다.");
            refresh_locked(monitor, false, 0);
        } else printf("취소 상태 확인 실패: %s\n",
                      backend_last_error(monitor->backend));
    } else printf("주문 취소 실패: %s\n", backend_last_error(monitor->backend));
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
        if (strcmp(line, "connection") == 0) {
            pthread_mutex_lock(&monitor.mutex);
            printf("서버: %s\n", backend_base_url(monitor.backend));
            printf("인증: %s\n", monitor.authenticated ? "로그인됨" : "로그인 필요");
            printf("연결: %s\n", monitor.connection_failed ? "장애" :
                   (monitor.has_snapshot ? "정상" : "미확인"));
            if (monitor.last_successful_sync != 0) {
                char timestamp[32];
                struct tm local_time;
                localtime_r(&monitor.last_successful_sync, &local_time);
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                         &local_time);
                printf("마지막 동기화: %s\n", timestamp);
            }
            pthread_mutex_unlock(&monitor.mutex);
            continue;
        }
        if (strcmp(line, "sync") == 0) {
            pthread_mutex_lock(&monitor.mutex);
            if (refresh_locked(&monitor, false, 3))
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
        if (strcmp(line, "next") == 0 || strcmp(line, "start-next") == 0) {
            BackendOrder next_order;
            if (!get_next_order(&monitor, &next_order)) {
                puts("현재 접수 상태의 주문이 없습니다.");
                continue;
            }
            show_recommended_order(&monitor, &next_order);
            if (strcmp(line, "start-next") == 0) {
                if (!confirm_change("조리중으로 변경", next_order.order_id)) {
                    puts("상태 변경을 취소했습니다.");
                } else if (update_and_refresh(&monitor, next_order.order_id,
                                              "조리중")) {
                    puts("추천 주문의 조리를 시작했습니다.");
                }
            }
            continue;
        }

        char username[64];
        if (sscanf(line, "login %63s", username) == 1) {
            char password[128] = {0};
            if (!read_password(password, sizeof(password))) {
                puts("비밀번호 입력을 취소했습니다.");
                clear_secret(password, sizeof(password));
                continue;
            }
            pthread_mutex_lock(&monitor.mutex);
            bool success = backend_login(monitor.backend, username, password);
            if (success) {
                monitor.authenticated = true;
                refresh_locked(&monitor, false, 2);
                pthread_cond_signal(&monitor.wake);
            }
            pthread_mutex_unlock(&monitor.mutex);
            clear_secret(password, sizeof(password));
            if (success) {
                puts("백엔드 관리자 로그인 성공");
                print_cached_orders(&monitor, "active");
            } else printf("로그인 실패: %s\n", backend_last_error(monitor.backend));
            continue;
        }

        unsigned int id;
        if (sscanf(line, "detail %u", &id) == 1) {
            BackendOrderDetail detail;
            pthread_mutex_lock(&monitor.mutex);
            bool success = backend_fetch_order_detail(monitor.backend, id,
                                                       &detail);
            if (!success)
                printf("상세 조회 실패: %s\n",
                       backend_last_error(monitor.backend));
            pthread_mutex_unlock(&monitor.mutex);
            if (success) print_detail(&detail);
            continue;
        }
        if (sscanf(line, "start %u", &id) == 1) {
            if (!confirm_change("조리중으로 변경", id)) {
                puts("상태 변경을 취소했습니다.");
                continue;
            }
            if (update_and_refresh(&monitor, id, "조리중"))
                puts("백엔드 주문을 조리중으로 변경했습니다.");
            continue;
        }
        if (sscanf(line, "complete %u", &id) == 1) {
            if (!confirm_change("완료로 변경", id)) {
                puts("상태 변경을 취소했습니다.");
                continue;
            }
            if (update_and_refresh(&monitor, id, "완료"))
                puts("백엔드 주문을 완료로 변경했습니다.");
            continue;
        }
        if (sscanf(line, "cancel %u", &id) == 1) {
            if (!confirm_change("취소", id)) {
                puts("주문 취소를 중단했습니다.");
                continue;
            }
            bool success = cancel_and_refresh(&monitor, id);
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
