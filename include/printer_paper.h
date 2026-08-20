#ifndef BUNSHIK_PRINTER_PAPER_H
#define BUNSHIK_PRINTER_PAPER_H

#include <stdbool.h>

/* 프린터에 남은 용지 매수를 이 값으로 초기화합니다. RTOS 시작 시 한 번만 호출합니다. */
void printer_paper_init(int initial_sheets);

/*
 * 용지를 sheets_needed 매만큼 소비합니다.
 * 남은 용지가 충분하면 차감하고 true, 부족하면 아무것도 바꾸지 않고 false를 반환합니다.
 * 여러 Task가 동시에 호출할 수 있어 내부적으로 Mutex로 보호합니다.
 */
bool printer_paper_consume(int sheets_needed);

/* 남은 용지를 채웁니다 (관리자가 용지를 갈아끼운 상황을 시뮬레이션). */
void printer_paper_refill(int sheets);

/* 현재 남은 용지 매수를 반환합니다. */
int printer_paper_remaining(void);

#endif