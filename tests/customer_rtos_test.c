#include "customer_http_client.h"
#include "print_job.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    http_server_t server;
    assert(customer_http_server_parse("http://127.0.0.1:8080", &server) == 0);
    assert(strcmp(server.host, "127.0.0.1") == 0);
    assert(server.port == 8080);
    assert(customer_http_server_parse("https://127.0.0.1:8080", &server) == -1);

    PrintJob job;
    assert(print_job_parse_pending("{\"data\":[]}", &job) == 0);
    const char *receipt =
        "{\"data\":{\"id\":7,\"type\":\"RECEIPT\","
        "\"order_number\":\"260\",\"order_type\":\"매장\","
        "\"payment_method\":\"카드\",\"total_price\":9000,\"items\":["
        "{\"menu_name\":\"떡볶이\",\"quantity\":2,\"price\":4500}]}}";
    assert(print_job_parse_pending(receipt, &job) == 1);
    assert(job.id == 7);
    assert(job.type == PRINT_TYPE_RECEIPT);
    assert(strcmp(job.order_number, "260") == 0);
    assert(strcmp(job.order_type, "매장") == 0);
    assert(strcmp(job.payment_method, "카드") == 0);
    assert(job.item_count == 1);
    assert(job.items[0].quantity == 2);
    assert(job.total_price == 9000);

    const char *order_number =
        "{\"data\":[{\"id\":8,\"type\":\"ORDER_NUMBER\","
        "\"order_number\":\"306\",\"order_type\":\"포장\","
        "\"payment_method\":\"현금\",\"total_price\":4500,\"items\":[]}]}";
    assert(print_job_parse_pending(order_number, &job) == 1);
    assert(job.id == 8);
    assert(job.type == PRINT_TYPE_ORDER_NUMBER);
    assert(strcmp(job.order_number, "306") == 0);
    assert(job.item_count == 0);

    assert(print_job_parse_pending("{\"success\":true}", &job) == -1);
    assert(print_job_parse_pending(
        "{\"data\":[{\"id\":9,\"type\":\"RECEIPT\"}]}", &job) == -1);
    return 0;
}
