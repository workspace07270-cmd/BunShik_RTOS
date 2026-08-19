#define _POSIX_C_SOURCE 200809L

#include "admin_logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define LOG_PATH_LENGTH 512
#define LOG_LINE_LENGTH 1024
#define LOG_TAIL_MAX 1000

static pthread_mutex_t logger_mutex = PTHREAD_MUTEX_INITIALIZER;
static char logger_directory[LOG_PATH_LENGTH];
static char logger_path[LOG_PATH_LENGTH];
static bool logger_ready;

static bool update_log_path(void)
{
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    char date[16];
    strftime(date, sizeof(date), "%Y-%m-%d", &local_time);
    int length = snprintf(logger_path, sizeof(logger_path),
                          "%s/admin-%s.log", logger_directory, date);
    return length > 0 && (size_t)length < sizeof(logger_path);
}

static const char *level_name(AdminLogLevel level)
{
    switch (level) {
    case ADMIN_LOG_WARN: return "WARN";
    case ADMIN_LOG_ERROR: return "ERROR";
    default: return "INFO";
    }
}

bool admin_logger_init(const char *directory)
{
    if (directory == NULL || directory[0] == '\0') return false;
    pthread_mutex_lock(&logger_mutex);
    snprintf(logger_directory, sizeof(logger_directory), "%s", directory);
    if (mkdir(logger_directory, 0755) != 0 && errno != EEXIST) {
        pthread_mutex_unlock(&logger_mutex);
        return false;
    }
    logger_ready = update_log_path();
    pthread_mutex_unlock(&logger_mutex);
    return logger_ready;
}

void admin_logger_close(void)
{
    pthread_mutex_lock(&logger_mutex);
    logger_ready = false;
    pthread_mutex_unlock(&logger_mutex);
}

void admin_log(AdminLogLevel level, const char *format, ...)
{
    pthread_mutex_lock(&logger_mutex);
    if (!logger_ready || !update_log_path()) {
        pthread_mutex_unlock(&logger_mutex);
        return;
    }
    FILE *file = fopen(logger_path, "a");
    if (file == NULL) {
        pthread_mutex_unlock(&logger_mutex);
        return;
    }
    time_t now = time(NULL);
    struct tm local_time;
    localtime_r(&now, &local_time);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
    fprintf(file, "%s %-5s ", timestamp, level_name(level));
    va_list arguments;
    va_start(arguments, format);
    vfprintf(file, format, arguments);
    va_end(arguments);
    fputc('\n', file);
    fclose(file);
    pthread_mutex_unlock(&logger_mutex);
}

bool admin_log_tail(size_t line_count)
{
    if (line_count == 0 || line_count > LOG_TAIL_MAX) return false;
    pthread_mutex_lock(&logger_mutex);
    if (!logger_ready || !update_log_path()) {
        pthread_mutex_unlock(&logger_mutex);
        return false;
    }
    FILE *file = fopen(logger_path, "r");
    if (file == NULL) {
        pthread_mutex_unlock(&logger_mutex);
        return false;
    }
    char **lines = calloc(line_count, sizeof(*lines));
    if (lines == NULL) {
        fclose(file);
        pthread_mutex_unlock(&logger_mutex);
        return false;
    }
    size_t total = 0;
    char buffer[LOG_LINE_LENGTH];
    while (fgets(buffer, sizeof(buffer), file)) {
        size_t slot = total % line_count;
        free(lines[slot]);
        lines[slot] = strdup(buffer);
        ++total;
    }
    fclose(file);
    size_t shown = total < line_count ? total : line_count;
    size_t start = total < line_count ? 0 : total % line_count;
    for (size_t index = 0; index < shown; ++index) {
        size_t slot = (start + index) % line_count;
        if (lines[slot]) fputs(lines[slot], stdout);
    }
    for (size_t index = 0; index < line_count; ++index) free(lines[index]);
    free(lines);
    pthread_mutex_unlock(&logger_mutex);
    return true;
}

const char *admin_log_current_path(void)
{
    return logger_path;
}
