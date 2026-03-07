// test_texture_atlas.cpp -- Unit tests for the runtime texture atlas.
//
// Tests run in headless mode (no GL context). The atlas initializes in headless
// because rhi::createTexture() returns valid headless handles. Pixel upload
// (readTexturePixels / updateTextureSubImage) is a no-op in headless, but the
// packing math and UV remapping are fully exercised.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "renderer/texture_atlas.h"
#include "renderer/rhi.h"
#include "core/types.h"

using namespace ffe;
using namespace ffe::renderer;
using namespace ffe::rhi;

// Helper: create a headless texture with given dimensions.
// In headless mode, createTexture returns incrementing handle IDs.
static TextureHandle makeTexture(const u32 w, const u32 h) {
    TextureDesc desc;
    desc.width  = w;
    desc.height = h;
    desc.format = TextureFormat::RGBA8;
    return createTexture(desc);
}

// Helper: init RHI in headless mode if not already done.
// Called once by the first test that runs. Harmless if called again.
static bool s_rhiReady = false;
static void ensureRhi() {
    if (s_rhiReady) return;
    RhiConfig cfg;
    cfg.headless = true;
    init(cfg);
    s_rhiReady = true;
}

// --- Atlas Lifecycle ---

TEST_CASE("Atlas init and shutdown", "[texture_atlas]") {
    ensureRhi();
    initAtlas(2048);
    REQUIRE(getAtlasPageSize() == 2048);
    REQUIRE(getAtlasPageCount() == 1); // One page pre-allocated
    shutdownAtlas();
    REQUIRE(getAtlasPageSize() == 0);
    REQUIRE(getAtlasPageCount() == 0);
}

TEST_CASE("Atlas double init is safe", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);
    initAtlas(512); // Should warn and do nothing
    REQUIRE(getAtlasPageSize() == 1024); // First init wins
    shutdownAtlas();
}

TEST_CASE("Atlas shutdown without init is safe", "[texture_atlas]") {
    ensureRhi();
    shutdownAtlas(); // No-op, should not crash
    REQUIRE(getAtlasPageSize() == 0);
}

TEST_CASE("Atlas page size clamps to power-of-two", "[texture_atlas]") {
    ensureRhi();

    SECTION("Below minimum") {
        initAtlas(100);
        REQUIRE(getAtlasPageSize() == 256); // Minimum is 256
        shutdownAtlas();
    }

    SECTION("Above maximum") {
        initAtlas(8192);
        REQUIRE(getAtlasPageSize() == 4096); // Maximum is 4096
        shutdownAtlas();
    }

    SECTION("Non-power-of-two rounds down") {
        initAtlas(1500);
        REQUIRE(getAtlasPageSize() == 1024); // Rounded down to 1024
        shutdownAtlas();
    }

    SECTION("Exact power-of-two") {
        initAtlas(512);
        REQUIRE(getAtlasPageSize() == 512);
        shutdownAtlas();
    }
}

// --- Single Texture Packing ---

TEST_CASE("Single texture packing returns valid region", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);

    const TextureHandle tex = makeTexture(64, 64);
    const AtlasRegion region = addToAtlas(tex);

    REQUIRE(region.page == 0);
    REQUIRE(region.u0 >= 0.0f);
    REQUIRE(region.v0 >= 0.0f);
    REQUIRE(region.u1 > region.u0);
    REQUIRE(region.v1 > region.v0);
    REQUIRE(region.u1 <= 1.0f);
    REQUIRE(region.v1 <= 1.0f);

    // Check UV region size matches texture size relative to atlas
    const f32 expectedSize = 64.0f / 1024.0f;
    REQUIRE_THAT(region.u1 - region.u0, Catch::Matchers::WithinAbs(expectedSize, 0.001));
    REQUIRE_THAT(region.v1 - region.v0, Catch::Matchers::WithinAbs(expectedSize, 0.001));

    shutdownAtlas();
}

TEST_CASE("Packing same texture twice returns cached region", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);

    const TextureHandle tex = makeTexture(32, 32);
    const AtlasRegion r1 = addToAtlas(tex);
    const AtlasRegion r2 = addToAtlas(tex);

    REQUIRE(r1.page == r2.page);
    REQUIRE_THAT(r1.u0, Catch::Matchers::WithinAbs(r2.u0, 0.0001));
    REQUIRE_THAT(r1.v0, Catch::Matchers::WithinAbs(r2.v0, 0.0001));
    REQUIRE_THAT(r1.u1, Catch::Matchers::WithinAbs(r2.u1, 0.0001));
    REQUIRE_THAT(r1.v1, Catch::Matchers::WithinAbs(r2.v1, 0.0001));

    shutdownAtlas();
}

TEST_CASE("isInAtlas returns correct state", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);

    const TextureHandle tex1 = makeTexture(32, 32);
    const TextureHandle tex2 = makeTexture(32, 32);

    REQUIRE_FALSE(isInAtlas(tex1));
    addToAtlas(tex1);
    REQUIRE(isInAtlas(tex1));
    REQUIRE_FALSE(isInAtlas(tex2));

    shutdownAtlas();
}

TEST_CASE("getAtlasRegion for unpacked texture returns invalid", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);

    const TextureHandle tex = makeTexture(32, 32);
    const AtlasRegion region = getAtlasRegion(tex);
    REQUIRE(region.page == -1);

    shutdownAtlas();
}

// --- Multiple Texture Packing ---

TEST_CASE("Multiple textures pack without overlap", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);

    const TextureHandle tex1 = makeTexture(100, 100);
    const TextureHandle tex2 = makeTexture(100, 100);
    const TextureHandle tex3 = makeTexture(100, 100);

    const AtlasRegion r1 = addToAtlas(tex1);
    const AtlasRegion r2 = addToAtlas(tex2);
    const AtlasRegion r3 = addToAtlas(tex3);

    REQUIRE(r1.page >= 0);
    REQUIRE(r2.page >= 0);
    REQUIRE(r3.page >= 0);

    // If on the same page, regions must not overlap
    if (r1.page == r2.page) {
        // Either r1 is entirely left of r2, or entirely above
        const bool noOverlap = (r1.u1 <= r2.u0 + 0.001f) || (r2.u1 <= r1.u0 + 0.001f) ||
                               (r1.v1 <= r2.v0 + 0.001f) || (r2.v1 <= r1.v0 + 0.001f);
        REQUIRE(noOverlap);
    }

    shutdownAtlas();
}

// --- Overflow Handling ---

TEST_CASE("Texture too large for atlas is rejected", "[texture_atlas]") {
    ensureRhi();
    initAtlas(512);

    // MAX_ATLAS_SPRITE_DIM is 512, but a 513x513 texture should be rejected
    const TextureHandle bigTex = makeTexture(513, 513);
    const AtlasRegion region = addToAtlas(bigTex);
    REQUIRE(region.page == -1);
    REQUIRE_FALSE(isInAtlas(bigTex));

    shutdownAtlas();
}

TEST_CASE("Texture larger than atlas page is rejected", "[texture_atlas]") {
    ensureRhi();
    initAtlas(256); // Small atlas

    // 300x300 is within MAX_ATLAS_SPRITE_DIM (512) but larger than page (256)
    const TextureHandle tex = makeTexture(300, 300);
    const AtlasRegion region = addToAtlas(tex);
    REQUIRE(region.page == -1);

    shutdownAtlas();
}

TEST_CASE("Invalid texture handle returns invalid region", "[texture_atlas]") {
    ensureRhi();
    initAtlas(1024);

    const TextureHandle invalid{0};
    const AtlasRegion region = addToAtlas(invalid);
    REQUIRE(region.page == -1);

    shutdownAtlas();
}

// --- UV Remapping ---

TEST_CASE("remapUVs identity (full texture)", "[texture_atlas]") {
    // If the original UV covers the full texture [0,0]-[1,1],
    // the remapped UV should equal the atlas region exactly.
    const AtlasRegion region{0.25f, 0.25f, 0.5f, 0.5f, 0};

    f32 outUMin = 0.0f, outVMin = 0.0f, outUMax = 0.0f, outVMax = 0.0f;
    remapUVs(region, 0.0f, 0.0f, 1.0f, 1.0f, outUMin, outVMin, outUMax, outVMax);

    REQUIRE_THAT(outUMin, Catch::Matchers::WithinAbs(0.25, 0.0001));
    REQUIRE_THAT(outVMin, Catch::Matchers::WithinAbs(0.25, 0.0001));
    REQUIRE_THAT(outUMax, Catch::Matchers::WithinAbs(0.5, 0.0001));
    REQUIRE_THAT(outVMax, Catch::Matchers::WithinAbs(0.5, 0.0001));
}

TEST_CASE("remapUVs sub-region (sprite sheet frame)", "[texture_atlas]") {
    // Atlas region: [0.0, 0.0] to [0.5, 0.5] (texture occupies top-left quarter of atlas)
    // Original UV: [0.25, 0.0] to [0.5, 0.5] (right half of the sprite sheet)
    // Expected: [0.125, 0.0] to [0.25, 0.25] in atlas space
    const AtlasRegion region{0.0f, 0.0f, 0.5f, 0.5f, 0};

    f32 outUMin = 0.0f, outVMin = 0.0f, outUMax = 0.0f, outVMax = 0.0f;
    remapUVs(region, 0.25f, 0.0f, 0.5f, 0.5f, outUMin, outVMin, outUMax, outVMax);

    REQUIRE_THAT(outUMin, Catch::Matchers::WithinAbs(0.125, 0.0001));
    REQUIRE_THAT(outVMin, Catch::Matchers::WithinAbs(0.0, 0.0001));
    REQUIRE_THAT(outUMax, Catch::Matchers::WithinAbs(0.25, 0.0001));
    REQUIRE_THAT(outVMax, Catch::Matchers::WithinAbs(0.25, 0.0001));
}

TEST_CASE("remapUVs preserves zero-origin", "[texture_atlas]") {
    const AtlasRegion region{0.1f, 0.2f, 0.3f, 0.4f, 0};

    f32 outUMin = 0.0f, outVMin = 0.0f, outUMax = 0.0f, outVMax = 0.0f;
    remapUVs(region, 0.0f, 0.0f, 0.0f, 0.0f, outUMin, outVMin, outUMax, outVMax);

    REQUIRE_THAT(outUMin, Catch::Matchers::WithinAbs(0.1, 0.0001));
    REQUIRE_THAT(outVMin, Catch::Matchers::WithinAbs(0.2, 0.0001));
    REQUIRE_THAT(outUMax, Catch::Matchers::WithinAbs(0.1, 0.0001));
    REQUIRE_THAT(outVMax, Catch::Matchers::WithinAbs(0.2, 0.0001));
}

// --- Shelf Packing Row Advancement ---

TEST_CASE("Shelf packing advances to new row when shelf is full", "[texture_atlas]") {
    ensureRhi();
    initAtlas(256); // Small atlas: 256x256

    // Fill the first shelf with 100px-wide textures (fits ~2 per row with padding)
    const TextureHandle tex1 = makeTexture(120, 50);
    const TextureHandle tex2 = makeTexture(120, 50);
    const TextureHandle tex3 = makeTexture(120, 50); // Should start new row

    const AtlasRegion r1 = addToAtlas(tex1);
    const AtlasRegion r2 = addToAtlas(tex2);
    const AtlasRegion r3 = addToAtlas(tex3);

    REQUIRE(r1.page >= 0);
    REQUIRE(r2.page >= 0);
    REQUIRE(r3.page >= 0);

    // r1 and r2 should be on the same row (same v0)
    REQUIRE_THAT(r1.v0, Catch::Matchers::WithinAbs(r2.v0, 0.001));

    // r3 should be on a different row (different v0)
    REQUIRE(r3.v0 > r1.v0 + 0.01f);

    shutdownAtlas();
}

// --- Atlas Page Texture Handle ---

TEST_CASE("getAtlasPageTexture returns valid handle for allocated page", "[texture_atlas]") {
    ensureRhi();
    initAtlas(512);

    const TextureHandle pageTex = getAtlasPageTexture(0);
    REQUIRE(isValid(pageTex));

    shutdownAtlas();
}

TEST_CASE("getAtlasPageTexture returns invalid for out-of-range page", "[texture_atlas]") {
    ensureRhi();
    initAtlas(512);

    const TextureHandle badPage = getAtlasPageTexture(99);
    REQUIRE_FALSE(isValid(badPage));

    const TextureHandle negativePage = getAtlasPageTexture(-1);
    REQUIRE_FALSE(isValid(negativePage));

    shutdownAtlas();
}

// --- Atlas Utilization ---

TEST_CASE("Atlas utilization is zero when empty", "[texture_atlas]") {
    ensureRhi();
    initAtlas(512);

    REQUIRE_THAT(getAtlasUtilization(), Catch::Matchers::WithinAbs(0.0, 0.01));

    shutdownAtlas();
}

TEST_CASE("Atlas utilization increases after packing", "[texture_atlas]") {
    ensureRhi();
    initAtlas(512);

    const f32 before = getAtlasUtilization();
    const TextureHandle tex = makeTexture(128, 128);
    addToAtlas(tex);
    const f32 after = getAtlasUtilization();

    REQUIRE(after > before);
    REQUIRE(after <= 1.0f);
    REQUIRE(after >= 0.0f);

    shutdownAtlas();
}

TEST_CASE("Atlas utilization returns zero when not initialized", "[texture_atlas]") {
    ensureRhi();
    shutdownAtlas(); // Ensure atlas is NOT initialized
    REQUIRE_THAT(getAtlasUtilization(), Catch::Matchers::WithinAbs(0.0, 0.001));
}

// --- Fallback Behavior ---

TEST_CASE("addToAtlas returns invalid when atlas not initialized", "[texture_atlas]") {
    ensureRhi();
    shutdownAtlas(); // Ensure atlas is NOT initialized
    const TextureHandle tex = makeTexture(32, 32);
    const AtlasRegion region = addToAtlas(tex);
    REQUIRE(region.page == -1);
}

TEST_CASE("isInAtlas returns false when atlas not initialized", "[texture_atlas]") {
    ensureRhi();
    shutdownAtlas(); // Ensure atlas is NOT initialized
    const TextureHandle tex = makeTexture(32, 32);
    REQUIRE_FALSE(isInAtlas(tex));
}

// --- Atlas page allocation on overflow ---

TEST_CASE("Atlas allocates new page when first page is full", "[texture_atlas]") {
    ensureRhi();
    initAtlas(256); // Small atlas: 256x256

    // Pack enough textures to fill the first page.
    // Each 120x120 texture (with padding) takes ~121x121 pixels.
    // 256/121 = ~2 per row, 256/121 = ~2 rows, so ~4 textures per page.
    u32 pagesUsed = 1;
    for (u32 i = 0; i < 6; ++i) {
        const TextureHandle tex = makeTexture(120, 120);
        const AtlasRegion r = addToAtlas(tex);
        REQUIRE(r.page >= 0);
        if (static_cast<u32>(r.page) >= pagesUsed) {
            pagesUsed = static_cast<u32>(r.page) + 1;
        }
    }

    // Should have needed more than 1 page
    REQUIRE(pagesUsed > 1);
    REQUIRE(getAtlasPageCount() > 1);

    shutdownAtlas();
}
