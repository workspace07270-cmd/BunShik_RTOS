#include "admin_mode.h"

#include "admin_logger.h"
#include "backend_client.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADMIN_POLL_MS 10000
#define ADMIN_RETRY_MS 5000
#define ADMIN_EVENT_QUEUE_LENGTH 32

typedef enum {
    ADMIN_EVENT_CONNECTED,
    ADMIN_EVENT_CONNECTION_LOST,
    ADMIN_EVENT_NEW_ORDER,
    ADMIN_EVENT_STATUS_CHANGED
} AdminEventType;

typedef struct {
    AdminEventType type;
    BackendOrder order;
    char previous_status[BACKEND_TEXT_LENGTH];
    size_t order_count;
} AdminEvent;

typedef struct {
    char backend_url[256];
    char username[64];
    char password[128];
    QueueHandle_t events;
    BackendOrder previous[BACKEND_MAX_ORDERS];
    BackendOrder current[BACKEND_MAX_ORDERS];
} AdminService;

static AdminService service;

static const BackendOrder *find_order(const BackendOrder *orders, size_t count,
        unsigned int order_id)
{
    for (size_t index = 0; index < count; ++index)
        if (orders[index].order_id == order_id) return &orders[index];
    return NULL;
}

static void send_event(const AdminEvent *event)
{
    if (xQueueSend(service.events, event, 0) != pdPASS)
        admin_log(ADMIN_LOG_WARN, "관리자 이벤트 큐 가득 참: type=%d", event->type);
}

static void publish_changes(const BackendOrder *previous, size_t previous_count,
        const BackendOrder *current, size_t current_count, bool has_snapshot)
{
    if (!has_snapshot) return;
    for (size_t index = 0; index < current_count; ++index) {
        const BackendOrder *old = find_order(previous, previous_count,
                current[index].order_id);
        AdminEvent event = {.order = current[index]};
        if (old == NULL) {
            event.type = ADMIN_EVENT_NEW_ORDER;
            send_event(&event);
        } else if (strcmp(old->order_status, current[index].order_status) != 0) {
            event.type = ADMIN_EVENT_STATUS_CHANGED;
            snprintf(event.previous_status, sizeof(event.previous_status), "%s",
                    old->order_status);
            send_event(&event);
        }
    }
}

static void admin_event_task(void *parameter)
{
    (void)parameter;
    AdminEvent event;
    for (;;) {
        if (xQueueReceive(service.events, &event, portMAX_DELAY) != pdTRUE)
            continue;
        switch (event.type) {
        case ADMIN_EVENT_CONNECTED:
            printf("[AdminOrderPollTask] 백엔드 연결 완료, 주문 %zu건 동기화\n",
                    event.order_count);
            admin_log(ADMIN_LOG_INFO, "백엔드 연결 완료: 주문=%zu건",
                    event.order_count);
            break;
        case ADMIN_EVENT_CONNECTION_LOST:
            puts("[AdminOrderPollTask] 백엔드 연결 끊김, 자동 재연결 중");
            admin_log(ADMIN_LOG_ERROR, "백엔드 연결 끊김");
            break;
        case ADMIN_EVENT_NEW_ORDER:
            printf("[AdminEventTask] 신규 주문: ID=%u 번호=%s 상태=%s 금액=%u원\n",
                    event.order.order_id, event.order.order_number,
                    event.order.order_status, event.order.total_price);
            admin_log(ADMIN_LOG_INFO, "신규 주문: ID=%u 번호=%s 상태=%s",
                    event.order.order_id, event.order.order_number,
                    event.order.order_status);
            break;
        case ADMIN_EVENT_STATUS_CHANGED:
            printf("[AdminEventTask] 주문 상태 변경: ID=%u %s -> %s\n",
                    event.order.order_id, event.previous_status,
                    event.order.order_status);
            admin_log(ADMIN_LOG_INFO, "주문 상태 변경: ID=%u %s -> %s",
                    event.order.order_id, event.previous_status,
                    event.order.order_status);
            break;
        }
        fflush(stdout);
    }
}

static void admin_order_poll_task(void *parameter)
{
    (void)parameter;
    BackendClient *backend = backend_client_create(service.backend_url);
    configASSERT(backend != NULL);
    size_t previous_count = 0;
    bool has_snapshot = false;
    bool connected = false;

    for (;;) {
        if (!backend_is_authenticated(backend)) {
            if (!backend_login(backend, service.username, service.password)) {
                fprintf(stderr, "[AdminOrderPollTask] 로그인 실패: %s, %d ms 후 재시도\n",
                        backend_last_error(backend), ADMIN_RETRY_MS);
                vTaskDelay(pdMS_TO_TICKS(ADMIN_RETRY_MS));
                continue;
            }
            puts("[AdminOrderPollTask] 관리자 자동 로그인 성공");
        }

        size_t current_count = 0;
        if (!backend_fetch_orders(backend, service.current, BACKEND_MAX_ORDERS,
                &current_count)) {
            if (connected) {
                AdminEvent event = {.type = ADMIN_EVENT_CONNECTION_LOST};
                send_event(&event);
                connected = false;
            }
            if (backend_error_kind(backend) == BACKEND_ERROR_AUTH)
                backend_logout(backend);
            vTaskDelay(pdMS_TO_TICKS(ADMIN_RETRY_MS));
            continue;
        }

        if (!connected) {
            AdminEvent event = {
                .type = ADMIN_EVENT_CONNECTED,
                .order_count = current_count
            };
            send_event(&event);
            connected = true;
        }
        publish_changes(service.previous, previous_count, service.current,
                current_count, has_snapshot);
        memcpy(service.previous, service.current,
                current_count * sizeof(service.current[0]));
        previous_count = current_count;
        has_snapshot = true;
        vTaskDelay(pdMS_TO_TICKS(ADMIN_POLL_MS));
    }
}

int admin_tasks_start(const char *backend_url, const char *username,
        const char *password)
{
    if (backend_url == NULL || username == NULL || password == NULL
            || username[0] == '\0' || password[0] == '\0') return EXIT_FAILURE;
    snprintf(service.backend_url, sizeof(service.backend_url), "%s", backend_url);
    snprintf(service.username, sizeof(service.username), "%s", username);
    snprintf(service.password, sizeof(service.password), "%s", password);

    const char *log_directory = getenv("BUNSHIK_LOG_DIR");
    if (!admin_logger_init(log_directory != NULL && log_directory[0] != '\0'
            ? log_directory : "logs")) return EXIT_FAILURE;

    service.events = xQueueCreate(ADMIN_EVENT_QUEUE_LENGTH, sizeof(AdminEvent));
    if (service.events == NULL) return EXIT_FAILURE;
    if (xTaskCreate(admin_event_task, "AdminEventTask", 4096, NULL, 2, NULL)
            != pdPASS) return EXIT_FAILURE;
    if (xTaskCreate(admin_order_poll_task, "AdminOrderPollTask", 8192, NULL, 3,
            NULL) != pdPASS) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
