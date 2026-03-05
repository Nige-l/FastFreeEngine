#include "core/logging.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>

// Internal state — file-scope, not exported
static ffe::LogLevel s_minLevel = ffe::LogLevel::TRACE;
static FILE* s_output = stdout;
static std::mutex s_logMutex; // Only lock for writes, never in hot path

namespace ffe {

void setLogLevel(const LogLevel level) {
    s_minLevel = level;
}

void setLogFile(FILE* file) {
    s_output = file ? file : stdout;
}

void logMessage(const LogLevel level, const char* system, const char* fmt, ...) {
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(s_minLevel)) {
        return;
    }

    // Get timestamp — use steady_clock offset from process start, not wall clock
    static const auto s_startTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_startTime);

    const int64_t totalMs = elapsed.count();
    const int32_t hours   = static_cast<int32_t>(totalMs / 3600000);
    const int32_t minutes = static_cast<int32_t>((totalMs % 3600000) / 60000);
    const int32_t seconds = static_cast<int32_t>((totalMs % 60000) / 1000);
    const int32_t millis  = static_cast<int32_t>(totalMs % 1000);

    // Format the user message into a stack buffer — no heap allocation
    char messageBuffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(messageBuffer, sizeof(messageBuffer), fmt, args);
    va_end(args);

    static constexpr const char* LEVEL_NAMES[] = {
        "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
    };

    // Lock and write. This is the only mutex in the logging system.
    const std::lock_guard<std::mutex> lock(s_logMutex);
    fprintf(s_output, "[%02d:%02d:%02d.%03d] [%s] [%s] %s\n",
        hours, minutes, seconds, millis,
        LEVEL_NAMES[static_cast<uint8_t>(level)],
        system,
        messageBuffer
    );

    // Flush FATAL immediately
    if (level == LogLevel::FATAL) {
        fflush(s_output);
    }
}

void initLogging() {
    s_minLevel = LogLevel::TRACE;
    s_output = stdout;
}

void shutdownLogging() {
    if (s_output != stdout && s_output != nullptr) {
        fflush(s_output);
    }
}

} // namespace ffe
