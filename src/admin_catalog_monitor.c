#include "admin_catalog_monitor.h"

#include "admin_logger.h"
#include "backend_client.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CATALOG_POLL_MS 10000
#define CATALOG_RETRY_MS 5000

typedef struct {
    char backend_url[256];
    char username[64];
    char password[128];
    BackendCatalogItem previous_menus[BACKEND_MAX_CATALOG_ITEMS];
    BackendCatalogItem current_menus[BACKEND_MAX_CATALOG_ITEMS];
    BackendCatalogItem previous_options[BACKEND_MAX_CATALOG_ITEMS];
    BackendCatalogItem current_options[BACKEND_MAX_CATALOG_ITEMS];
} CatalogMonitor;

static CatalogMonitor monitor;

static const BackendCatalogItem *find_item(const BackendCatalogItem *items,
                                           size_t count, unsigned int id)
{
    for (size_t index = 0; index < count; ++index)
        if (items[index].id == id) return &items[index];
    return NULL;
}

static void report_changes(const char *kind,
                           const BackendCatalogItem *previous,
                           size_t previous_count,
                           const BackendCatalogItem *current,
                           size_t current_count)
{
    for (size_t index = 0; index < current_count; ++index) {
        const BackendCatalogItem *old = find_item(previous, previous_count,
                                                  current[index].id);
        if (old == NULL) {
            printf("[AdminCatalogTask] %s 등록: ID=%u 이름=%s 가격=%u원\n",
                   kind, current[index].id, current[index].name,
                   current[index].price);
            admin_log(ADMIN_LOG_INFO, "%s 등록: ID=%u 이름=%s 가격=%u원",
                      kind, current[index].id, current[index].name,
                      current[index].price);
            continue;
        }
        if (strcmp(old->name, current[index].name) != 0) {
            printf("[AdminCatalogTask] %s 이름 변경: ID=%u %s -> %s\n",
                   kind, current[index].id, old->name, current[index].name);
            admin_log(ADMIN_LOG_INFO, "%s 이름 변경: ID=%u %s -> %s",
                      kind, current[index].id, old->name, current[index].name);
        }
        if (old->price != current[index].price) {
            printf("[AdminCatalogTask] %s 가격 변경: %s %u원 -> %u원\n",
                   kind, current[index].name, old->price,
                   current[index].price);
            admin_log(ADMIN_LOG_INFO, "%s 가격 변경: ID=%u %u원 -> %u원",
                      kind, current[index].id, old->price,
                      current[index].price);
        }
        /* isVisible은 관리자의 판매 중단/재개 상태입니다. 판매 중단 API는
         * available도 함께 false로 바꾸므로 이 경우 품절 알림을 중복 출력하지
         * 않고 판매 상태 변경 하나만 알립니다. */
        if (old->visible != current[index].visible) {
            printf("[AdminCatalogTask] %s 판매 %s: %s\n", kind,
                   current[index].visible ? "재개" : "중단",
                   current[index].name);
            admin_log(ADMIN_LOG_INFO, "%s 판매 %s: ID=%u 이름=%s", kind,
                      current[index].visible ? "재개" : "중단",
                      current[index].id, current[index].name);
        /* isAvailable만 바뀌고 화면에 보이는 상품이면 품절/품절 해제입니다. */
        } else if (current[index].visible &&
                   old->available != current[index].available) {
            printf("[AdminCatalogTask] %s %s: %s\n", kind,
                   current[index].available ? "품절 해제" : "품절",
                   current[index].name);
            admin_log(ADMIN_LOG_INFO, "%s %s: ID=%u 이름=%s", kind,
                      current[index].available ? "품절 해제" : "품절",
                      current[index].id, current[index].name);
        }
    }

    for (size_t index = 0; index < previous_count; ++index) {
        if (find_item(current, current_count, previous[index].id) == NULL) {
            printf("[AdminCatalogTask] %s 목록에서 제거됨: ID=%u 이름=%s\n",
                   kind, previous[index].id, previous[index].name);
            admin_log(ADMIN_LOG_WARN, "%s 목록에서 제거됨: ID=%u 이름=%s",
                      kind, previous[index].id, previous[index].name);
        }
    }
}

static void admin_catalog_task(void *parameter)
{
    (void)parameter;
    BackendClient *backend = backend_client_create(monitor.backend_url);
    configASSERT(backend != NULL);
    size_t previous_menu_count = 0;
    size_t previous_option_count = 0;
    bool has_snapshot = false;

    for (;;) {
        if (!backend_is_authenticated(backend) &&
                !backend_login(backend, monitor.username, monitor.password)) {
            fprintf(stderr,
                    "[AdminCatalogTask] 로그인 실패: %s, %d ms 후 재시도\n",
                    backend_last_error(backend), CATALOG_RETRY_MS);
            vTaskDelay(pdMS_TO_TICKS(CATALOG_RETRY_MS));
            continue;
        }

        size_t menu_count = 0;
        size_t option_count = 0;
        bool menus_ok = backend_fetch_menus(backend, monitor.current_menus,
                BACKEND_MAX_CATALOG_ITEMS, &menu_count);
        bool options_ok = menus_ok && backend_fetch_options(backend,
                monitor.current_options, BACKEND_MAX_CATALOG_ITEMS,
                &option_count);
        if (!menus_ok || !options_ok) {
            fprintf(stderr, "[AdminCatalogTask] 메뉴·옵션 조회 실패: %s\n",
                    backend_last_error(backend));
            if (backend_error_kind(backend) == BACKEND_ERROR_AUTH)
                backend_logout(backend);
            vTaskDelay(pdMS_TO_TICKS(CATALOG_RETRY_MS));
            continue;
        }

        if (has_snapshot) {
            report_changes("메뉴", monitor.previous_menus,
                    previous_menu_count, monitor.current_menus, menu_count);
            report_changes("옵션", monitor.previous_options,
                    previous_option_count, monitor.current_options,
                    option_count);
        } else {
            printf("[AdminCatalogTask] 메뉴 %zu개·옵션 %zu개 기준 상태 저장\n",
                   menu_count, option_count);
            admin_log(ADMIN_LOG_INFO, "메뉴·옵션 기준 상태 저장: 메뉴=%zu 옵션=%zu",
                      menu_count, option_count);
            has_snapshot = true;
        }
        memcpy(monitor.previous_menus, monitor.current_menus,
               menu_count * sizeof(monitor.previous_menus[0]));
        memcpy(monitor.previous_options, monitor.current_options,
               option_count * sizeof(monitor.previous_options[0]));
        previous_menu_count = menu_count;
        previous_option_count = option_count;
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(CATALOG_POLL_MS));
    }
}

int admin_catalog_monitor_start(const char *backend_url, const char *username,
                                const char *password)
{
    if (backend_url == NULL || username == NULL || password == NULL)
        return EXIT_FAILURE;
    snprintf(monitor.backend_url, sizeof(monitor.backend_url), "%s",
             backend_url);
    snprintf(monitor.username, sizeof(monitor.username), "%s", username);
    snprintf(monitor.password, sizeof(monitor.password), "%s", password);
    return xTaskCreate(admin_catalog_task, "AdminCatalogTask", 8192, NULL, 2,
                       NULL) == pdPASS ? EXIT_SUCCESS : EXIT_FAILURE;
}
