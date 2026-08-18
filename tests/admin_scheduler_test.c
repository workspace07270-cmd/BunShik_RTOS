#include "scheduler.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    Scheduler *manual = scheduler_create();
    assert(manual != NULL);
    assert(scheduler_submit(manual, "M-001", "떡볶이", 1, 10) == 1);
    assert(!scheduler_complete_order(manual, 1));
    assert(scheduler_start_order(manual, 1));
    assert(!scheduler_start_order(manual, 1));
    assert(scheduler_complete_order(manual, 1));
    assert(!scheduler_cancel(manual, 1));

    Order completed;
    assert(scheduler_get_order(manual, 1, &completed));
    assert(completed.status == ORDER_COMPLETED);
    assert(!scheduler_get_order(manual, 999, &completed));
    scheduler_destroy(manual);

    Scheduler *scheduler = scheduler_create();
    assert(scheduler != NULL);

    assert(scheduler_submit(scheduler, "A-001", "떡볶이", 1, 10) == 1);
    assert(scheduler_submit(scheduler, "A-002", "라면", 5, 10) == 2);
    assert(scheduler_submit(scheduler, "A-003", "김밥", 3, 10) == 3);
    assert(scheduler_cancel(scheduler, 3));
    assert(!scheduler_cancel(scheduler, 999));

    assert(scheduler_start(scheduler));
    assert(scheduler_wait_idle(scheduler, 2000));

    Order orders[3];
    assert(scheduler_snapshot(scheduler, orders, 3) == 3);
    assert(orders[0].status == ORDER_COMPLETED);
    assert(orders[1].status == ORDER_COMPLETED);
    assert(orders[2].status == ORDER_CANCELED);

    scheduler_destroy(scheduler);
    puts("admin scheduler tests passed");
    return 0;
}
