#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int mkdir_p(const char *path) {
    size_t len = strlen(path);
    if (len == 0 || len >= 4096) {
        return -1;
    }
    char tmp[4096];
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/') {
            continue;
        }
        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static FILE *g_log;

static void log_write(const char *level, const char *fmt, va_list ap) {
    if (!g_log) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&now, &tm_buf);
    char stamp[32];
    if (!tm_info || strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tm_info) == 0) {
        stamp[0] = '\0';
    }
    fprintf(g_log, "%s %s ", stamp, level);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
}

void log_open(const char *path) {
    log_close();
    if (!path || !path[0]) {
        return;
    }
    char dir[4096];
    size_t len = strlen(path);
    if (len < sizeof dir) {
        memcpy(dir, path, len + 1);
        char *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = '\0';
            mkdir_p(dir);
        }
    }
    g_log = fopen(path, "a");
    if (!g_log) {
        fprintf(stderr, "skeet: cannot open log %s\n", path);
    }
}

void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void log_debug(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("debug", fmt, ap);
    va_end(ap);
}

void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("info", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("warn", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("error", fmt, ap);
    va_end(ap);
}
