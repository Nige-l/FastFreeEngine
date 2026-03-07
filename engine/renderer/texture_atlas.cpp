// texture_atlas.cpp -- Runtime texture atlas for 2D sprite batching.
//
// Packs multiple small sprite textures into large atlas page textures so that
// sprites with different source textures can share a single draw call when
// they land on the same atlas page.
//
// Uses a simple shelf (row) packing algorithm:
//   - Textures are placed left-to-right in horizontal "shelves" (rows).
//   - Each shelf's height equals the tallest texture placed in that shelf.
//   - When a texture does not fit in the current shelf, a new shelf is started.
//   - When a page is full, a new page is allocated (up to MAX_ATLAS_PAGES).
//
// Atlas page textures are allocated via the RHI (RGBA8, NEAREST filter,
// CLAMP_TO_EDGE). Individual sprite textures are uploaded via
// rhi::updateTextureSubImage() (glTexSubImage2D).
//
// Performance notes:
//   - addToAtlas() is a COLD-PATH function (GPU upload). Called once per texture.
//   - getAtlasRegion() / isInAtlas() are O(1) lookups (linear scan bounded by
//     MAX_ATLAS_ENTRIES, but in practice << 512 entries and called rarely).
//   - remapUVs() is the only per-frame function (inlined in the header).
//   - No heap allocation. All state is file-static fixed-size arrays.
//
// Tier support: LEGACY (OpenGL 3.3). glTexSubImage2D is core in GL 2.0+.

#include "renderer/texture_atlas.h"
#include "renderer/rhi.h"
#include "core/logging.h"

namespace ffe::renderer {

// --- Atlas page state ---
struct AtlasPage {
    rhi::TextureHandle texture;  // RHI handle for the page texture
    i32 cursorX     = 0;         // Current X position in the shelf
    i32 cursorY     = 0;         // Current Y position (top of current shelf)
    i32 shelfHeight = 0;         // Height of the tallest texture in the current shelf
    bool allocated  = false;
};

// --- Per-texture entry in the atlas lookup table ---
struct AtlasEntry {
    rhi::TextureHandle sourceTexture;  // Original texture handle (key)
    AtlasRegion region;                // Where it lives in the atlas
    bool used = false;
};

// --- Module state (file-static, no heap) ---
static struct {
    AtlasPage pages[MAX_ATLAS_PAGES];
    AtlasEntry entries[MAX_ATLAS_ENTRIES];
    u32 entryCount   = 0;
    u32 pageCount    = 0;
    i32 pageSize     = 0;  // Width == height of each atlas page
    bool initialized = false;
} s_atlas = {};

// --- Internal helpers ---

// Allocate a new atlas page. Returns the page index, or -1 on failure.
static i32 allocatePage() {
    if (s_atlas.pageCount >= MAX_ATLAS_PAGES) {
        FFE_LOG_WARN("texture_atlas", "All %u atlas pages exhausted", MAX_ATLAS_PAGES);
        return -1;
    }

    const u32 idx = s_atlas.pageCount;
    AtlasPage& page = s_atlas.pages[idx];

    // Create an empty RGBA8 texture for the atlas page.
    // NEAREST filter to avoid blending between packed sprites at edges.
    // CLAMP_TO_EDGE to avoid wrap-around sampling.
    rhi::TextureDesc desc;
    desc.width      = static_cast<u32>(s_atlas.pageSize);
    desc.height     = static_cast<u32>(s_atlas.pageSize);
    desc.format     = rhi::TextureFormat::RGBA8;
    desc.filter     = rhi::TextureFilter::NEAREST;
    desc.wrap       = rhi::TextureWrap::CLAMP_TO_EDGE;
    desc.pixelData  = nullptr;  // Empty — we'll fill regions via glTexSubImage2D

    page.texture = rhi::createTexture(desc);
    if (!rhi::isValid(page.texture)) {
        FFE_LOG_ERROR("texture_atlas", "Failed to create atlas page %u (%dx%d)",
                      idx, s_atlas.pageSize, s_atlas.pageSize);
        return -1;
    }

    page.cursorX     = 0;
    page.cursorY     = 0;
    page.shelfHeight = 0;
    page.allocated   = true;

    s_atlas.pageCount++;

    FFE_LOG_INFO("texture_atlas", "Allocated atlas page %u (%dx%d RGBA8)",
                 idx, s_atlas.pageSize, s_atlas.pageSize);
    return static_cast<i32>(idx);
}

// Try to pack a rectangle (w x h) into the given page using shelf packing.
// On success, sets outX/outY to the position and returns true.
// On failure (page full), returns false.
static bool packIntoPage(AtlasPage& page, const i32 w, const i32 h,
                         i32& outX, i32& outY) {
    const i32 paddedW = w + static_cast<i32>(ATLAS_PADDING);
    const i32 paddedH = h + static_cast<i32>(ATLAS_PADDING);

    // Does it fit in the current shelf?
    if (page.cursorX + paddedW <= s_atlas.pageSize) {
        // Fits horizontally. Check vertical space.
        const i32 requiredY = page.cursorY + paddedH;
        if (requiredY <= s_atlas.pageSize) {
            outX = page.cursorX;
            outY = page.cursorY;
            page.cursorX += paddedW;
            if (paddedH > page.shelfHeight) {
                page.shelfHeight = paddedH;
            }
            return true;
        }
    }

    // Current shelf is full horizontally. Try starting a new shelf.
    const i32 newShelfY = page.cursorY + page.shelfHeight;
    if (newShelfY + paddedH <= s_atlas.pageSize && paddedW <= s_atlas.pageSize) {
        page.cursorX     = paddedW;
        page.cursorY     = newShelfY;
        page.shelfHeight = paddedH;
        outX = 0;
        outY = newShelfY;
        return true;
    }

    // Page is full.
    return false;
}

// --- Public API ---

void initAtlas(const i32 maxSize) {
    if (s_atlas.initialized) {
        FFE_LOG_WARN("texture_atlas", "initAtlas called but atlas already initialized");
        return;
    }

    // Clamp to a reasonable range. Minimum 256, maximum 4096.
    i32 size = maxSize;
    if (size < 256)  size = 256;
    if (size > 4096) size = 4096;

    // Ensure power-of-two (round down to nearest PoT)
    i32 pot = 256;
    while (pot * 2 <= size) {
        pot *= 2;
    }
    size = pot;

    s_atlas.pageSize    = size;
    s_atlas.pageCount   = 0;
    s_atlas.entryCount  = 0;
    s_atlas.initialized = true;

    // Pre-allocate the first page
    const i32 firstPage = allocatePage();
    if (firstPage < 0) {
        FFE_LOG_ERROR("texture_atlas", "Failed to allocate initial atlas page");
        s_atlas.initialized = false;
        return;
    }

    FFE_LOG_INFO("texture_atlas", "Atlas initialized: page size %dx%d, max %u pages",
                 size, size, MAX_ATLAS_PAGES);
}

void shutdownAtlas() {
    if (!s_atlas.initialized) return;

    for (u32 i = 0; i < s_atlas.pageCount; ++i) {
        if (s_atlas.pages[i].allocated && rhi::isValid(s_atlas.pages[i].texture)) {
            rhi::destroyTexture(s_atlas.pages[i].texture);
        }
        s_atlas.pages[i] = AtlasPage{};
    }

    for (u32 i = 0; i < s_atlas.entryCount; ++i) {
        s_atlas.entries[i] = AtlasEntry{};
    }

    s_atlas.pageCount   = 0;
    s_atlas.entryCount  = 0;
    s_atlas.pageSize    = 0;
    s_atlas.initialized = false;

    FFE_LOG_INFO("texture_atlas", "Atlas shut down");
}

AtlasRegion addToAtlas(const rhi::TextureHandle tex) {
    AtlasRegion invalid;
    invalid.page = -1;

    if (!s_atlas.initialized) return invalid;
    if (!rhi::isValid(tex)) return invalid;

    // Check if already packed
    for (u32 i = 0; i < s_atlas.entryCount; ++i) {
        if (s_atlas.entries[i].used && s_atlas.entries[i].sourceTexture.id == tex.id) {
            return s_atlas.entries[i].region;
        }
    }

    // Check entry table capacity
    if (s_atlas.entryCount >= MAX_ATLAS_ENTRIES) {
        FFE_LOG_WARN("texture_atlas", "Atlas entry table full (%u entries)", MAX_ATLAS_ENTRIES);
        return invalid;
    }

    // Query texture dimensions via RHI
    const u32 texW = rhi::getTextureWidth(tex);
    const u32 texH = rhi::getTextureHeight(tex);
    if (texW == 0 || texH == 0) return invalid;

    // Reject textures that are too large for atlas packing
    if (texW > MAX_ATLAS_SPRITE_DIM || texH > MAX_ATLAS_SPRITE_DIM) {
        return invalid;
    }

    // Reject textures that are larger than the atlas page itself
    if (static_cast<i32>(texW) > s_atlas.pageSize ||
        static_cast<i32>(texH) > s_atlas.pageSize) {
        return invalid;
    }

    // Try to pack into an existing page
    const i32 w = static_cast<i32>(texW);
    const i32 h = static_cast<i32>(texH);
    i32 packX = 0;
    i32 packY = 0;
    i32 targetPage = -1;

    for (u32 p = 0; p < s_atlas.pageCount; ++p) {
        if (packIntoPage(s_atlas.pages[p], w, h, packX, packY)) {
            targetPage = static_cast<i32>(p);
            break;
        }
    }

    // If no existing page has space, allocate a new one
    if (targetPage < 0) {
        targetPage = allocatePage();
        if (targetPage < 0) return invalid;  // All pages exhausted

        if (!packIntoPage(s_atlas.pages[static_cast<u32>(targetPage)], w, h, packX, packY)) {
            // Should never happen for a fresh page (texture fits in page size)
            FFE_LOG_ERROR("texture_atlas", "Failed to pack %ux%u into fresh page %d",
                          texW, texH, targetPage);
            return invalid;
        }
    }

    // Upload the texture data into the atlas page via rhi::updateTextureSubImage.
    // We read pixel data from the source texture via rhi::readTexturePixels
    // (cold-path glGetTexImage). This is acceptable because addToAtlas is cold-path.
    //
    // In headless mode, readTexturePixels returns false and we skip the upload.
    // The UV math is still correct for tests.
    {
        // Static buffer for pixel readback. Max 512*512*4 = 1 MB.
        // Single-threaded render thread — no contention.
        static u8 s_pixelBuffer[MAX_ATLAS_SPRITE_DIM * MAX_ATLAS_SPRITE_DIM * 4];
        const u32 bufferSize = texW * texH * 4u;

        if (rhi::readTexturePixels(tex, s_pixelBuffer, bufferSize)) {
            rhi::updateTextureSubImage(
                s_atlas.pages[static_cast<u32>(targetPage)].texture,
                static_cast<u32>(packX), static_cast<u32>(packY),
                texW, texH, s_pixelBuffer);
        }
    }

    // Compute atlas-space UV coordinates
    const f32 invSize = 1.0f / static_cast<f32>(s_atlas.pageSize);
    AtlasRegion region;
    region.u0   = static_cast<f32>(packX) * invSize;
    region.v0   = static_cast<f32>(packY) * invSize;
    region.u1   = static_cast<f32>(packX + w) * invSize;
    region.v1   = static_cast<f32>(packY + h) * invSize;
    region.page = targetPage;

    // Store in the lookup table
    AtlasEntry& entry = s_atlas.entries[s_atlas.entryCount];
    entry.sourceTexture = tex;
    entry.region        = region;
    entry.used          = true;
    s_atlas.entryCount++;

    return region;
}

AtlasRegion getAtlasRegion(const rhi::TextureHandle tex) {
    AtlasRegion invalid;
    invalid.page = -1;

    if (!s_atlas.initialized) return invalid;
    if (!rhi::isValid(tex)) return invalid;

    for (u32 i = 0; i < s_atlas.entryCount; ++i) {
        if (s_atlas.entries[i].used && s_atlas.entries[i].sourceTexture.id == tex.id) {
            return s_atlas.entries[i].region;
        }
    }
    return invalid;
}

rhi::TextureHandle getAtlasPageTexture(const i32 page) {
    if (page < 0 || static_cast<u32>(page) >= s_atlas.pageCount) {
        return rhi::TextureHandle{0};
    }
    return s_atlas.pages[static_cast<u32>(page)].texture;
}

bool isInAtlas(const rhi::TextureHandle tex) {
    if (!s_atlas.initialized) return false;
    if (!rhi::isValid(tex)) return false;

    for (u32 i = 0; i < s_atlas.entryCount; ++i) {
        if (s_atlas.entries[i].used && s_atlas.entries[i].sourceTexture.id == tex.id) {
            return true;
        }
    }
    return false;
}

f32 getAtlasUtilization() {
    if (!s_atlas.initialized || s_atlas.pageCount == 0) return 0.0f;

    const f32 pageArea = static_cast<f32>(s_atlas.pageSize) * static_cast<f32>(s_atlas.pageSize);
    f32 totalUsed = 0.0f;

    for (u32 p = 0; p < s_atlas.pageCount; ++p) {
        const AtlasPage& page = s_atlas.pages[p];
        // Approximate used area: full shelves below cursor + current shelf partial
        const f32 fullShelvesArea = static_cast<f32>(page.cursorY) * static_cast<f32>(s_atlas.pageSize);
        const f32 currentShelfArea = static_cast<f32>(page.cursorX) * static_cast<f32>(page.shelfHeight);
        totalUsed += fullShelvesArea + currentShelfArea;
    }

    const f32 totalArea = pageArea * static_cast<f32>(s_atlas.pageCount);
    if (totalArea <= 0.0f) return 0.0f;
    const f32 util = totalUsed / totalArea;
    return util > 1.0f ? 1.0f : util;
}

i32 getAtlasPageSize() {
    return s_atlas.pageSize;
}

u32 getAtlasPageCount() {
    return s_atlas.pageCount;
}

} // namespace ffe::renderer
