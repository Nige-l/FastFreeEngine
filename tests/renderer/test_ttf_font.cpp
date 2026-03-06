#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "renderer/text_renderer.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"

#include <cstdlib>
#include <cstring>

// =============================================================================
// Unit tests for TTF font data structures and TextRenderer font slot management.
// These tests run in headless mode (no GPU) and verify the logic paths
// without actually loading font files.
// =============================================================================

// =============================================================================
// FontHandle struct defaults
// =============================================================================

TEST_CASE("FontHandle defaults to unoccupied", "[renderer][font]") {
    ffe::renderer::FontHandle fh{};
    REQUIRE_FALSE(fh.occupied);
    REQUIRE(fh.fontSize == 0.0f);
    REQUIRE(fh.ascent == 0.0f);
    REQUIRE(fh.descent == 0.0f);
    REQUIRE(fh.lineHeight == 0.0f);
    REQUIRE(fh.atlas.id == 0);
}

// =============================================================================
// TtfGlyph defaults
// =============================================================================

TEST_CASE("TtfGlyph default-initializes to zero", "[renderer][font]") {
    ffe::renderer::TtfGlyph g{};
    REQUIRE(g.advanceX == 0.0f);
    REQUIRE(g.offsetX == 0.0f);
    REQUIRE(g.offsetY == 0.0f);
    REQUIRE(g.width == 0.0f);
    REQUIRE(g.height == 0.0f);
    REQUIRE(g.uvMinX == 0.0f);
    REQUIRE(g.uvMinY == 0.0f);
    REQUIRE(g.uvMaxX == 0.0f);
    REQUIRE(g.uvMaxY == 0.0f);
}

// =============================================================================
// TextRenderer font slots
// =============================================================================

TEST_CASE("TextRenderer initializes with all font slots empty", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    for (ffe::u32 i = 0; i < ffe::renderer::MAX_FONTS; ++i) {
        REQUIRE_FALSE(tr.fonts[i].occupied);
    }
}

// =============================================================================
// loadFont rejects invalid arguments
// =============================================================================

TEST_CASE("loadFont rejects null path", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const ffe::u32 result = ffe::renderer::loadFont(tr, nullptr, 24.0f);
    REQUIRE(result == 0);
}

TEST_CASE("loadFont rejects empty path", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const ffe::u32 result = ffe::renderer::loadFont(tr, "", 24.0f);
    REQUIRE(result == 0);
}

TEST_CASE("loadFont rejects too-small pixel size", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const ffe::u32 result = ffe::renderer::loadFont(tr, "/nonexistent.ttf", 2.0f);
    REQUIRE(result == 0);
}

TEST_CASE("loadFont rejects too-large pixel size", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const ffe::u32 result = ffe::renderer::loadFont(tr, "/nonexistent.ttf", 300.0f);
    REQUIRE(result == 0);
}

TEST_CASE("loadFont rejects nonexistent file", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const ffe::u32 result = ffe::renderer::loadFont(tr, "/tmp/nonexistent_font_12345.ttf", 24.0f);
    REQUIRE(result == 0);
}

// =============================================================================
// unloadFont with invalid IDs
// =============================================================================

TEST_CASE("unloadFont is a no-op for fontId 0", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    ffe::renderer::unloadFont(tr, 0); // Should not crash.
}

TEST_CASE("unloadFont is a no-op for out-of-range fontId", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    ffe::renderer::unloadFont(tr, ffe::renderer::MAX_FONTS + 1); // Should not crash.
    ffe::renderer::unloadFont(tr, 99); // Should not crash.
}

// =============================================================================
// drawFontText with invalid fontId
// =============================================================================

TEST_CASE("drawFontText returns false for fontId 0", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const bool result = ffe::renderer::drawFontText(tr, 0, "test", 0, 0, 1, 1, 1, 1, 1);
    REQUIRE_FALSE(result);
}

TEST_CASE("drawFontText returns false for out-of-range fontId", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const bool result = ffe::renderer::drawFontText(tr, ffe::renderer::MAX_FONTS + 1,
                                                     "test", 0, 0, 1, 1, 1, 1, 1);
    REQUIRE_FALSE(result);
}

TEST_CASE("drawFontText returns false for unoccupied slot", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const bool result = ffe::renderer::drawFontText(tr, 1, "test", 0, 0, 1, 1, 1, 1, 1);
    REQUIRE_FALSE(result);
}

TEST_CASE("drawFontText returns false for null text", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    const bool result = ffe::renderer::drawFontText(tr, 1, nullptr, 0, 0, 1, 1, 1, 1, 1);
    REQUIRE_FALSE(result);
}

// =============================================================================
// measureText with invalid fontId
// =============================================================================

TEST_CASE("measureText returns zero for fontId 0", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    ffe::f32 w = -1.0f, h = -1.0f;
    ffe::renderer::measureText(tr, 0, "test", 1.0f, &w, &h);
    REQUIRE(w == 0.0f);
    REQUIRE(h == 0.0f);
}

TEST_CASE("measureText returns zero for unoccupied slot", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    ffe::f32 w = -1.0f, h = -1.0f;
    ffe::renderer::measureText(tr, 1, "test", 1.0f, &w, &h);
    REQUIRE(w == 0.0f);
    REQUIRE(h == 0.0f);
}

TEST_CASE("measureText handles null output pointers", "[renderer][font]") {
    ffe::renderer::TextRenderer tr{};
    // Should not crash with null output pointers.
    ffe::renderer::measureText(tr, 1, "test", 1.0f, nullptr, nullptr);
}

// =============================================================================
// GlyphQuad texture field
// =============================================================================

TEST_CASE("GlyphQuad texture field default-initializes to invalid", "[renderer][font]") {
    ffe::renderer::GlyphQuad gq{};
    REQUIRE(gq.texture.id == 0);
    REQUIRE_FALSE(ffe::rhi::isValid(gq.texture));
}

// =============================================================================
// Constants
// =============================================================================

TEST_CASE("MAX_FONTS is 8", "[renderer][font]") {
    REQUIRE(ffe::renderer::MAX_FONTS == 8);
}

TEST_CASE("TTF_CHAR_COUNT is 96", "[renderer][font]") {
    REQUIRE(ffe::renderer::TTF_CHAR_COUNT == 96);
}

TEST_CASE("TTF_FIRST_CHAR is 32", "[renderer][font]") {
    REQUIRE(ffe::renderer::TTF_FIRST_CHAR == 32);
}
