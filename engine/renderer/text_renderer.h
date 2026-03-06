#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"
#include "renderer/sprite_batch.h"

namespace ffe::renderer {

// Maximum glyphs that can be queued in a single frame.
// 4096 glyphs = ~64 lines of 64 characters. Plenty for HUD text.
inline constexpr u32 MAX_TEXT_GLYPHS = 4096;

// Maximum number of simultaneously loaded TTF fonts.
inline constexpr u32 MAX_FONTS = 8;

// ASCII range for TTF glyph coverage.
inline constexpr u32 TTF_FIRST_CHAR = 32;
inline constexpr u32 TTF_CHAR_COUNT = 96; // 32-127

// A single glyph quad ready for rendering.
struct GlyphQuad {
    f32 x, y;           // Screen position (top-left corner)
    f32 width, height;  // Size in screen pixels
    f32 uvMinX, uvMinY; // UV top-left in font atlas
    f32 uvMaxX, uvMaxY; // UV bottom-right in font atlas
    f32 r, g, b, a;     // Tint color
    rhi::TextureHandle texture; // Which atlas this glyph belongs to (0 = bitmap font)
};

// Per-glyph metrics for a TTF font, indexed by (char - 32).
struct TtfGlyph {
    f32 advanceX;    // Horizontal advance to next glyph (pixels)
    f32 offsetX;     // Bearing X — left edge offset from cursor
    f32 offsetY;     // Bearing Y — top edge offset from baseline
    f32 width;       // Glyph quad width in pixels
    f32 height;      // Glyph quad height in pixels
    f32 uvMinX, uvMinY;
    f32 uvMaxX, uvMaxY;
};

// A loaded TTF font — one atlas per font+size combination.
struct FontHandle {
    rhi::TextureHandle atlas;            // GPU texture (GL_RED + swizzle)
    TtfGlyph glyphs[TTF_CHAR_COUNT];    // ASCII 32-127
    f32 fontSize  = 0.0f;               // Requested pixel height
    f32 ascent    = 0.0f;               // Baseline to top
    f32 descent   = 0.0f;               // Baseline to bottom (negative)
    f32 lineHeight = 0.0f;              // ascent - descent + lineGap
    bool occupied  = false;              // Slot in use?
};

struct TextRenderer {
    rhi::TextureHandle fontAtlas;        // Bitmap 8x8 font atlas
    u32 glyphCount           = 0;
    GlyphQuad* glyphs       = nullptr;   // Fixed allocation, MAX_TEXT_GLYPHS
    f32 screenWidth          = 0.0f;
    f32 screenHeight         = 0.0f;
    FontHandle fonts[MAX_FONTS] = {};    // TTF font slots (1-indexed externally)
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

// --- TTF Font API ---

// Load a TrueType font from a file. Rasterizes ASCII 32-127 into a glyph
// atlas at the given pixel size. Returns a font slot index (1-8) on success,
// or 0 on failure. The file path must be an absolute path (caller resolves
// against asset root).
u32 loadFont(TextRenderer& tr, const char* absolutePath, f32 pixelSize);

// Unload a previously loaded font, destroying its atlas and freeing the slot.
// No-op if fontId is 0 or out of range or already unloaded.
void unloadFont(TextRenderer& tr, u32 fontId);

// Queue TTF text for rendering. fontId is a value returned by loadFont().
// Coordinates are screen pixels, origin top-left. scale=1.0 renders at the
// loaded pixel size. Returns false if fontId is invalid.
bool drawFontText(TextRenderer& tr, u32 fontId, const char* text,
                  f32 x, f32 y, f32 scale,
                  f32 r, f32 g, f32 b, f32 a);

// Measure text without rendering. Returns width and height in pixels at the
// given scale. fontId must be a valid loaded font. Returns (0, 0) on error.
void measureText(const TextRenderer& tr, u32 fontId, const char* text,
                 f32 scale, f32* outWidth, f32* outHeight);

} // namespace ffe::renderer
