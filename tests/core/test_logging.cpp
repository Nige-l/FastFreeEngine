#include <catch2/catch_test_macros.hpp>
#include "core/logging.h"

#include <cstdio>
#include <cstring>

TEST_CASE("Logging level filtering", "[logging]") {
    ffe::initLogging();

    // Redirect output to a temporary file so we can check it
    FILE* tmp = tmpfile();
    REQUIRE(tmp != nullptr);
    ffe::setLogFile(tmp);

    SECTION("Messages below min level are suppressed") {
        ffe::setLogLevel(ffe::LogLevel::WARN);

        FFE_LOG_INFO("Test", "this should not appear");
        fflush(tmp);

        rewind(tmp);
        char buf[512];
        const char* result = fgets(buf, sizeof(buf), tmp);
        REQUIRE(result == nullptr); // Nothing written
    }

    SECTION("Messages at or above min level are emitted") {
        ffe::setLogLevel(ffe::LogLevel::INFO);

        FFE_LOG_INFO("Test", "hello %s", "world");
        fflush(tmp);

        rewind(tmp);
        char buf[512];
        const char* result = fgets(buf, sizeof(buf), tmp);
        REQUIRE(result != nullptr);
        REQUIRE(std::strstr(buf, "INFO") != nullptr);
        REQUIRE(std::strstr(buf, "hello world") != nullptr);
        REQUIRE(std::strstr(buf, "[Test]") != nullptr);
    }

    fclose(tmp);

    // Restore defaults
    ffe::setLogFile(nullptr);
    ffe::setLogLevel(ffe::LogLevel::TRACE);
    ffe::shutdownLogging();
}

TEST_CASE("Logging format string with arguments", "[logging]") {
    ffe::initLogging();

    FILE* tmp = tmpfile();
    REQUIRE(tmp != nullptr);
    ffe::setLogFile(tmp);
    ffe::setLogLevel(ffe::LogLevel::TRACE);

    FFE_LOG_WARN("Engine", "value=%d name=%s", 42, "test");
    fflush(tmp);

    rewind(tmp);
    char buf[512];
    const char* result = fgets(buf, sizeof(buf), tmp);
    REQUIRE(result != nullptr);
    REQUIRE(std::strstr(buf, "WARN") != nullptr);
    REQUIRE(std::strstr(buf, "value=42 name=test") != nullptr);

    fclose(tmp);
    ffe::setLogFile(nullptr);
    ffe::shutdownLogging();
}
