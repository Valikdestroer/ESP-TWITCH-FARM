#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <vector>

enum LogLevel {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_DEBUG
};

struct LogMessage {
    uint32_t timestamp;
    LogLevel level;
    char text[256];
};

class Logger {
public:
    static void init();
    static void log(LogLevel level, const char* format, ...);
    static void info(const char* format, ...);
    static void warn(const char* format, ...);
    static void error(const char* format, ...);
    static void debug(const char* format, ...);

    static String getLogsJson();
    static void clearLogs();

private:
    static void vlog(LogLevel level, const char* format, va_list args);
    static const size_t MAX_LOGS = 60;
    static LogMessage logs[MAX_LOGS];
    static size_t head;
    static size_t count;
    static portMUX_TYPE mux;
};

#endif // LOGGER_H
