#include "backend_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *valid =
        "{\"success\":true,\"data\":["
        "{\"orderId\":260,\"orderNumber\":\"260\","
        "\"orderType\":\"매장\",\"totalPrice\":11500,"
        "\"orderStatus\":\"조리중\","
        "\"createdAt\":\"2026-08-18T15:40:45\","
        "\"paymentMethod\":\"카드\"},"
        "{\"orderId\":259,\"orderNumber\":\"259\","
        "\"orderType\":\"포장\",\"totalPrice\":4500,"
        "\"orderStatus\":\"접수\",\"paymentMethod\":null}]}";
    BackendOrder orders[2];
    size_t count = 0;
    char error[256] = {0};
    assert(backend_parse_orders_json(valid, orders, 2, &count,
                                     error, sizeof(error)));
    assert(count == 2);
    assert(orders[0].order_id == 260);
    assert(orders[0].total_price == 11500);
    assert(strcmp(orders[0].order_status, "조리중") == 0);
    assert(strcmp(orders[1].order_type, "포장") == 0);
    assert(orders[1].payment_method[0] == '\0');

    count = 0;
    assert(!backend_parse_orders_json(valid, orders, 1, &count,
                                      error, sizeof(error)));
    assert(strstr(error, "저장 한도") != NULL);

    const char *missing =
        "{\"data\":[{\"orderId\":1,\"orderNumber\":\"1\"}]}";
    count = 0;
    assert(!backend_parse_orders_json(missing, orders, 2, &count,
                                      error, sizeof(error)));
    assert(strstr(error, "필수 주문 필드") != NULL);

    const char *detail_json =
        "{\"success\":true,\"data\":{"
        "\"orderId\":260,\"orderNumber\":\"260\","
        "\"orderType\":\"매장\",\"totalPrice\":11500,"
        "\"orderStatus\":\"조리중\","
        "\"createdAt\":\"2026-08-18T15:40:45\","
        "\"paymentMethod\":\"카드\",\"items\":[{"
        "\"orderItemId\":501,\"menuName\":\"라면\","
        "\"quantity\":2,\"unitPrice\":4000,\"options\":[{"
        "\"optionId\":7,\"optionName\":\"치즈 추가\","
        "\"optionPrice\":500}],\"components\":[]},{"
        "\"orderItemId\":502,\"menuName\":\"김밥 세트\","
        "\"quantity\":1,\"unitPrice\":2500,\"options\":[],"
        "\"components\":[{\"componentMenuId\":12,"
        "\"componentMenuName\":\"콜라\"}]}]}}";
    BackendOrderDetail detail;
    assert(backend_parse_order_detail_json(detail_json, &detail,
                                           error, sizeof(error)));
    assert(detail.order.order_id == 260);
    assert(detail.item_count == 2);
    assert(strcmp(detail.items[0].menu_name, "라면") == 0);
    assert(detail.items[0].option_count == 1);
    assert(strcmp(detail.items[0].options[0].option_name, "치즈 추가") == 0);
    assert(detail.items[1].component_count == 1);
    assert(strcmp(detail.items[1].components[0].component_menu_name,
                  "콜라") == 0);

    const char *menus_json =
        "{\"success\":true,\"data\":[{\"menuId\":1,"
        "\"menuName\":\"떡볶이\",\"price\":4500,"
        "\"isAvailable\":true,\"isVisible\":true}]}";
    BackendCatalogItem catalog[2];
    count = 0;
    assert(backend_parse_menus_json(menus_json, catalog, 2, &count,
                                    error, sizeof(error)));
    assert(count == 1);
    assert(catalog[0].id == 1);
    assert(strcmp(catalog[0].name, "떡볶이") == 0);
    assert(catalog[0].price == 4500);
    assert(catalog[0].available && catalog[0].visible);

    const char *options_json =
        "{\"data\":[{\"optionId\":7,\"optionName\":\"치즈\","
        "\"optionPrice\":500,\"optionIsAvailable\":false,"
        "\"isVisible\":false}]}";
    count = 0;
    assert(backend_parse_options_json(options_json, catalog, 2, &count,
                                      error, sizeof(error)));
    assert(count == 1);
    assert(catalog[0].id == 7);
    assert(strcmp(catalog[0].name, "치즈") == 0);
    assert(catalog[0].price == 500);
    assert(!catalog[0].available && !catalog[0].visible);

    puts("backend parser tests passed");
    return 0;
}
