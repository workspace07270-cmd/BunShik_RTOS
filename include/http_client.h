#ifndef BUNSHIK_HTTP_CLIENT_H
#define BUNSHIK_HTTP_CLIENT_H

#include <stddef.h>

typedef struct {
    int status_code;
    char *body;
} HttpResponse;

int http_request(const char *base_url, const char *method, const char *path,
                 const char *bearer_token, const char *json_body,
                 HttpResponse *response, char *error, size_t error_size);
void http_response_free(HttpResponse *response);

#endif
