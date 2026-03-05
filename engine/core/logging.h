#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdarg>

namespace ffe {

enum class LogLevel : uint8_t {
    TRACE  = 0,
    DEBUG  = 1,
    INFO   = 2,
    WARN   = 3,
    ERR    = 4,   // Not ERROR — that's a Windows macro
    FATAL  = 5
};

// Set the minimum severity that will be emitted at runtime.
// Messages below this level are discarded (but still compiled-out via macros in Release).
void setLogLevel(LogLevel level);

// Set the output file. Pass nullptr to log to stdout (the default).
// The engine does NOT own this file pointer — caller is responsible for fclose().
void setLogFile(FILE* file);

// Core log function. Do not call directly — use the macros below.
void logMessage(LogLevel level, const char* system, const char* fmt, ...);

// Initialize/shutdown logging subsystem
void initLogging();
void shutdownLogging();

} // namespace ffe

// --- Compile-time filtering macros ---
// In Release builds, TRACE and DEBUG are compiled out entirely.
// The do-while(0) idiom ensures the macro is a single statement.
// Uses C++20 __VA_OPT__ instead of GNU ## extension for portability.

#if defined(FFE_DEBUG) || defined(FFE_RELWITHDEBINFO)
    #define FFE_LOG_TRACE(system, fmt, ...) \
        do { ffe::logMessage(ffe::LogLevel::TRACE, system, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
    #define FFE_LOG_DEBUG(system, fmt, ...) \
        do { ffe::logMessage(ffe::LogLevel::DEBUG, system, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
#else
    #define FFE_LOG_TRACE(system, fmt, ...) do {} while(0)
    #define FFE_LOG_DEBUG(system, fmt, ...) do {} while(0)
#endif

#define FFE_LOG_INFO(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::INFO, system, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
#define FFE_LOG_WARN(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::WARN, system, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
#define FFE_LOG_ERROR(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::ERR, system, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
#define FFE_LOG_FATAL(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::FATAL, system, fmt __VA_OPT__(,) __VA_ARGS__); } while(0)
