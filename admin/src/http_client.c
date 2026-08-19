#define _POSIX_C_SOURCE 200809L

#include "http_client.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HTTP_MAX_RESPONSE (1024U * 1024U)

typedef struct {
    char host[256];
    char port[8];
    char prefix[256];
} ParsedUrl;

static int parse_url(const char *url, ParsedUrl *parsed)
{
    const char *start = url;
    if (strncmp(start, "http://", 7) != 0) return -1;
    start += 7;
    const char *path = strchr(start, '/');
    const char *end = path == NULL ? start + strlen(start) : path;
    const char *colon = NULL;
    for (const char *cursor = start; cursor < end; ++cursor) {
        if (*cursor == ':') colon = cursor;
    }
    const char *host_end = colon == NULL ? end : colon;
    size_t host_length = (size_t)(host_end - start);
    if (host_length == 0 || host_length >= sizeof(parsed->host)) return -1;
    memcpy(parsed->host, start, host_length);
    parsed->host[host_length] = '\0';
    snprintf(parsed->port, sizeof(parsed->port), "%s",
             colon == NULL ? "80" : colon + 1);
    if (path == NULL) parsed->prefix[0] = '\0';
    else snprintf(parsed->prefix, sizeof(parsed->prefix), "%s", path);
    return 0;
}

static int send_all(int socket_fd, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(socket_fd, data + sent, length - sent, 0);
        if (result <= 0) return -1;
        sent += (size_t)result;
    }
    return 0;
}

void http_response_free(HttpResponse *response)
{
    if (response == NULL) return;
    free(response->body);
    response->body = NULL;
    response->status_code = 0;
}

int http_request(const char *base_url, const char *method, const char *path,
                 const char *bearer_token, const char *json_body,
                 HttpResponse *response, char *error, size_t error_size)
{
    ParsedUrl url;
    memset(response, 0, sizeof(*response));
    if (parse_url(base_url, &url) != 0) {
        snprintf(error, error_size, "http:// 형식의 서버 주소가 필요합니다.");
        return -1;
    }

    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    int lookup = getaddrinfo(url.host, url.port, &hints, &addresses);
    if (lookup != 0) {
        snprintf(error, error_size, "서버 주소 확인 실패: %s", gai_strerror(lookup));
        return -1;
    }

    int socket_fd = -1;
    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype,
                           address->ai_protocol);
        if (socket_fd < 0) continue;
        struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) break;
        close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);
    if (socket_fd < 0) {
        snprintf(error, error_size, "백엔드 %s에 연결할 수 없습니다.", base_url);
        return -1;
    }

    const char *body = json_body == NULL ? "" : json_body;
    char target[512];
    snprintf(target, sizeof(target), "%s%s", url.prefix, path);
    size_t request_size = strlen(body) + strlen(target) +
                          (bearer_token ? strlen(bearer_token) : 0) + 768;
    char *request = malloc(request_size);
    if (request == NULL) {
        close(socket_fd);
        snprintf(error, error_size, "요청 메모리 할당 실패");
        return -1;
    }
    int length = snprintf(request, request_size,
        "%s %s HTTP/1.1\r\nHost: %s:%s\r\nAccept: application/json\r\n"
        "Content-Type: application/json\r\nConnection: close\r\n"
        "%s%s%sContent-Length: %zu\r\n\r\n%s",
        method, target, url.host, url.port,
        bearer_token ? "Authorization: Bearer " : "",
        bearer_token ? bearer_token : "", bearer_token ? "\r\n" : "",
        strlen(body), body);
    if (length < 0 || (size_t)length >= request_size ||
        send_all(socket_fd, request, (size_t)length) != 0) {
        free(request);
        close(socket_fd);
        snprintf(error, error_size, "HTTP 요청 전송 실패");
        return -1;
    }
    free(request);

    char *raw = malloc(HTTP_MAX_RESPONSE + 1U);
    if (raw == NULL) {
        close(socket_fd);
        snprintf(error, error_size, "응답 메모리 할당 실패");
        return -1;
    }
    size_t used = 0;
    while (used < HTTP_MAX_RESPONSE) {
        ssize_t received = recv(socket_fd, raw + used, HTTP_MAX_RESPONSE - used, 0);
        if (received == 0) break;
        if (received < 0) {
            if (errno == EINTR) continue;
            free(raw);
            close(socket_fd);
            snprintf(error, error_size, "HTTP 응답 수신 실패");
            return -1;
        }
        used += (size_t)received;
    }
    close(socket_fd);
    raw[used] = '\0';

    char *headers_end = strstr(raw, "\r\n\r\n");
    if (headers_end == NULL || sscanf(raw, "HTTP/%*s %d", &response->status_code) != 1) {
        free(raw);
        snprintf(error, error_size, "올바르지 않은 HTTP 응답");
        return -1;
    }
    char *body_start = headers_end + 4;
    size_t body_length = used - (size_t)(body_start - raw);
    response->body = malloc(body_length + 1U);
    if (response->body == NULL) {
        free(raw);
        snprintf(error, error_size, "응답 본문 메모리 할당 실패");
        return -1;
    }
    memcpy(response->body, body_start, body_length);
    response->body[body_length] = '\0';
    free(raw);
    return 0;
}
