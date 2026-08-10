#pragma once
#include <string>

enum class LogLevel { Debug, Info, Warn, Error };

void log_set_level(LogLevel level);
void log_debug(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
