#ifndef BUNSHIK_CUSTOMER_HTTP_CLIENT_H
#define BUNSHIK_CUSTOMER_HTTP_CLIENT_H

#include <stddef.h>

/* 접속할 서버의 호스트 이름과 TCP 포트입니다. */
typedef struct {
    char host[128];
    unsigned short port;
} http_server_t;

/* HTTP 응답에서 상위 코드가 사용할 핵심 정보만 보관합니다. */
typedef struct {
    int status_code;
    size_t body_length;
} http_response_t;

/* URL 문자열을 http_server_t 구조체로 변환합니다. 성공은 0, 실패는 -1입니다. */
int customer_http_server_parse(const char *url, http_server_t *server);
/* HTTP 요청을 한 번 보내고 상태 코드와 응답 본문을 돌려줍니다. */
int customer_http_request(const http_server_t *server, const char *method,
        const char *path, const char *json_body, char *response_body,
        size_t response_capacity, http_response_t *response);

#endif
