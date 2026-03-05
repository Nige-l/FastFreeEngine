#include <catch2/catch_test_macros.hpp>
#include "core/types.h"

#include <cstring>

// --- Result type tests ---

TEST_CASE("Result::ok returns success", "[types]") {
    const ffe::Result r = ffe::Result::ok();

    REQUIRE(r.isOk() == true);
    REQUIRE(static_cast<bool>(r) == true);
}

TEST_CASE("Result::fail returns failure with message", "[types]") {
    const ffe::Result r = ffe::Result::fail("something broke");

    REQUIRE(r.isOk() == false);
    REQUIRE(static_cast<bool>(r) == false);
    REQUIRE(std::strcmp(r.message(), "something broke") == 0);
}

TEST_CASE("Result::fail with empty string message", "[types]") {
    const ffe::Result r = ffe::Result::fail("");

    REQUIRE(r.isOk() == false);
    REQUIRE(static_cast<bool>(r) == false);
    REQUIRE(r.message() != nullptr);
    REQUIRE(std::strcmp(r.message(), "") == 0);
}

TEST_CASE("Result::ok message returns empty string", "[types]") {
    const ffe::Result r = ffe::Result::ok();

    // The ok() factory passes "" as the message
    REQUIRE(r.message() != nullptr);
    REQUIRE(std::strcmp(r.message(), "") == 0);
}

TEST_CASE("Result fits in two registers on x86-64", "[types]") {
    // A bool + a pointer should fit in 16 bytes (two registers)
    static_assert(sizeof(ffe::Result) <= 16,
        "Result must fit in two x86-64 registers (16 bytes)");
}

// --- Tier helper tests ---

TEST_CASE("tierAtLeast with CURRENT_TIER", "[types]") {
    // CURRENT_TIER is LEGACY (value 1) in the default build
    REQUIRE(ffe::tierAtLeast(ffe::HardwareTier::RETRO) == true);
    REQUIRE(ffe::tierAtLeast(ffe::HardwareTier::LEGACY) == true);

    // These depend on actual tier; if LEGACY, STANDARD and MODERN should be false
    if constexpr (ffe::CURRENT_TIER == ffe::HardwareTier::LEGACY) {
        REQUIRE(ffe::tierAtLeast(ffe::HardwareTier::STANDARD) == false);
        REQUIRE(ffe::tierAtLeast(ffe::HardwareTier::MODERN) == false);
    }
}

TEST_CASE("tierAtMost with CURRENT_TIER", "[types]") {
    REQUIRE(ffe::tierAtMost(ffe::HardwareTier::MODERN) == true);

    if constexpr (ffe::CURRENT_TIER == ffe::HardwareTier::LEGACY) {
        REQUIRE(ffe::tierAtMost(ffe::HardwareTier::LEGACY) == true);
        REQUIRE(ffe::tierAtMost(ffe::HardwareTier::RETRO) == false);
    }
}

TEST_CASE("arenaDefaultSize returns nonzero for all tiers", "[types]") {
    const size_t size = ffe::arenaDefaultSize();
    REQUIRE(size > 0);

    // Verify it matches the expected value for the current tier
    if constexpr (ffe::CURRENT_TIER == ffe::HardwareTier::RETRO) {
        REQUIRE(size == ffe::ARENA_SIZE_RETRO);
    } else if constexpr (ffe::CURRENT_TIER == ffe::HardwareTier::LEGACY) {
        REQUIRE(size == ffe::ARENA_SIZE_LEGACY);
    } else if constexpr (ffe::CURRENT_TIER == ffe::HardwareTier::STANDARD) {
        REQUIRE(size == ffe::ARENA_SIZE_STANDARD);
    } else if constexpr (ffe::CURRENT_TIER == ffe::HardwareTier::MODERN) {
        REQUIRE(size == ffe::ARENA_SIZE_MODERN);
    }
}
