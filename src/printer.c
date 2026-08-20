#include "printer.h"
#include "printer_paper.h"

#include <stdio.h>

#define PAPER_SHEETS_PER_RECEIPT      1
#define PAPER_SHEETS_PER_ORDER_NUMBER 1
#define ITEM_NAME_COLUMN_WIDTH        20
#define STORE_NAME                    "맛있는 분식집"

static void print_line(char ch, int width) {
    for (int i = 0; i < width; i++) putchar(ch);
    putchar('\n');
}

/*
 * UTF-8 문자열이 터미널에서 차지하는 실제 칸 수를 계산합니다.
 * printf의 "%-20s"는 바이트 수 기준으로 정렬하는데, 한글은 한 글자가
 * 3바이트지만 화면에서는 2칸을 차지해서 영어/숫자와 섞이면 정렬이
 * 어긋납니다. 이 함수로 계산한 폭을 기준으로 직접 공백을 채웁니다.
 */
static int utf8_display_width(const char *text) {
    int width = 0;
    const unsigned char *p = (const unsigned char *) text;

    while (*p) {
        unsigned int codepoint;
        int extra_bytes;

        if ((*p & 0x80) == 0x00) {
            codepoint = *p;
            extra_bytes = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = *p & 0x1F;
            extra_bytes = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = *p & 0x0F;
            extra_bytes = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = *p & 0x07;
            extra_bytes = 3;
        } else {
            p++;
            continue;
        }

        p++;
        for (int i = 0; i < extra_bytes && (*p & 0xC0) == 0x80; i++) {
            codepoint = (codepoint << 6) | (*p & 0x3F);
            p++;
        }

        /* 한글 완성형/자모, 가나(일본어), CJK 한자 등은 화면에서 2칸을 차지합니다. */
        if ((codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
            (codepoint >= 0x1100 && codepoint <= 0x11FF) ||
            (codepoint >= 0x3130 && codepoint <= 0x318F) ||
            (codepoint >= 0x3040 && codepoint <= 0x30FF) ||
            (codepoint >= 0x4E00 && codepoint <= 0x9FFF)) {
            width += 2;
        } else {
            width += 1;
        }
    }

    return width;
}

/* text를 total_width 칸 안에서 정가운데에 오도록 앞에 공백을 채워 출력합니다. */
static void print_centered(const char *text, int total_width) {
    int width = utf8_display_width(text);
    int left_padding = (total_width - width) / 2;
    if (left_padding < 0) left_padding = 0;
    for (int i = 0; i < left_padding; i++) putchar(' ');
    printf("%s\n", text);
}

/* text를 target_width 칸이 되도록 오른쪽에 공백을 채워 왼쪽 정렬로 출력합니다. */
static void print_left_padded(const char *text, int target_width) {
    int width = utf8_display_width(text);
    printf("%s", text);
    for (int i = width; i < target_width; i++) putchar(' ');
}

bool printer_print_receipt(const PrintJob *job) {
    if (!printer_paper_consume(PAPER_SHEETS_PER_RECEIPT)) return false;

    printf("\n");
    print_line('=', 40);
    print_centered("BUNSHIK RECEIPT", 40);
    print_line('=', 40);
    printf("주문번호 : %s\n", job->order_number);
    print_line('-', 40);

    for (int i = 0; i < job->item_count; i++) {
        const PrintItem *item = &job->items[i];
        print_left_padded(item->name, ITEM_NAME_COLUMN_WIDTH);
        printf(" x%-2d  %8d원\n", item->quantity,
               item->price * item->quantity);
    }

    print_line('-', 40);
    print_left_padded("합계", 24);
    printf(" %8d원\n", job->total_price);
    print_line('=', 40);
    print_centered("이용해 주셔서 감사합니다", 40);
    print_centered(STORE_NAME, 40);
    print_line('=', 40);
    printf("\n");

    return true;
}

bool printer_print_order_number(const PrintJob *job) {
    if (!printer_paper_consume(PAPER_SHEETS_PER_ORDER_NUMBER)) return false;

    printf("\n");
    print_line('*', 24);
    print_centered("주 문 번 호", 24);
    printf("\n");
    print_centered(job->order_number, 24);
    printf("\n");
    print_line('*', 24);
    print_centered(STORE_NAME, 24);
    printf("\n");

    return true;
}