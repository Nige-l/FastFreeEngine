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

// =============================================================================
// Text buffer persistence (flicker fix)
// =============================================================================
// These tests verify the invariant that the text glyph buffer retains its
// contents across frames when beginText() is NOT called. This is critical for
// the fixed-timestep game loop: when zero ticks execute in a frame, drawText()
// is never called, so the previous frame's glyphs must persist.

TEST_CASE("beginText resets glyph count to zero", "[renderer][text]") {
    ffe::renderer::TextRenderer tr{};
    tr.glyphs = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    tr.glyphCount = 0;

    // Simulate queuing some glyphs manually
    tr.glyphCount = 5;
    REQUIRE(tr.glyphCount == 5);

    ffe::renderer::beginText(tr);
    REQUIRE(tr.glyphCount == 0);

    std::free(tr.glyphs);
}

TEST_CASE("Text glyphs persist when beginText is not called", "[renderer][text]") {
    ffe::renderer::TextRenderer tr{};
    tr.glyphs = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    tr.glyphCount = 0;
    tr.screenWidth = 800.0f;
    tr.screenHeight = 600.0f;

    // Simulate a tick frame: beginText clears, drawText populates
    ffe::renderer::beginText(tr);
    ffe::renderer::drawText(tr, "AB", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    REQUIRE(tr.glyphCount == 2);

    // Simulate a zero-tick frame: do NOT call beginText.
    // Glyphs from the previous tick must still be present.
    REQUIRE(tr.glyphCount == 2);
    // Verify the glyph data is still valid (not zeroed)
    REQUIRE(tr.glyphs[0].width > 0.0f);
    REQUIRE(tr.glyphs[1].width > 0.0f);

    std::free(tr.glyphs);
}

TEST_CASE("drawText appends to existing glyphs when beginText is skipped", "[renderer][text]") {
    ffe::renderer::TextRenderer tr{};
    tr.glyphs = static_cast<ffe::renderer::GlyphQuad*>(
        std::malloc(ffe::renderer::MAX_TEXT_GLYPHS * sizeof(ffe::renderer::GlyphQuad)));
    tr.glyphCount = 0;
    tr.screenWidth = 800.0f;
    tr.screenHeight = 600.0f;

    // First tick: draw "Hi" (2 glyphs)
    ffe::renderer::beginText(tr);
    ffe::renderer::drawText(tr, "Hi", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    REQUIRE(tr.glyphCount == 2);

    // Second tick without beginText: drawing more text appends
    ffe::renderer::drawText(tr, "AB", 0.0f, 10.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    REQUIRE(tr.glyphCount == 4);

    // With beginText: resets properly
    ffe::renderer::beginText(tr);
    REQUIRE(tr.glyphCount == 0);

    std::free(tr.glyphs);
}
