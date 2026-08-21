#define _POSIX_C_SOURCE 200809L
#include "printer.h"
#include "printer_paper.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define PAPER_SHEETS_PER_RECEIPT      1
#define PAPER_SHEETS_PER_ORDER_NUMBER 1

#define RECEIPT_WIDTH 40
#define TICKET_WIDTH  24

#define ITEM_NAME_COLUMN_WIDTH   14
#define ITEM_QTY_COLUMN_WIDTH     6
#define ITEM_UNIT_COLUMN_WIDTH    9
#define ITEM_AMOUNT_COLUMN_WIDTH 11

#define STORE_NAME    "맛있는 분식집"
#define STORE_ADDRESS "서울시 마포구 신촌로 104"        
#define STORE_TEL     "Tel. 02-1234-5678"           
#define STORE_BIZ_NO  "사업자등록번호 123-45-67890"    

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

/* text를 target_width 칸이 되도록 오른쪽에 공백을 채워 왼쪽 정렬로 출력합니다. */
static void print_left_padded(const char *text, int target_width) {
    int width = utf8_display_width(text);
    printf("%s", text);
    for (int i = width; i < target_width; i++) putchar(' ');
}

/* text를 target_width 칸이 되도록 왼쪽에 공백을 채워 오른쪽 정렬로 출력합니다. */
static void print_right_padded(const char *text, int target_width) {
    int width = utf8_display_width(text);
    for (int i = width; i < target_width; i++) putchar(' ');
    printf("%s", text);
}

/* text를 total_width 칸 안에서 정가운데에 오도록 앞에 공백을 채워 출력합니다. */
static void print_centered(const char *text, int total_width) {
    int width = utf8_display_width(text);
    int left_padding = (total_width - width) / 2;
    if (left_padding < 0) left_padding = 0;
    for (int i = 0; i < left_padding; i++) putchar(' ');
    printf("%s\n", text);
}

/* 정수를 "1,234" 형태의 천 단위 콤마 문자열로 바꿉니다. */
static void format_amount(int amount, char *buffer, size_t size) {
    char digits[16];
    int len = snprintf(digits, sizeof(digits), "%d", amount);
    if (len <= 0 || (size_t) len >= sizeof(digits)) {
        snprintf(buffer, size, "0");
        return;
    }

    size_t out = 0;
    for (int i = 0; i < len && out + 1 < size; i++) {
        if (i > 0 && (len - i) % 3 == 0) buffer[out++] = ',';
        if (out + 1 < size) buffer[out++] = digits[i];
    }
    buffer[out] = '\0';
}

/* 현재 시각을 "YYYY-MM-DD HH:MM" 형태로 buffer에 채웁니다. */
static void format_now(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    strftime(buffer, size, "%Y-%m-%d %H:%M", &local_time);
}

/* 가짜 바코드 모양으로 하단 장식선을 출력합니다. */
static void print_barcode(void) {
    const char *bars =
        "| || ||| |  | || ||||| || |  || | ||| || | | ||";
    printf("%.*s\n", RECEIPT_WIDTH, bars);
}

bool printer_print_receipt(const PrintJob *job) {
    if (!printer_paper_consume(PAPER_SHEETS_PER_RECEIPT)) return false;

    char timestamp[24];
    format_now(timestamp, sizeof(timestamp));

    printf("\n");
    print_line('=', RECEIPT_WIDTH);
    printf("%s\n", STORE_NAME);
    printf("%s\n", STORE_ADDRESS);
    printf("%s\n", STORE_TEL);
    print_line('=', RECEIPT_WIDTH);
    printf("판매시간 : %s\n", timestamp);
    char order_label[48];
    snprintf(order_label, sizeof(order_label), "주문번호 : %s", job->order_number);
    char type_label[32];
    snprintf(type_label, sizeof(type_label), "[%s]", job->order_type);

    print_left_padded(order_label, RECEIPT_WIDTH - utf8_display_width(type_label));
    printf("%s\n", type_label);
    print_line('-', RECEIPT_WIDTH);

    print_left_padded("품목", ITEM_NAME_COLUMN_WIDTH);
    print_right_padded("수량", ITEM_QTY_COLUMN_WIDTH);
    print_right_padded("단가", ITEM_UNIT_COLUMN_WIDTH);
    print_right_padded("금액", ITEM_AMOUNT_COLUMN_WIDTH);
    printf("\n");
    print_line('-', RECEIPT_WIDTH);

    for (int i = 0; i < job->item_count; i++) {
        const PrintItem *item = &job->items[i];
        char qty[8], unit[16], amount[16];
        snprintf(qty, sizeof(qty), "%d", item->quantity);
        format_amount(item->price, unit, sizeof(unit));
        format_amount(item->price * item->quantity, amount, sizeof(amount));

        print_left_padded(item->name, ITEM_NAME_COLUMN_WIDTH);
        print_right_padded(qty, ITEM_QTY_COLUMN_WIDTH);
        print_right_padded(unit, ITEM_UNIT_COLUMN_WIDTH);
        print_right_padded(amount, ITEM_AMOUNT_COLUMN_WIDTH);
        printf("\n");
    }

    print_line('-', RECEIPT_WIDTH);

    char total[16], total_label[24];
    format_amount(job->total_price, total, sizeof(total));
    snprintf(total_label, sizeof(total_label), "%s원", total);
    print_left_padded("합  계", 28);
    print_right_padded(total_label, RECEIPT_WIDTH - 28);
    printf("\n");
    printf("결제수단 : %s\n", job->payment_method);

    print_line('=', RECEIPT_WIDTH);

    print_line('=', RECEIPT_WIDTH);
    print_centered("이용해 주셔서 감사합니다", RECEIPT_WIDTH);
    print_centered(STORE_BIZ_NO, RECEIPT_WIDTH);
    print_barcode();
    printf("\n");

    return true;
}

bool printer_print_order_number(const PrintJob *job) {
    if (!printer_paper_consume(PAPER_SHEETS_PER_ORDER_NUMBER)) return false;

    char timestamp[24];
    format_now(timestamp, sizeof(timestamp));

    char order_line[48];
    snprintf(order_line, sizeof(order_line), "%s [%s]",
             job->order_number, job->order_type);

    printf("\n");
    print_line('*', TICKET_WIDTH);
    print_centered("주 문 번 호", TICKET_WIDTH);
    printf("\n");
    print_centered(timestamp, TICKET_WIDTH);
    printf("\n");
    print_centered(order_line, TICKET_WIDTH);
    printf("\n");
    print_line('*', TICKET_WIDTH);
    print_centered(STORE_NAME, TICKET_WIDTH);
    printf("\n");

    return true;
}