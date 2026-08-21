#define _POSIX_C_SOURCE 200809L

#include "admin_logger.h"
#include "printer.h"
#include "printer_paper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char log_directory[] = "/tmp/bunshik-printer-log-XXXXXX";
    assert(mkdtemp(log_directory) != NULL);
    assert(admin_logger_init(log_directory));

    PrintJob receipt = {0};
    receipt.id = 1;
    strcpy(receipt.order_number, "305");
    strcpy(receipt.order_type, "매장");
    strcpy(receipt.payment_method, "카드");
    receipt.total_price = 9000;
    receipt.item_count = 1;
    strcpy(receipt.items[0].name, "떡볶이");
    receipt.items[0].quantity = 2;
    receipt.items[0].price = 4500;

    PrintJob number = {0};
    number.id = 2;
    strcpy(number.order_number, "306");
    strcpy(number.order_type, "포장");

    FILE *capture = tmpfile();
    assert(capture != NULL);
    int saved_stdout = dup(STDOUT_FILENO);
    assert(saved_stdout >= 0);
    fflush(stdout);
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);

    printer_paper_init(2);
    assert(printer_paper_remaining() == 2);
    assert(printer_print_receipt(&receipt));
    assert(printer_paper_remaining() == 1);
    assert(printer_print_order_number(&number));
    assert(printer_paper_remaining() == 0);
    assert(!printer_print_receipt(&receipt));
    assert(printer_paper_remaining() == 0);

    printer_paper_refill(1);
    assert(printer_paper_remaining() == 1);
    assert(printer_print_order_number(&number));
    assert(printer_paper_remaining() == 0);

    fflush(stdout);
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);

    rewind(capture);
    char output[8192] = {0};
    assert(fread(output, 1, sizeof(output) - 1, capture) > 0);
    fclose(capture);
    assert(strstr(output, "맛있는 분식집") != NULL);
    assert(strstr(output, "떡볶이") != NULL);
    assert(strstr(output, "9,000원") != NULL);
    assert(strstr(output, "결제수단 : 카드") != NULL);
    assert(strstr(output, "305") != NULL);
    assert(strstr(output, "306") != NULL);

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s", admin_log_current_path());
    admin_logger_close();
    assert(unlink(log_path) == 0);
    assert(rmdir(log_directory) == 0);
    puts("printer tests passed");
    return 0;
}
