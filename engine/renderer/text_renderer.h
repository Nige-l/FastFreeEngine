#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"
#include "renderer/sprite_batch.h"

namespace ffe::renderer {

// Maximum glyphs that can be queued in a single frame.
// 4096 glyphs = ~64 lines of 64 characters. Plenty for HUD text.
inline constexpr u32 MAX_TEXT_GLYPHS = 4096;

// A single glyph quad ready for rendering.
struct GlyphQuad {
    f32 x, y;           // Screen position (top-left corner)
    f32 width, height;  // Size in screen pixels
    f32 uvMinX, uvMinY; // UV top-left in font atlas
    f32 uvMaxX, uvMaxY; // UV bottom-right in font atlas
    f32 r, g, b, a;     // Tint color
};

struct TextRenderer {
    rhi::TextureHandle fontAtlas;
    u32 glyphCount           = 0;
    GlyphQuad* glyphs       = nullptr; // Fixed allocation, MAX_TEXT_GLYPHS
    f32 screenWidth          = 0.0f;
    f32 screenHeight         = 0.0f;
};

// Initialize the text renderer. Bakes an 8x8 bitmap font into a GPU texture.
// screenWidth/screenHeight define the coordinate space for drawText().
void initTextRenderer(TextRenderer& tr, f32 screenWidth, f32 screenHeight);

// Shutdown — destroy the font atlas and free glyph buffer.
void shutdownTextRenderer(TextRenderer& tr);

// Reset per-frame glyph buffer. Call at the start of each frame.
void beginText(TextRenderer& tr);

// Queue text for rendering. Coordinates are screen pixels, origin top-left.
// scale=1.0 renders each character as 8x8 pixels. scale=2.0 renders 16x16, etc.
void drawText(TextRenderer& tr, const char* text,
              f32 x, f32 y, f32 scale,
              f32 r, f32 g, f32 b, f32 a);

// Queue a filled rectangle for rendering. Same coordinate space as drawText.
// Uses the font atlas's solid-white cell so no extra texture bind is needed.
void drawRect(TextRenderer& tr,
              f32 x, f32 y, f32 width, f32 height,
              f32 r, f32 g, f32 b, f32 a);

// Flush all queued text. Renders using the provided sprite batch.
// Sets up a screen-space ortho projection internally, then restores the
// previous VP matrix after rendering. Call after world sprite rendering.
void flushText(TextRenderer& tr, SpriteBatch& batch,
               rhi::SpriteVertex* staging);

} // namespace ffe::renderer
