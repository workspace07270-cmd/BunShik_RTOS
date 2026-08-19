#define _POSIX_C_SOURCE 200809L

#include "admin_logger.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "/tmp/bunshik-admin-log-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    assert(admin_logger_init(directory));
    admin_log(ADMIN_LOG_INFO, "로그 테스트 ID=%d", 260);

    const char *path = admin_log_current_path();
    char saved_path[512];
    snprintf(saved_path, sizeof(saved_path), "%s", path);
    FILE *file = fopen(saved_path, "r");
    assert(file != NULL);
    char contents[2048] = {0};
    assert(fread(contents, 1, sizeof(contents) - 1, file) > 0);
    fclose(file);
    assert(strstr(contents, "INFO") != NULL);
    assert(strstr(contents, "로그 테스트 ID=260") != NULL);

    admin_logger_close();
    assert(unlink(saved_path) == 0);
    assert(rmdir(directory) == 0);
    puts("admin logger tests passed");
    return 0;
}
