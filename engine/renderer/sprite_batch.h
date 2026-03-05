#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

// Maximum sprites per batch
inline constexpr u32 MAX_SPRITES_PER_BATCH = 2048;
inline constexpr u32 MAX_BATCH_VERTICES = MAX_SPRITES_PER_BATCH * 4;
inline constexpr u32 MAX_BATCH_INDICES  = MAX_SPRITES_PER_BATCH * 6;

// A single sprite instance to be batched
struct SpriteInstance {
    glm::vec2 position;     // World position (center of sprite)
    glm::vec2 size;         // Width, height in world units
    glm::vec2 uvMin;        // Top-left UV
    glm::vec2 uvMax;        // Bottom-right UV
    glm::vec4 color;        // Tint color
    f32 rotation;           // Radians, counter-clockwise
    f32 depth;              // Z-order (0.0 = back, 1.0 = front)
};

struct SpriteBatch {
    // CPU-side vertex staging buffer
    rhi::SpriteVertex* vertices = nullptr;
    u32 vertexCount             = 0;

    // GPU resources
    rhi::BufferHandle vertexBuffer;
    rhi::BufferHandle indexBuffer;
    rhi::ShaderHandle shader;

    // State tracking
    rhi::TextureHandle currentTexture;
    u32 spriteCount     = 0;
    u32 drawCallCount   = 0;
};

// Initialize the sprite batch. Creates GPU buffers.
void initSpriteBatch(SpriteBatch& batch, rhi::ShaderHandle spriteShader);

// Shutdown — destroy GPU buffers.
void shutdownSpriteBatch(SpriteBatch& batch);

// Begin a new frame of sprite rendering. Resets counters.
void beginSpriteBatch(SpriteBatch& batch, rhi::SpriteVertex* vertexStaging);

// Add a sprite to the batch.
void addSprite(SpriteBatch& batch, rhi::TextureHandle texture, const SpriteInstance& sprite);

// Flush any remaining sprites. Must be called after all sprites are added.
void endSpriteBatch(SpriteBatch& batch);

} // namespace ffe::renderer
