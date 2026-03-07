// test_screenshot.cpp — Catch2 unit tests for the screenshot / framebuffer capture system.
//
// All tests run in headless mode. The RHI headless path skips all GL calls, so
// captureFramebuffer returns false in headless mode — this verifies the guard
// path without requiring a real OpenGL context.
//
// The RHI is initialised once for all tests in this file via a one-shot static
// (same pattern as test_mesh_loader.cpp). No per-test init/shutdown to avoid
// conflicting with other tests in the ffe_tests binary.
//
// Tests cover:
//   - RHI query functions: isHeadless(), getViewportWidth(), getViewportHeight()
//   - captureFramebuffer argument validation (null, empty, bad dimensions)
//   - captureFramebuffer headless guard (no crash, returns false cleanly)

#include <catch2/catch_test_macros.hpp>

#include "renderer/screenshot.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"

using namespace ffe;
using namespace ffe::renderer;
using namespace ffe::rhi;

// ---------------------------------------------------------------------------
// Headless RHI — initialised once for all tests in this file
// ---------------------------------------------------------------------------

static bool s_rhiInited = false;

static bool ensureHeadlessRhi() {
    if (s_rhiInited) { return true; }
    rhi::RhiConfig cfg;
    cfg.headless       = true;
    cfg.viewportWidth  = 1280;
    cfg.viewportHeight = 720;
    const rhi::RhiResult r = rhi::init(cfg);
    s_rhiInited = (r == rhi::RhiResult::OK);
    return s_rhiInited;
}

// ---------------------------------------------------------------------------
// RHI query function tests
// ---------------------------------------------------------------------------

TEST_CASE("rhi::isHeadless returns true after headless init", "[screenshot][rhi]") {
    REQUIRE(ensureHeadlessRhi());
    CHECK(rhi::isHeadless() == true);
}

TEST_CASE("rhi::getViewportWidth returns configured width after init", "[screenshot][rhi]") {
    REQUIRE(ensureHeadlessRhi());
    CHECK(rhi::getViewportWidth() == 1280);
}

TEST_CASE("rhi::getViewportHeight returns configured height after init", "[screenshot][rhi]") {
    REQUIRE(ensureHeadlessRhi());
    CHECK(rhi::getViewportHeight() == 720);
}

// ---------------------------------------------------------------------------
// captureFramebuffer argument validation
// ---------------------------------------------------------------------------

TEST_CASE("captureFramebuffer: null path returns false without crashing", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    // In headless mode the headless guard fires before path validation.
    // Either way the result must be false and the function must not crash.
    const bool result = captureFramebuffer(nullptr, 1280, 720);
    CHECK(result == false);
}

TEST_CASE("captureFramebuffer: empty path returns false without crashing", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    const bool result = captureFramebuffer("", 1280, 720);
    CHECK(result == false);
}

TEST_CASE("captureFramebuffer: zero width returns false without crashing", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    const bool result = captureFramebuffer("screenshots/test.png", 0, 720);
    CHECK(result == false);
}

TEST_CASE("captureFramebuffer: zero height returns false without crashing", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    const bool result = captureFramebuffer("screenshots/test.png", 1280, 0);
    CHECK(result == false);
}

TEST_CASE("captureFramebuffer: negative width returns false without crashing", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    const bool result = captureFramebuffer("screenshots/test.png", -1, 720);
    CHECK(result == false);
}

TEST_CASE("captureFramebuffer: negative height returns false without crashing", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    const bool result = captureFramebuffer("screenshots/test.png", 1280, -1);
    CHECK(result == false);
}

// ---------------------------------------------------------------------------
// captureFramebuffer headless guard
// ---------------------------------------------------------------------------

TEST_CASE("captureFramebuffer: returns false in headless mode (no GL context)", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    // With valid args and headless=true, the headless guard must fire and return
    // false without calling glReadPixels or stbi_write_png. No output file
    // should be written (no filesystem side-effects in headless mode).
    const bool result = captureFramebuffer("screenshots/headless_test.png", 1280, 720);
    CHECK(result == false);
}

TEST_CASE("captureFramebuffer: valid path with valid dims still returns false in headless", "[screenshot]") {
    REQUIRE(ensureHeadlessRhi());
    // Confirm the headless guard fires regardless of otherwise-valid arguments.
    const bool result = captureFramebuffer("shot.png", 800, 600);
    CHECK(result == false);
}
