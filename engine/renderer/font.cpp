// font.cpp — TTF font loading and rendering for FFE.
//
// Uses stb_truetype + stb_rect_pack for rasterization and atlas packing.
// Implements the TTF portion of the TextRenderer API declared in text_renderer.h.
//
// stb_truetype and stb_rect_pack are public-domain single-header libraries
// embedded in third_party/stb/.

#include "renderer/text_renderer.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>

// Suppress warnings stb generates under -Wall -Wextra (it's a C library).
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wimplicit-int-conversion"
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wcomma"
    #pragma clang diagnostic ignored "-Wextra-semi-stmt"
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wcast-qual"
    #pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

#define STB_RECT_PACK_IMPLEMENTATION
#include "../../third_party/stb/stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace ffe::renderer {

// Maximum atlas dimension. If glyphs do not fit at this size, loadFont fails.
static constexpr u32 MAX_ATLAS_DIM = 2048;

// Maximum TTF file size to load (16 MB — generous for any reasonable font).
static constexpr std::size_t MAX_FONT_FILE_SIZE = 16u * 1024u * 1024u;

u32 loadFont(TextRenderer& tr, const char* absolutePath, const f32 pixelSize) {
    if (absolutePath == nullptr || absolutePath[0] == '\0') {
        FFE_LOG_ERROR("Font", "loadFont: null or empty path");
        return 0;
    }

    if (pixelSize < 4.0f || pixelSize > 256.0f) {
        FFE_LOG_ERROR("Font", "loadFont: pixel size %.1f out of range [4, 256]", static_cast<double>(pixelSize));
        return 0;
    }

    // Find a free slot (1-indexed for external use).
    u32 slotIdx = 0;
    for (u32 i = 0; i < MAX_FONTS; ++i) {
        if (!tr.fonts[i].occupied) {
            slotIdx = i;
            break;
        }
        if (i == MAX_FONTS - 1) {
            FFE_LOG_ERROR("Font", "loadFont: all %u font slots in use", MAX_FONTS);
            return 0;
        }
    }

    // Read the font file.
    FILE* f = std::fopen(absolutePath, "rb");
    if (f == nullptr) {
        FFE_LOG_ERROR("Font", "loadFont: failed to open '%s'", absolutePath);
        return 0;
    }

    std::fseek(f, 0, SEEK_END);
    const long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (fileSize <= 0 || static_cast<std::size_t>(fileSize) > MAX_FONT_FILE_SIZE) {
        FFE_LOG_ERROR("Font", "loadFont: file size %ld out of range", fileSize);
        std::fclose(f);
        return 0;
    }

    auto* fontData = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(fileSize)));
    if (fontData == nullptr) {
        FFE_LOG_ERROR("Font", "loadFont: failed to allocate %ld bytes for font data", fileSize);
        std::fclose(f);
        return 0;
    }

    const std::size_t bytesRead = std::fread(fontData, 1, static_cast<std::size_t>(fileSize), f);
    std::fclose(f);

    if (bytesRead != static_cast<std::size_t>(fileSize)) {
        FFE_LOG_ERROR("Font", "loadFont: incomplete read (%zu / %ld bytes)", bytesRead, fileSize);
        std::free(fontData);
        return 0;
    }

    // Validate font data — stbtt_GetFontOffsetForIndex returns -1 for invalid data.
    if (stbtt_GetFontOffsetForIndex(fontData, 0) < 0) {
        FFE_LOG_ERROR("Font", "loadFont: invalid font data in '%s'", absolutePath);
        std::free(fontData);
        return 0;
    }

    // Try atlas sizes: 512, 1024, 2048.
    stbtt_packedchar packedChars[TTF_CHAR_COUNT] = {};
    unsigned char* atlasPixels = nullptr;
    u32 atlasW = 0;
    u32 atlasH = 0;

    for (u32 dim = 512; dim <= MAX_ATLAS_DIM; dim *= 2) {
        atlasPixels = static_cast<unsigned char*>(std::malloc(dim * dim));
        if (atlasPixels == nullptr) {
            if (dim == MAX_ATLAS_DIM) {
                FFE_LOG_ERROR("Font", "loadFont: failed to allocate atlas (%ux%u)", dim, dim);
                std::free(fontData);
                return 0;
            }
            continue;
        }

        stbtt_pack_context ctx;
        if (!stbtt_PackBegin(&ctx, atlasPixels, static_cast<int>(dim), static_cast<int>(dim), 0, 1, nullptr)) {
            std::free(atlasPixels);
            atlasPixels = nullptr;
            if (dim == MAX_ATLAS_DIM) {
                FFE_LOG_ERROR("Font", "loadFont: stbtt_PackBegin failed at %ux%u", dim, dim);
                std::free(fontData);
                return 0;
            }
            continue;
        }

        // 2x horizontal oversampling for better quality at small sizes.
        stbtt_PackSetOversampling(&ctx, 2, 1);

        const int packResult = stbtt_PackFontRange(
            &ctx, fontData, 0,
            static_cast<float>(pixelSize),
            static_cast<int>(TTF_FIRST_CHAR),
            static_cast<int>(TTF_CHAR_COUNT),
            packedChars);

        stbtt_PackEnd(&ctx);

        if (packResult != 0) {
            // Success — glyphs fit.
            atlasW = dim;
            atlasH = dim;
            break;
        }

        // Did not fit — try larger.
        std::free(atlasPixels);
        atlasPixels = nullptr;

        if (dim == MAX_ATLAS_DIM) {
            FFE_LOG_ERROR("Font", "loadFont: glyphs do not fit in %ux%u atlas", dim, dim);
            std::free(fontData);
            return 0;
        }
    }

    // Extract font-level metrics.
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData, 0)) {
        FFE_LOG_ERROR("Font", "loadFont: stbtt_InitFont failed");
        std::free(atlasPixels);
        std::free(fontData);
        return 0;
    }

    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, static_cast<float>(pixelSize));
    int ascentI = 0, descentI = 0, lineGapI = 0;
    stbtt_GetFontVMetrics(&fontInfo, &ascentI, &descentI, &lineGapI);

    const f32 ascent   = static_cast<f32>(ascentI) * scale;
    const f32 descent  = static_cast<f32>(descentI) * scale;
    const f32 lineGap  = static_cast<f32>(lineGapI) * scale;

    // Font data is no longer needed after rasterization.
    std::free(fontData);
    fontData = nullptr;

    // Upload atlas to GPU.
    rhi::TextureDesc texDesc;
    texDesc.width             = atlasW;
    texDesc.height            = atlasH;
    texDesc.format            = rhi::TextureFormat::R8;
    texDesc.filter            = rhi::TextureFilter::LINEAR;
    texDesc.wrap              = rhi::TextureWrap::CLAMP_TO_EDGE;
    texDesc.pixelData         = atlasPixels;
    texDesc.swizzleRedToAlpha = true;

    const rhi::TextureHandle atlas = rhi::createTexture(texDesc);
    std::free(atlasPixels);
    atlasPixels = nullptr;

    if (!rhi::isValid(atlas)) {
        FFE_LOG_ERROR("Font", "loadFont: failed to create atlas texture");
        return 0;
    }

    // Fill the FontHandle slot.
    FontHandle& fh = tr.fonts[slotIdx];
    fh.atlas      = atlas;
    fh.fontSize   = pixelSize;
    fh.ascent     = ascent;
    fh.descent    = descent;
    fh.lineHeight = ascent - descent + lineGap;
    fh.occupied   = true;

    // Extract per-glyph metrics from packed chars.
    const f32 atlasWf = static_cast<f32>(atlasW);
    const f32 atlasHf = static_cast<f32>(atlasH);

    for (u32 i = 0; i < TTF_CHAR_COUNT; ++i) {
        const stbtt_packedchar& pc = packedChars[i];
        TtfGlyph& g = fh.glyphs[i];

        g.advanceX = pc.xadvance;
        g.offsetX  = pc.xoff;
        g.offsetY  = pc.yoff;
        g.width    = static_cast<f32>(pc.x1 - pc.x0);
        g.height   = static_cast<f32>(pc.y1 - pc.y0);
        g.uvMinX   = static_cast<f32>(pc.x0) / atlasWf;
        g.uvMinY   = static_cast<f32>(pc.y0) / atlasHf;
        g.uvMaxX   = static_cast<f32>(pc.x1) / atlasWf;
        g.uvMaxY   = static_cast<f32>(pc.y1) / atlasHf;
    }

    const u32 fontId = slotIdx + 1; // 1-indexed
    FFE_LOG_INFO("Font", "Loaded font '%s' at %.0fpx -> slot %u, atlas %ux%u",
                 absolutePath, static_cast<double>(pixelSize), fontId, atlasW, atlasH);

    return fontId;
}

void unloadFont(TextRenderer& tr, const u32 fontId) {
    if (fontId == 0 || fontId > MAX_FONTS) return;

    FontHandle& fh = tr.fonts[fontId - 1];
    if (!fh.occupied) return;

    if (rhi::isValid(fh.atlas)) {
        rhi::destroyTexture(fh.atlas);
    }

    fh = FontHandle{}; // Reset to defaults (occupied = false)
}

bool drawFontText(TextRenderer& tr, const u32 fontId, const char* text,
                  f32 x, f32 y, const f32 scale,
                  const f32 r, const f32 g, const f32 b, const f32 a) {
    if (fontId == 0 || fontId > MAX_FONTS) return false;
    if (text == nullptr) return false;

    const FontHandle& fh = tr.fonts[fontId - 1];
    if (!fh.occupied) return false;

    // Baseline: position y is the top of the text. Add ascent to get baseline.
    const f32 baseline = y + fh.ascent * scale;
    const f32 startX = x;

    while (*text != '\0' && tr.glyphCount < MAX_TEXT_GLYPHS) {
        const char ch = *text++;

        // Newline support
        if (ch == '\n') {
            x = startX;
            y += fh.lineHeight * scale;
            continue;
        }

        // Skip unprintable characters
        if (ch < 32 || ch > 126) continue;

        const u32 charIdx = static_cast<u32>(ch - 32);
        const TtfGlyph& glyph = fh.glyphs[charIdx];

        // Only emit a quad if the glyph has non-zero dimensions (skip space etc.)
        if (glyph.width > 0.0f && glyph.height > 0.0f) {
            GlyphQuad& gq = tr.glyphs[tr.glyphCount++];
            gq.x      = x + glyph.offsetX * scale;
            gq.y      = baseline + glyph.offsetY * scale;
            gq.width  = glyph.width * scale;
            gq.height = glyph.height * scale;
            gq.uvMinX = glyph.uvMinX;
            gq.uvMinY = glyph.uvMinY;
            gq.uvMaxX = glyph.uvMaxX;
            gq.uvMaxY = glyph.uvMaxY;
            gq.r = r;
            gq.g = g;
            gq.b = b;
            gq.a = a;
            gq.texture = fh.atlas;
        }

        x += glyph.advanceX * scale;
    }

    return true;
}

void measureText(const TextRenderer& tr, const u32 fontId, const char* text,
                 const f32 scale, f32* outWidth, f32* outHeight) {
    if (outWidth != nullptr) *outWidth = 0.0f;
    if (outHeight != nullptr) *outHeight = 0.0f;

    if (fontId == 0 || fontId > MAX_FONTS) return;
    if (text == nullptr) return;

    const FontHandle& fh = tr.fonts[fontId - 1];
    if (!fh.occupied) return;

    f32 maxLineWidth = 0.0f;
    f32 curLineWidth = 0.0f;
    u32 lineCount = 1;

    while (*text != '\0') {
        const char ch = *text++;

        if (ch == '\n') {
            if (curLineWidth > maxLineWidth) maxLineWidth = curLineWidth;
            curLineWidth = 0.0f;
            ++lineCount;
            continue;
        }

        if (ch < 32 || ch > 126) continue;

        const u32 charIdx = static_cast<u32>(ch - 32);
        curLineWidth += fh.glyphs[charIdx].advanceX * scale;
    }

    if (curLineWidth > maxLineWidth) maxLineWidth = curLineWidth;

    if (outWidth != nullptr) *outWidth = maxLineWidth;
    if (outHeight != nullptr) *outHeight = fh.lineHeight * scale * static_cast<f32>(lineCount);
}

} // namespace ffe::renderer
