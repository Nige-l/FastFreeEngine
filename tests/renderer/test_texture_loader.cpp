#include <catch2/catch_test_macros.hpp>
#include "renderer/texture_loader.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include <string_view>
#include <cstdio>

// =============================================================================
// Design notes
// =============================================================================
//
// texture_loader's setAssetRoot() has write-once semantics (LOW-2): once set
// to a valid directory, all subsequent setAssetRoot() calls return false.
// Because ctest discovers and runs each test in a separate process instance
// by default (via catch_discover_tests), the write-once global state does NOT
// persist across test cases. Tests that need write-once behaviour verified
// must do so within a single TEST_CASE using multiple REQUIRE calls, or via
// the two-argument loadTexture(path, assetRoot) overload which does not mutate
// global state.
//
// For path-validation tests that do NOT depend on the global root, the two-
// argument overload loadTexture(path, assetRoot) is used with a known absolute
// directory ("/tmp") to avoid touching global state.
//
// Tests that require a real file on disk and GPU upload are tagged
// [requires_file]. They write a minimal 1x1 RGBA PNG to /tmp via fwrite.

// =============================================================================
// Helper: a guaranteed-existing absolute directory for path validation tests.
// /tmp is present on all Linux systems and is always a valid absolute directory.
// =============================================================================
static constexpr const char* SAFE_ROOT = "/tmp";

// =============================================================================
// Minimal valid 1x1 RGBA8 PNG (white pixel).
// Generated via Python: zlib.compress(b'\x00\xff\xff\xff\xff') and correct CRCs.
// Verified to load successfully with stb_image.
// =============================================================================
namespace {

static const unsigned char k_minimalPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, // PNG signature
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // width=1, height=1
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, // 8-bit RGBA, CRC
    0x89, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, // IDAT chunk
    0x54, 0x78, 0x9c, 0x63, 0xf8, 0x0f, 0x04, 0x00, // zlib-compressed scanline
    0x09, 0xfb, 0x03, 0xfd, 0xfb, 0x5e, 0x6b, 0x2b, // data + CRC
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, // IEND chunk
    0xae, 0x42, 0x60, 0x82                           // IEND CRC
};
static constexpr std::size_t k_minimalPngSize = sizeof(k_minimalPng);

static constexpr const char* k_testPngAbsPath = "/tmp/ffe_test_1x1.png";
static constexpr const char* k_testPngRelPath  = "ffe_test_1x1.png";

bool writeTestPng() {
    FILE* const f = ::fopen(k_testPngAbsPath, "wb");
    if (f == nullptr) { return false; }
    const std::size_t written = ::fwrite(k_minimalPng, 1u, k_minimalPngSize, f);
    ::fclose(f);
    return written == k_minimalPngSize;
}

} // anonymous namespace

// =============================================================================
// VRAM budget query — no RHI context required.
// =============================================================================

TEST_CASE("textureVramUsed returns 0 before RHI init", "[texture_loader][vram]") {
    // s_totalTextureVram is zero-initialised before the RHI is started.
    REQUIRE(ffe::rhi::textureVramUsed() == 0u);
}

// =============================================================================
// Path validation — two-argument overload, no global state mutation.
// isPathSafe() runs before realpath()/fopen(), so no real file is needed.
// =============================================================================

TEST_CASE("loadTexture with null path returns invalid handle", "[texture_loader][path]") {
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture(nullptr, SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with empty string path returns invalid handle", "[texture_loader][path]") {
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with absolute path starting with slash returns invalid handle", "[texture_loader][path][security]") {
    // Absolute Unix paths are rejected by isPathSafe() — SEC-1.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("/etc/passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with absolute path starting with backslash returns invalid handle", "[texture_loader][path][security]") {
    // Absolute Windows-style paths are rejected — SEC-1.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("\\etc\\passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with path traversal ../ returns invalid handle", "[texture_loader][path][security]") {
    // SEC-1: traversal sequences rejected before any syscall.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("../../../etc/passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with embedded path traversal returns invalid handle", "[texture_loader][path][security]") {
    // Path traversal embedded after a directory component — SEC-1.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("assets/../../etc/passwd", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with trailing /.. returns invalid handle", "[texture_loader][path][security]") {
    // "/.. " pattern — also rejected by isPathSafe().
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("assets/..", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with nonexistent file returns invalid handle", "[texture_loader][path]") {
    // File does not exist — realpath() fails and returns invalid handle.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture(
        "definitely_does_not_exist_ffe_test.png", SAFE_ROOT);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

// =============================================================================
// Two-argument overload: invalid assetRoot values
// =============================================================================

TEST_CASE("loadTexture with null assetRoot returns invalid handle", "[texture_loader][path]") {
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("test.png", nullptr);
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with empty assetRoot returns invalid handle", "[texture_loader][path]") {
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("test.png", "");
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with relative assetRoot returns invalid handle", "[texture_loader][path]") {
    // validateAssetRoot() requires an absolute path (starts with '/').
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("test.png", "relative/path");
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

TEST_CASE("loadTexture with nonexistent assetRoot returns invalid handle", "[texture_loader][path]") {
    // validateAssetRoot() calls stat() — nonexistent directories fail.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture(
        "test.png", "/nonexistent_ffe_test_root_dir_12345");
    REQUIRE_FALSE(ffe::rhi::isValid(h));
}

// =============================================================================
// Global setAssetRoot — write-once semantics (LOW-2).
// These tests are kept in a single TEST_CASE with SECTIONs so that they run
// sequentially in the same process instance and the write-once state is shared.
// =============================================================================

TEST_CASE("setAssetRoot global write-once semantics", "[texture_loader][global_root]") {
    SECTION("loadTexture before setAssetRoot returns invalid handle") {
        // The single-argument overload rejects calls before setAssetRoot() is called (HIGH-3).
        const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture("test.png");
        REQUIRE_FALSE(ffe::rhi::isValid(h));
    }

    SECTION("setAssetRoot with valid directory succeeds, second call returns false") {
        // /tmp is always present and is an absolute directory on Linux.
        REQUIRE(ffe::renderer::setAssetRoot("/tmp"));

        // Write-once semantics (LOW-2): second call must be rejected.
        REQUIRE_FALSE(ffe::renderer::setAssetRoot("/tmp"));

        // getAssetRoot must reflect the set value.
        const char* const root = ffe::renderer::getAssetRoot();
        REQUIRE(root != nullptr);
        REQUIRE(std::string_view{root} == std::string_view{"/tmp"});
    }

    SECTION("setAssetRoot with null path returns false") {
        REQUIRE_FALSE(ffe::renderer::setAssetRoot(nullptr));
    }

    SECTION("setAssetRoot with relative path returns false") {
        REQUIRE_FALSE(ffe::renderer::setAssetRoot("relative/dir"));
    }

    SECTION("setAssetRoot with nonexistent absolute path returns false") {
        REQUIRE_FALSE(ffe::renderer::setAssetRoot("/nonexistent_ffe_test_dir_67890"));
    }
}

// =============================================================================
// GPU upload tests — require headless RHI context + real image file.
// A 1x1 RGBA PNG is written to /tmp/ffe_test_1x1.png using the correct
// PNG byte sequence verified against stb_image.
// =============================================================================

TEST_CASE("loadTexture with valid PNG succeeds in headless RHI context", "[texture_loader][gpu][requires_file]") {
    // Write the test PNG to /tmp using the two-argument overload (no global state).
    if (!writeTestPng()) {
        WARN("Could not write test PNG to " << k_testPngAbsPath << " — skipping GPU upload test");
        return;
    }

    // Init headless RHI
    ffe::rhi::RhiConfig cfg;
    cfg.headless = true;
    REQUIRE(ffe::rhi::init(cfg) == ffe::rhi::RhiResult::OK);

    // VRAM must be 0 before any texture upload
    REQUIRE(ffe::rhi::textureVramUsed() == 0u);

    // Load using the two-argument overload so we don't rely on global setAssetRoot state.
    const ffe::rhi::TextureHandle h = ffe::renderer::loadTexture(k_testPngRelPath, "/tmp");
    // In headless mode the RHI returns a monotonically-increasing handle id != 0.
    // This confirms the decode path (stb_image) and the upload path both completed.
    // Note: textureVramUsed() stays 0 in headless mode (no real GL upload occurs).
    REQUIRE(ffe::rhi::isValid(h));

    ffe::renderer::unloadTexture(h);
    ffe::rhi::shutdown();
}

TEST_CASE("unloadTexture with invalid handle is a no-op", "[texture_loader][gpu]") {
    // Safe to call with a zero handle — must not crash.
    ffe::renderer::unloadTexture(ffe::rhi::TextureHandle{0});
}

TEST_CASE("textureVramUsed returns 0 after headless init with no textures", "[texture_loader][vram]") {
    ffe::rhi::RhiConfig cfg;
    cfg.headless = true;
    REQUIRE(ffe::rhi::init(cfg) == ffe::rhi::RhiResult::OK);

    // No textures uploaded — must be 0
    REQUIRE(ffe::rhi::textureVramUsed() == 0u);

    ffe::rhi::shutdown();
}
