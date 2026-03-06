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

TEST_CASE("Log ring buffer captures messages", "[logging]") {
    ffe::initLogging();

    // Redirect output to tmpfile so we don't spam stdout
    FILE* tmp = tmpfile();
    REQUIRE(tmp != nullptr);
    ffe::setLogFile(tmp);
    ffe::setLogLevel(ffe::LogLevel::TRACE);

    auto* ring = ffe::getLogRingBuffer();
    REQUIRE(ring != nullptr);

    SECTION("Ring buffer starts empty after init") {
        REQUIRE(ring->count == 0);
    }

    SECTION("Messages are captured in ring buffer") {
        FFE_LOG_INFO("RingTest", "hello ring");
        REQUIRE(ring->count >= 1);

        // Find the entry we just wrote (it's the most recent)
        const uint32_t idx = (ring->writeIndex + ffe::LOG_RING_CAPACITY - 1) % ffe::LOG_RING_CAPACITY;
        REQUIRE(ring->entries[idx].level == ffe::LogLevel::INFO);
        REQUIRE(std::strstr(ring->entries[idx].system, "RingTest") != nullptr);
        REQUIRE(std::strstr(ring->entries[idx].message, "hello ring") != nullptr);
    }

    SECTION("Ring buffer wraps at capacity") {
        for (uint32_t i = 0; i < ffe::LOG_RING_CAPACITY + 10; ++i) {
            FFE_LOG_INFO("Wrap", "msg %u", i);
        }
        // Count saturates at capacity
        REQUIRE(ring->count == ffe::LOG_RING_CAPACITY);
    }

    SECTION("Filtered messages are not captured") {
        const uint32_t before = ring->count;
        ffe::setLogLevel(ffe::LogLevel::ERR);
        FFE_LOG_INFO("Filter", "should not capture");
        REQUIRE(ring->count == before);
        ffe::setLogLevel(ffe::LogLevel::TRACE);
    }

    fclose(tmp);
    ffe::setLogFile(nullptr);
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
