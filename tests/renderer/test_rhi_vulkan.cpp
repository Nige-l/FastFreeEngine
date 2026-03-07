#include <catch2/catch_test_macros.hpp>
#include "renderer/rhi_types.h"

using namespace ffe;
using namespace ffe::rhi;

// ============================================================================
// CPU-only tests — no Vulkan context needed, always compile
// ============================================================================

TEST_CASE("RhiBackend enum values", "[rhi][vulkan]") {
    REQUIRE(static_cast<u8>(RhiBackend::OPENGL) == 0);
    REQUIRE(static_cast<u8>(RhiBackend::VULKAN) == 1);
}

TEST_CASE("RhiConfig default values preserve backward compatibility", "[rhi][vulkan]") {
    const RhiConfig config{};

    // Existing defaults must be unchanged
    REQUIRE(config.viewportWidth  == 1280);
    REQUIRE(config.viewportHeight == 720);
    REQUIRE(config.headless       == false);
    REQUIRE(config.vsync          == true);
    REQUIRE(config.debugGL        == false);

    // New Vulkan fields default to safe values
    REQUIRE(config.backend        == RhiBackend::OPENGL);
    REQUIRE(config.debugVulkan    == false);
    REQUIRE(config.preferredSwapImages == 2);
    REQUIRE(config.window         == nullptr);
}

TEST_CASE("RhiConfig Vulkan-specific fields", "[rhi][vulkan]") {
    RhiConfig config{};
    config.backend             = RhiBackend::VULKAN;
    config.debugVulkan         = true;
    config.preferredSwapImages = 3;

    REQUIRE(config.backend             == RhiBackend::VULKAN);
    REQUIRE(config.debugVulkan         == true);
    REQUIRE(config.preferredSwapImages == 3);
}

TEST_CASE("MAX_SWAPCHAIN_IMAGES constant", "[rhi][vulkan]") {
    REQUIRE(MAX_SWAPCHAIN_IMAGES == 4);
    // Must be at least 2 for double-buffering
    REQUIRE(MAX_SWAPCHAIN_IMAGES >= 2);
}

TEST_CASE("Backend/tier compatibility validation", "[rhi][vulkan]") {
    // These are compile-time constraints enforced by CMake.
    // At runtime we can verify the enum combinations that would be valid.

    // RETRO and LEGACY only support OpenGL
    SECTION("RETRO tier requires OpenGL") {
        const auto tier = HardwareTier::RETRO;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE_FALSE(vulkanAllowed);
    }

    SECTION("LEGACY tier requires OpenGL") {
        const auto tier = HardwareTier::LEGACY;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE_FALSE(vulkanAllowed);
    }

    SECTION("STANDARD tier allows both backends") {
        const auto tier = HardwareTier::STANDARD;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE(vulkanAllowed);
    }

    SECTION("MODERN tier requires Vulkan") {
        const auto tier = HardwareTier::MODERN;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE(vulkanAllowed);
        // MODERN must use Vulkan (enforced by CMake, validated here as a logic check)
        const bool openglAllowed = (static_cast<u8>(tier) < static_cast<u8>(HardwareTier::MODERN));
        REQUIRE_FALSE(openglAllowed);
    }
}

TEST_CASE("RhiConfig can be constructed with Vulkan settings", "[rhi][vulkan]") {
    // Verify that constructing a config with all Vulkan-related fields
    // does not break any existing fields
    RhiConfig config{};
    config.viewportWidth       = 800;
    config.viewportHeight      = 600;
    config.headless            = true;
    config.vsync               = false;
    config.debugGL             = false;
    config.debugVulkan         = true;
    config.backend             = RhiBackend::VULKAN;
    config.preferredSwapImages = 3;
    config.window              = nullptr;

    REQUIRE(config.viewportWidth  == 800);
    REQUIRE(config.viewportHeight == 600);
    REQUIRE(config.headless       == true);
    REQUIRE(config.vsync          == false);
    REQUIRE(config.debugGL        == false);
    REQUIRE(config.debugVulkan    == true);
    REQUIRE(config.backend        == RhiBackend::VULKAN);
    REQUIRE(config.preferredSwapImages == 3);
    REQUIRE(config.window         == nullptr);
}
