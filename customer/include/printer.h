#ifndef BUNSHIK_PRINTER_H
#define BUNSHIK_PRINTER_H

#include "print_job.h"

/* 영수증(주문번호 + 메뉴명 + 가격 + 합계)을 콘솔에 출력합니다. */
void printer_print_receipt(const PrintJob *job);

/* 주문번호표(주문번호만 크게)를 콘솔에 출력합니다. */
void printer_print_order_number(const PrintJob *job);

#endif