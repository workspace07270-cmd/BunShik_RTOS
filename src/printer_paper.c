#include "printer_paper.h"
#include "admin_logger.h"
#include "rtos_runtime.h"

/* 이 매수 이하로 남으면 소비할 때마다 관리자 로그에 경고를 남깁니다. */
#define PRINTER_PAPER_LOW_THRESHOLD 10

static RtosMutex paper_mutex;
static int paper_remaining;
static bool paper_low_reported;

void printer_paper_init(int initial_sheets)
{
    if (paper_mutex == NULL) rtos_mutex_init(&paper_mutex);

    rtos_mutex_lock(&paper_mutex);
    paper_remaining = initial_sheets > 0 ? initial_sheets : 0;
    paper_low_reported = paper_remaining <= PRINTER_PAPER_LOW_THRESHOLD;
    rtos_mutex_unlock(&paper_mutex);

    admin_log(ADMIN_LOG_INFO, "[Printer] 용지 %d매로 초기화되었습니다.",
            paper_remaining);
}

bool printer_paper_consume(int sheets_needed)
{
    if (sheets_needed <= 0) return true;

    rtos_mutex_lock(&paper_mutex);

    if (paper_remaining < sheets_needed) {
        int remaining = paper_remaining;
        rtos_mutex_unlock(&paper_mutex);
        admin_log(ADMIN_LOG_ERROR,
                "[Printer] 용지 부족으로 출력하지 못했습니다. 남은 용지: %d매",
                remaining);
        return false;
    }

    paper_remaining -= sheets_needed;
    int remaining = paper_remaining;
    bool report_low = remaining <= PRINTER_PAPER_LOW_THRESHOLD &&
                      !paper_low_reported;
    if (report_low) paper_low_reported = true;

    rtos_mutex_unlock(&paper_mutex);

    if (report_low) {
        admin_log(ADMIN_LOG_WARN,
                "[Printer] 용지가 얼마 남지 않았습니다. 남은 용지: %d매",
                remaining);
    }

    return true;
}

void printer_paper_refill(int sheets)
{
    if (sheets <= 0) return;

    rtos_mutex_lock(&paper_mutex);
    paper_remaining += sheets;
    int remaining = paper_remaining;
    if (remaining > PRINTER_PAPER_LOW_THRESHOLD) paper_low_reported = false;
    rtos_mutex_unlock(&paper_mutex);

    admin_log(ADMIN_LOG_INFO,
            "[Printer] 용지 %d매를 채웠습니다. 남은 용지: %d매", sheets, remaining);
}

int printer_paper_remaining(void)
{
    rtos_mutex_lock(&paper_mutex);
    int remaining = paper_remaining;
    rtos_mutex_unlock(&paper_mutex);
    return remaining;
}
