#include "printer.h"

#include <stdio.h>

static void print_line(char ch, int width) {
    for (int i = 0; i < width; i++) putchar(ch);
    putchar('\n');
}

void printer_print_receipt(const PrintJob *job) {
    printf("\n");
    print_line('=', 40);
    printf("           BUNSHIK RECEIPT\n");
    print_line('=', 40);
    printf("주문번호 : %s\n", job->order_number);
    print_line('-', 40);

    for (int i = 0; i < job->item_count; i++) {
        const PrintItem *item = &job->items[i];
        printf("%-20s x%-2d  %8d원\n", item->name, item->quantity,
               item->price * item->quantity);
    }

    print_line('-', 40);
    printf("%-24s %8d원\n", "합계", job->total_price);
    print_line('=', 40);
    printf("      이용해 주셔서 감사합니다\n");
    print_line('=', 40);
    printf("\n");
}

void printer_print_order_number(const PrintJob *job) {
    printf("\n");
    print_line('*', 24);
    printf("    주 문 번 호\n");
    printf("\n");
    printf("      %s\n", job->order_number);
    printf("\n");
    print_line('*', 24);
    printf("\n");
}
