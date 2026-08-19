#ifndef BUNSHIK_ADMIN_LOGGER_H
#define BUNSHIK_ADMIN_LOGGER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ADMIN_LOG_INFO,
    ADMIN_LOG_WARN,
    ADMIN_LOG_ERROR
} AdminLogLevel;

bool admin_logger_init(const char *directory);
void admin_logger_close(void);
void admin_log(AdminLogLevel level, const char *format, ...);
bool admin_log_tail(size_t line_count);
const char *admin_log_current_path(void);

#endif
