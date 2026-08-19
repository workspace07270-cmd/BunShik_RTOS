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
        "\"order_number\":\"260\",\"total_price\":9000,\"items\":["
        "{\"menu_name\":\"떡볶이\",\"quantity\":2,\"price\":4500}]}}";
    assert(print_job_parse_pending(receipt, &job) == 1);
    assert(job.id == 7);
    assert(job.type == PRINT_TYPE_RECEIPT);
    assert(strcmp(job.order_number, "260") == 0);
    assert(job.item_count == 1);
    assert(job.items[0].quantity == 2);
    assert(job.total_price == 9000);
    return 0;
}
