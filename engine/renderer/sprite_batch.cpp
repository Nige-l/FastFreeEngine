#include "renderer/sprite_batch.h"
#include "renderer/texture_atlas.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <cmath>

namespace ffe::renderer {

// Internal: upload current vertex data and issue draw call.
// Shader and u_texture0 uniform are already bound in beginSpriteBatch.
// Only rebind texture on change (handled by addSprite calling flushBatch).
static void flushBatch(SpriteBatch& batch) {
    if (batch.spriteCount == 0) return;

    // Upload vertex data to GPU
    const u32 uploadBytes = batch.vertexCount * static_cast<u32>(sizeof(rhi::SpriteVertex));
    rhi::updateBuffer(batch.vertexBuffer, batch.vertices, uploadBytes, 0);

    // Bind the current texture (only changes when addSprite detects a texture switch)
    rhi::bindTexture(batch.currentTexture, 0);

    // Draw
    rhi::drawIndexed(batch.vertexBuffer, batch.indexBuffer,
                     batch.spriteCount * 6, 0, 0);

    batch.drawCallCount++;

    // Reset for next batch (reuse the same staging memory)
    batch.vertexCount = 0;
    batch.spriteCount = 0;
}

void initSpriteBatch(SpriteBatch& batch, const rhi::ShaderHandle spriteShader) {
    batch.shader = spriteShader;

    // Create the dynamic vertex buffer
    rhi::BufferDesc vbDesc;
    vbDesc.type      = rhi::BufferType::VERTEX;
    vbDesc.usage     = rhi::BufferUsage::DYNAMIC;
    vbDesc.sizeBytes = MAX_BATCH_VERTICES * static_cast<u32>(sizeof(rhi::SpriteVertex));
    vbDesc.data      = nullptr;
    batch.vertexBuffer = rhi::createBuffer(vbDesc);

    // Create the static index buffer
    // Pattern: 0,1,2, 2,3,0, 4,5,6, 6,7,4, ...
    u16 indices[MAX_BATCH_INDICES];
    for (u32 i = 0; i < MAX_SPRITES_PER_BATCH; ++i) {
        const u32 base = i * 4;
        const u32 idx  = i * 6;
        indices[idx + 0] = static_cast<u16>(base + 0);
        indices[idx + 1] = static_cast<u16>(base + 1);
        indices[idx + 2] = static_cast<u16>(base + 2);
        indices[idx + 3] = static_cast<u16>(base + 2);
        indices[idx + 4] = static_cast<u16>(base + 3);
        indices[idx + 5] = static_cast<u16>(base + 0);
    }

    rhi::BufferDesc ibDesc;
    ibDesc.type      = rhi::BufferType::INDEX;
    ibDesc.usage     = rhi::BufferUsage::STATIC;
    ibDesc.sizeBytes = static_cast<u32>(sizeof(indices));
    ibDesc.data      = indices;
    batch.indexBuffer = rhi::createBuffer(ibDesc);
}

void shutdownSpriteBatch(SpriteBatch& batch) {
    rhi::destroyBuffer(batch.vertexBuffer);
    rhi::destroyBuffer(batch.indexBuffer);
    batch.vertexBuffer = {};
    batch.indexBuffer = {};
}

void beginSpriteBatch(SpriteBatch& batch, rhi::SpriteVertex* vertexStaging) {
    batch.vertices      = vertexStaging;
    batch.vertexCount   = 0;
    batch.spriteCount   = 0;
    batch.drawCallCount = 0;
    batch.currentTexture = {};
    batch.currentAtlasPage = -1;
    batch.atlasBatchedCount = 0;

    // Bind the sprite shader once for the entire batch.
    // Set the texture sampler uniform once — it always points to unit 0.
    rhi::bindShader(batch.shader);
    rhi::setUniformInt(batch.shader, "u_texture0", 0);
}

void addSprite(SpriteBatch& batch, const rhi::TextureHandle texture, const SpriteInstance& sprite) {
    // --- Atlas resolution ---
    // Try to use the atlas for this texture. If the texture is in the atlas,
    // we bind the atlas page texture instead and remap UVs.
    // This is the key optimization: sprites using different source textures
    // but sharing an atlas page avoid a texture-switch flush.
    rhi::TextureHandle effectiveTexture = texture;
    f32 finalUMin = sprite.uvMin.x;
    f32 finalVMin = sprite.uvMin.y;
    f32 finalUMax = sprite.uvMax.x;
    f32 finalVMax = sprite.uvMax.y;
    i32 atlasPage = -1;

    // Lazily attempt to add the texture to the atlas on first use.
    // addToAtlas returns the cached region immediately for already-packed textures
    // (O(n) lookup where n = number of atlas entries, typically < 100).
    // For textures too large or atlas full, it returns page == -1 (fallback to
    // original per-texture batching — zero behavior change from before atlas).
    const AtlasRegion region = addToAtlas(texture);
    if (region.page >= 0) {
        // Texture is in the atlas — use the atlas page texture
        effectiveTexture = getAtlasPageTexture(region.page);
        atlasPage = region.page;

        // Remap the sprite's original UVs to atlas-space UVs
        remapUVs(region,
                 sprite.uvMin.x, sprite.uvMin.y, sprite.uvMax.x, sprite.uvMax.y,
                 finalUMin, finalVMin, finalUMax, finalVMax);

        batch.atlasBatchedCount++;
    }

    // --- Flush decision ---
    // Flush if the effective texture changed or the batch is full.
    // With atlas batching, sprites on the same atlas page share the same
    // effective texture, so they batch together even if source textures differ.
    if ((effectiveTexture.id != batch.currentTexture.id && batch.spriteCount > 0) ||
        batch.spriteCount >= MAX_SPRITES_PER_BATCH) {
        flushBatch(batch);
    }

    batch.currentTexture = effectiveTexture;
    batch.currentAtlasPage = atlasPage;

    // Compute rotated quad corners
    const f32 hw = sprite.size.x * 0.5f;
    const f32 hh = sprite.size.y * 0.5f;
    // Fast-path: skip trig entirely for the common zero-rotation case.
    // == 0.0f is intentional here — callers set this to the literal 0.0f.
    float cosR, sinR;
    if (sprite.rotation == 0.0f) {
        cosR = 1.0f;
        sinR = 0.0f;
    } else {
        cosR = std::cos(sprite.rotation);
        sinR = std::sin(sprite.rotation);
    }

    // Corners relative to center: TL, TR, BR, BL
    const glm::vec2 offsets[4] = {
        {-hw, -hh}, { hw, -hh}, { hw,  hh}, {-hw,  hh}
    };

    const u32 base = batch.vertexCount;
    for (u32 j = 0; j < 4; ++j) {
        rhi::SpriteVertex& v = batch.vertices[base + j];
        v.x = sprite.position.x + offsets[j].x * cosR - offsets[j].y * sinR;
        v.y = sprite.position.y + offsets[j].x * sinR + offsets[j].y * cosR;
        v.r = sprite.color.r;
        v.g = sprite.color.g;
        v.b = sprite.color.b;
        v.a = sprite.color.a;
    }

    // UVs: TL, TR, BR, BL (using atlas-remapped UVs when applicable)
    batch.vertices[base + 0].u = finalUMin;
    batch.vertices[base + 0].v = finalVMin;
    batch.vertices[base + 1].u = finalUMax;
    batch.vertices[base + 1].v = finalVMin;
    batch.vertices[base + 2].u = finalUMax;
    batch.vertices[base + 2].v = finalVMax;
    batch.vertices[base + 3].u = finalUMin;
    batch.vertices[base + 3].v = finalVMax;

    batch.vertexCount += 4;
    batch.spriteCount++;
}

void endSpriteBatch(SpriteBatch& batch) {
    flushBatch(batch);
}

} // namespace ffe::renderer
