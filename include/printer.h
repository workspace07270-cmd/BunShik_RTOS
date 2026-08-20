#ifndef BUNSHIK_PRINTER_H
#define BUNSHIK_PRINTER_H

#include "print_job.h"

#include <stdbool.h>

/* 영수증(주문번호 + 메뉴명 + 가격 + 합계)을 콘솔에 출력합니다.
 * 용지가 부족하면 아무것도 출력하지 않고 false를 반환합니다. */
bool printer_print_receipt(const PrintJob *job);

/* 주문번호표(주문번호만 크게)를 콘솔에 출력합니다.
 * 용지가 부족하면 아무것도 출력하지 않고 false를 반환합니다. */
bool printer_print_order_number(const PrintJob *job);

#endif