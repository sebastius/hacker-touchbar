#include "util/log.hpp"
#include <cstdio>
#include <cstdarg>
#include <ctime>

static LogLevel g_level = LogLevel::Info;

void log_set_level(LogLevel level) {
    g_level = level;
}

static void log_msg(const char* prefix, const char* fmt, va_list args) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "%02d:%02d:%02d.%03ld %s ",
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000, prefix);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void log_debug(const char* fmt, ...) {
    if (g_level > LogLevel::Debug) return;
    va_list args; va_start(args, fmt); log_msg("[DBG]", fmt, args); va_end(args);
}

void log_info(const char* fmt, ...) {
    if (g_level > LogLevel::Info) return;
    va_list args; va_start(args, fmt); log_msg("[INF]", fmt, args); va_end(args);
}

void log_warn(const char* fmt, ...) {
    if (g_level > LogLevel::Warn) return;
    va_list args; va_start(args, fmt); log_msg("[WRN]", fmt, args); va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args; va_start(args, fmt); log_msg("[ERR]", fmt, args); va_end(args);
}
