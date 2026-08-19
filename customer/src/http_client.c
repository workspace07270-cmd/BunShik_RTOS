#define _POSIX_C_SOURCE 200112L

#include "http_client.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define REQUEST_CAPACITY 1024
#define RAW_RESPONSE_CAPACITY 4096

/*
 * "http://localhost:8080" 같은 주소를 host="localhost", port=8080으로 나눕니다.
 * 이 예제는 학습용이므로 암호화된 https:// 주소는 처리하지 않습니다.
 */
int http_server_parse(const char *url, http_server_t *server) {
    const char *prefix = "http://";
    const char *host;
    const char *colon;
    size_t host_length;

    if (url == NULL || server == NULL
            || strncmp(url, prefix, strlen(prefix)) != 0) return -1;
    host = url + strlen(prefix);
    colon = strrchr(host, ':');
    if (colon == NULL) {
        /* 포트가 생략된 일반 HTTP 주소는 기본 포트 80을 사용합니다. */
        host_length = strlen(host);
        server->port = 80;
    } else {
        char *end;
        unsigned long port = strtoul(colon + 1, &end, 10);
        if (*end != '\0' || port == 0 || port > 65535) return -1;
        host_length = (size_t) (colon - host);
        server->port = (unsigned short) port;
    }
    if (host_length == 0 || host_length >= sizeof(server->host)) return -1;
    memcpy(server->host, host, host_length);
    server->host[host_length] = '\0';
    return 0;
}

static int connect_server(const http_server_t *server) {
    /*
     * getaddrinfo는 localhost나 도메인 이름을 소켓이 사용할 주소로 변환합니다.
     * 반환된 후보를 순서대로 연결해 보고 처음 성공한 소켓 번호(fd)를 돌려줍니다.
     */
    struct addrinfo hints = {0};
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[8];
    int fd = -1;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%u", server->port);
    if (getaddrinfo(server->host, port_text, &hints, &addresses) != 0) return -1;

    for (address = addresses; address != NULL; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype,
                address->ai_protocol);
        if (fd < 0) continue;
        /* 서버가 응답하지 않을 때 영원히 멈추지 않도록 5초 제한을 둡니다. */
        struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
        (void) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void) setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}

static int send_all(int fd, const char *data, size_t length) {
    /*
     * send()에 100바이트를 맡겨도 운영체제가 한 번에 40바이트만 보낼 수
     * 있습니다. sent에 지금까지 보낸 양을 누적하면서 남은 부분을 계속
     * 보내야 HTTP 요청이 중간에서 잘리지 않습니다.
     */
    size_t sent = 0;
    while (sent < length) {
        ssize_t count = send(fd, data + sent, length - sent, 0);
        /*
         * EINTR은 통신 단절이 아닙니다. FreeRTOS가 태스크 실행 시간을
         * 계산하려고 발생시킨 tick 신호가 send()를 잠시 깨웠다는 뜻이므로
         * 같은 위치부터 다시 호출합니다.
         */
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        sent += (size_t) count;
    }
    return 0;
}

/*
 * Spring Boot는 응답 크기를 미리 정하지 않고 여러 조각으로 보낼 수 있습니다.
 * 이것을 HTTP chunked 방식이라고 하며 각 조각 앞에는 16진수 길이가 붙습니다.
 *
 *   네트워크에서 받은 값: "2\r\n[]\r\n0\r\n\r\n"
 *   RTOS가 사용할 값:     "[]"
 *
 * 2는 다음 데이터가 2바이트라는 뜻이고, 마지막 0은 모든 조각이 끝났다는
 * 뜻입니다. 이 함수는 길이와 줄바꿈을 제거하고 JSON 본문만 이어 붙입니다.
 */
static int decode_chunked_body(const char *body, size_t body_length,
        char *body_out, size_t body_capacity, size_t *decoded_length) {
    const char *cursor = body;
    const char *end = body + body_length;
    size_t written = 0;

    while (cursor < end) {
        char *number_end;
        /* 조각 길이는 10진수가 아니라 16진수이므로 밑(base)에 16을 줍니다. */
        unsigned long chunk_size = strtoul(cursor, &number_end, 16);
        const char *line_end = strstr(cursor, "\r\n");
        if (number_end == cursor || line_end == NULL || number_end > line_end)
            return -1;

        cursor = line_end + 2;
        /* 크기 0은 실제 데이터가 아니라 전체 응답의 끝을 알리는 표시입니다. */
        if (chunk_size == 0) {
            if (body_capacity > 0) body_out[written] = '\0';
            *decoded_length = written;
            return 0;
        }

        /* 선언된 길이만큼 데이터가 도착했고 뒤에 줄바꿈이 있는지 확인합니다. */
        if ((size_t) (end - cursor) < chunk_size + 2
                || cursor[chunk_size] != '\r'
                || cursor[chunk_size + 1] != '\n') return -1;

        /* 출력 버퍼 크기를 넘지 않는 범위에서 이번 조각을 뒤에 붙입니다. */
        if (body_capacity > 0 && written < body_capacity - 1) {
            size_t available = body_capacity - 1 - written;
            size_t copied = chunk_size < available ? (size_t) chunk_size : available;
            memcpy(body_out + written, cursor, copied);
            written += copied;
        }
        cursor += chunk_size + 2;
    }
    return -1;
}

static int parse_response(char *raw, size_t raw_length, char *body_out,
        size_t body_capacity, http_response_t *response) {
    /* 응답 첫 줄(예: HTTP/1.1 201 Created)에서 숫자 상태 코드를 읽습니다. */
    if (raw_length < 12 || strncmp(raw, "HTTP/1.", 7) != 0) return -1;
    response->status_code = (raw[9] - '0') * 100
            + (raw[10] - '0') * 10 + (raw[11] - '0');
    /* HTTP 헤더와 본문은 빈 줄(\r\n\r\n)로 구분됩니다. */
    char *body = strstr(raw, "\r\n\r\n");
    if (body == NULL) return -1;
    body += 4;
    size_t length = raw_length - (size_t) (body - raw);

    /* 헤더가 chunked라고 알려 주면 위 함수를 이용해 JSON만 꺼냅니다. */
    if (strstr(raw, "Transfer-Encoding: chunked") != NULL) {
        return decode_chunked_body(body, length, body_out, body_capacity,
                &response->body_length);
    }

    /* Content-Length 방식이거나 연결 종료로 끝나는 일반 응답입니다. */
    if (body_capacity > 0) {
        size_t copied = length < body_capacity - 1 ? length : body_capacity - 1;
        memcpy(body_out, body, copied);
        body_out[copied] = '\0';
        response->body_length = copied;
    }
    return 0;
}

int http_request(const http_server_t *server, const char *method,
        const char *path, const char *json_body, char *response_body,
        size_t response_capacity, http_response_t *response) {
    char request[REQUEST_CAPACITY];
    char raw[RAW_RESPONSE_CAPACITY];
    size_t body_length = json_body == NULL ? 0 : strlen(json_body);
    size_t received = 0;
    /* 메서드, 경로, 헤더, JSON 본문을 하나의 HTTP/1.1 요청 문자열로 만듭니다. */
    int length = snprintf(request, sizeof(request),
            "%s %s HTTP/1.1\r\nHost: %s:%u\r\n"
            "Content-Type: application/json\r\nContent-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s",
            method, path, server->host, server->port, body_length,
            json_body == NULL ? "" : json_body);
    if (length < 0 || (size_t) length >= sizeof(request)) return -1;

    /* TCP 연결 -> 요청 전송 -> 응답 수신 -> 연결 종료 순서로 처리합니다. */
    int fd = connect_server(server);
    if (fd < 0) {
        fprintf(stderr, "[HTTP] %s:%u 연결 실패: %s\n",
                server->host, server->port, strerror(errno));
        return -1;
    }
    if (send_all(fd, request, (size_t) length) < 0) {
        fprintf(stderr, "[HTTP] 요청 전송 실패: %s %s (%s)\n",
                method, path, strerror(errno));
        close(fd);
        return -1;
    }
    /*
     * recv()는 서버 응답을 한 조각씩 가져옵니다. 한 번 호출했다고 전체
     * HTTP 응답이 오는 것은 아니므로 끝을 만날 때까지 raw 배열에 누적합니다.
     */
    while (received + 1 < sizeof(raw)) {
        ssize_t count = recv(fd, raw + received, sizeof(raw) - received - 1, 0);
        if (count == 0) break;
        if (count < 0) {
            /*
             * FreeRTOS POSIX scheduler는 일정 시간마다 태스크를 바꾸기 위해
             * Linux 신호로 tick을 만듭니다. recv() 도중 tick이 도착하면
             * errno가 EINTR이 됩니다. 데이터가 끊긴 것이 아니므로 오류를
             * 출력하지 않고 recv()를 다시 호출해야 합니다.
             */
            if (errno == EINTR) continue;
            /*
             * EAGAIN/EWOULDBLOCK은 5초 동안 추가 데이터가 없었다는 뜻입니다.
             * 이미 받은 내용이 있다면 그것을 HTTP 응답으로 해석해 봅니다.
             */
            if (received > 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            fprintf(stderr, "[HTTP] 응답 수신 실패: %s %s (%s)\n",
                    method, path, strerror(errno));
            close(fd);
            return -1;
        }
        received += (size_t) count;

        /*
         * chunked 응답의 마지막 조각(0)을 받았다면 서버가 소켓을 닫기를
         * 기다리지 않고 즉시 수신을 끝냅니다.
         */
        raw[received] = '\0';
        if (strstr(raw, "Transfer-Encoding: chunked") != NULL) {
            char *body_start = strstr(raw, "\r\n\r\n");
            if (body_start != NULL
                    && strstr(body_start + 4, "\r\n0\r\n\r\n") != NULL) break;
        }
    }
    close(fd);
    /* 받은 바이트를 일반 C 문자열로 사용할 수 있도록 끝에 \0을 붙입니다. */
    raw[received] = '\0';
    int parse_result = parse_response(raw, received, response_body,
            response_capacity, response);
    if (parse_result < 0) {
        fprintf(stderr,
                "[HTTP] 응답 해석 실패: %s %s, 받은 크기=%zu bytes\n",
                method, path, received);
    }
    return parse_result;
}
