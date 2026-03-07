#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"

namespace ffe::renderer {

// Maximum atlas pages. Each page is a single GPU texture (e.g. 2048x2048 RGBA8).
// 4 pages = up to 4 * 2048 * 2048 * 4 = 64 MB of atlas VRAM.
inline constexpr u32 MAX_ATLAS_PAGES = 4;

// Maximum individual textures tracked in the atlas lookup table.
// Textures beyond this limit fall back to non-atlas rendering.
inline constexpr u32 MAX_ATLAS_ENTRIES = 512;

// Maximum texture dimension eligible for atlas packing.
// Textures larger than this in either dimension are not packed (too wasteful).
inline constexpr u32 MAX_ATLAS_SPRITE_DIM = 512;

// 1-pixel border around each packed sprite to prevent texture bleeding
// when sampling at sub-pixel coordinates.
inline constexpr u32 ATLAS_PADDING = 1;

// --- Atlas region: describes where a texture lives inside an atlas page ---
struct AtlasRegion {
    f32 u0 = 0.0f;   // Top-left U in atlas space [0, 1]
    f32 v0 = 0.0f;   // Top-left V in atlas space [0, 1]
    f32 u1 = 1.0f;   // Bottom-right U in atlas space [0, 1]
    f32 v1 = 1.0f;   // Bottom-right V in atlas space [0, 1]
    i32 page = -1;    // Atlas page index (-1 = not in atlas)
};

// --- Public API ---

// Initialize the atlas system. Creates the first atlas page texture.
// maxSize is the width/height of each atlas page (must be power-of-two, max 4096).
// The actual size is clamped to GL_MAX_TEXTURE_SIZE if smaller.
// Must be called after RHI init (needs GL context for texture creation).
void initAtlas(i32 maxSize = 2048);

// Shut down the atlas system. Destroys all atlas page textures.
void shutdownAtlas();

// Try to pack a texture into the atlas. Returns the region if successful.
// If the texture is already packed, returns the cached region.
// If the texture is too large or the atlas is full, returns a region with page == -1.
// This is a COLD-PATH function (performs glTexSubImage2D). Call at load time, not per-frame.
AtlasRegion addToAtlas(rhi::TextureHandle tex);

// Look up the atlas region for a previously packed texture.
// Returns a region with page == -1 if the texture is not in the atlas.
AtlasRegion getAtlasRegion(rhi::TextureHandle tex);

// Get the RHI texture handle for an atlas page.
// Returns an invalid handle if the page index is out of range or not yet created.
rhi::TextureHandle getAtlasPageTexture(i32 page);

// Check if a texture has been packed into the atlas.
bool isInAtlas(rhi::TextureHandle tex);

// Returns the utilization of all atlas pages as a fraction [0.0, 1.0].
// 0.0 = entirely empty, 1.0 = all pages fully used.
f32 getAtlasUtilization();

// Returns the atlas page size (width == height) in pixels. 0 if not initialized.
i32 getAtlasPageSize();

// Returns the number of atlas pages currently allocated.
u32 getAtlasPageCount();

// Remap original texture UVs to atlas-space UVs.
// Given a sprite's original UV range [uvMin, uvMax] in [0,1] texture space,
// and the atlas region for that texture, compute the atlas-space UVs.
// This is a HOT-PATH inline function (called per-sprite per-frame).
inline void remapUVs(const AtlasRegion& region,
                     f32 origUMin, f32 origVMin, f32 origUMax, f32 origVMax,
                     f32& outUMin, f32& outVMin, f32& outUMax, f32& outVMax) {
    const f32 atlasW = region.u1 - region.u0;
    const f32 atlasH = region.v1 - region.v0;
    outUMin = region.u0 + origUMin * atlasW;
    outVMin = region.v0 + origVMin * atlasH;
    outUMax = region.u0 + origUMax * atlasW;
    outVMax = region.v0 + origVMax * atlasH;
}

} // namespace ffe::renderer
