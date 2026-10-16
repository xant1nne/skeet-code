#ifndef SKEET_LOG_H
#define SKEET_LOG_H

void log_open(const char *path);
void log_close(void);

void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif
