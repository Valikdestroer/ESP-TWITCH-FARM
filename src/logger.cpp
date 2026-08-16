#include "logger.h"
#include <cstdarg>
#include <ArduinoJson.h>

LogMessage Logger::logs[Logger::MAX_LOGS];
size_t Logger::head = 0;
size_t Logger::count = 0;
portMUX_TYPE Logger::mux = portMUX_INITIALIZER_UNLOCKED;

void Logger::init() {
    head = 0;
    count = 0;
}

void Logger::vlog(LogLevel level, const char* format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    // Print to USB Serial CDC
    const char* prefix = "[INFO]";
    if (level == LOG_LEVEL_WARN) prefix = "[WARN]";
    else if (level == LOG_LEVEL_ERROR) prefix = "[ERR ]";
    else if (level == LOG_LEVEL_DEBUG) prefix = "[DBG ]";

    uint32_t ms = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr = min / 60;
    
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", (unsigned int)(hr % 24), (unsigned int)(min % 60), (unsigned int)(sec % 60));

    Serial.printf("[%s] %s %s\n", timeStr, prefix, buffer);

    // Store in RAM ring buffer for Web UI (fast O(1) insertion, zero heap allocations in spinlock)
    portENTER_CRITICAL(&mux);
    size_t idx;
    if (count < MAX_LOGS) {
        idx = (head + count) % MAX_LOGS;
        count++;
    } else {
        idx = head;
        head = (head + 1) % MAX_LOGS;
    }
    logs[idx].timestamp = sec;
    logs[idx].level = level;
    strncpy(logs[idx].text, buffer, sizeof(logs[idx].text) - 1);
    logs[idx].text[sizeof(logs[idx].text) - 1] = '\0';
    portEXIT_CRITICAL(&mux);
}

void Logger::log(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(level, format, args);
    va_end(args);
}

void Logger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void Logger::warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LOG_LEVEL_WARN, format, args);
    va_end(args);
}

void Logger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

void Logger::debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

String Logger::getLogsJson() {
    // Allocate snapshot on heap rather than stack to prevent loopTask stack overflow (15.8KB stack array)
    LogMessage* snapshot = new (std::nothrow) LogMessage[MAX_LOGS];
    if (!snapshot) {
        return "[]";
    }

    size_t snapshotCount = 0;

    // Fast memory copy inside critical section
    portENTER_CRITICAL(&mux);
    snapshotCount = count;
    for (size_t i = 0; i < snapshotCount; ++i) {
        snapshot[i] = logs[(head + i) % MAX_LOGS];
    }
    portEXIT_CRITICAL(&mux);

    // Build JSON output outside critical section with interrupts enabled
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (size_t i = 0; i < snapshotCount; ++i) {
        JsonObject obj = arr.add<JsonObject>();
        obj["time"] = snapshot[i].timestamp;
        
        const char* levelStr = "INFO";
        if (snapshot[i].level == LOG_LEVEL_WARN) levelStr = "WARN";
        else if (snapshot[i].level == LOG_LEVEL_ERROR) levelStr = "ERROR";
        else if (snapshot[i].level == LOG_LEVEL_DEBUG) levelStr = "DEBUG";
        
        obj["level"] = levelStr;
        obj["msg"] = snapshot[i].text;
    }

    delete[] snapshot;

    String output;
    serializeJson(doc, output);
    return output;
}

void Logger::clearLogs() {
    portENTER_CRITICAL(&mux);
    head = 0;
    count = 0;
    portEXIT_CRITICAL(&mux);
}
