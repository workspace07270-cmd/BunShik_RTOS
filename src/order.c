#include "order.h"

const char *order_status_name(OrderStatus status)
{
    switch (status) {
    case ORDER_RECEIVED:
        return "접수";
    case ORDER_COOKING:
        return "조리중";
    case ORDER_COMPLETED:
        return "완료";
    case ORDER_CANCELED:
        return "취소";
    default:
        return "알 수 없음";
    }
}
