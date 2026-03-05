#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

struct DrawCommand {
    // --- Sort key (8 bytes) ---
    u64 sortKey = 0;

    // --- GPU resource references (8 bytes) ---
    rhi::ShaderHandle shader;
    rhi::TextureHandle texture;

    // --- Geometry (12 bytes) ---
    rhi::BufferHandle vertexBuffer;
    u32 vertexOffset = 0;
    u32 vertexCount  = 0;

    // --- Index buffer (8 bytes) ---
    rhi::BufferHandle indexBuffer;
    u32 indexOffset = 0;

    // --- Transform (16 bytes) ---
    f32 posX = 0.0f;
    f32 posY = 0.0f;
    f32 scaleX = 1.0f;
    f32 scaleY = 1.0f;

    // --- Pipeline state + padding to 64 bytes ---
    // 52 bytes used above + 1 (pipelineBits) + 11 (reserved) = 64
    u8 pipelineBits = 0;
    u8 reserved[11] = {};
};

static_assert(sizeof(DrawCommand) == 64, "DrawCommand must be exactly one cache line");

// Sort key encoding
inline u64 makeSortKey(const u8 layer, const u8 shaderId, const u16 textureId,
                       const f32 depth, const u16 subOrder = 0) {
    const u32 depthBits = static_cast<u32>(glm::clamp(depth, 0.0f, 1.0f) * 16777215.0f);

    return (static_cast<u64>(layer & 0xF) << 60)
         | (static_cast<u64>(shaderId)    << 52)
         | (static_cast<u64>(textureId & 0xFFF) << 40)
         | (static_cast<u64>(depthBits & 0xFFFFFF) << 16)
         | static_cast<u64>(subOrder);
}

// Pipeline bit packing
inline rhi::PipelineState unpackPipelineBits(const u8 bits) {
    rhi::PipelineState ps;
    ps.blend      = static_cast<rhi::BlendMode>((bits >> 6) & 0x3);
    ps.depth      = static_cast<rhi::DepthFunc>((bits >> 4) & 0x3);
    ps.cull       = static_cast<rhi::CullMode>((bits >> 2) & 0x3);
    ps.depthWrite = static_cast<bool>((bits >> 1) & 0x1);
    ps.wireframe  = static_cast<bool>(bits & 0x1);
    return ps;
}

inline u8 packPipelineBits(const rhi::PipelineState& ps) {
    return static_cast<u8>(
        (static_cast<u8>(ps.blend) << 6)
      | (static_cast<u8>(ps.depth) << 4)
      | (static_cast<u8>(ps.cull)  << 2)
      | (static_cast<u8>(ps.depthWrite) << 1)
      | static_cast<u8>(ps.wireframe)
    );
}

// Render queue — flat, pre-allocated array of draw commands.
// Owns its storage. Allocated once at init, reset (count = 0) each frame.
// No arena dependency — avoids use-after-free from frame allocator lifecycle.
struct RenderQueue {
    DrawCommand* commands = nullptr;
    u32 count    = 0;
    u32 capacity = 0;

    void clear() { count = 0; }
};

// Allocate the backing store for a render queue. Call once at startup.
// Uses a single heap allocation that lives for the lifetime of the queue.
void initRenderQueue(RenderQueue& queue, u32 maxCommands);

// Free the backing store. Call once at shutdown.
void destroyRenderQueue(RenderQueue& queue);

// Maximum draw commands per frame per tier
inline constexpr u32 MAX_DRAW_COMMANDS_LEGACY   = 8192;
inline constexpr u32 MAX_DRAW_COMMANDS_STANDARD = 32768;
inline constexpr u32 MAX_DRAW_COMMANDS_MODERN   = 131072;

// Push a draw command into the queue
inline void pushDrawCommand(RenderQueue& queue, const DrawCommand& cmd) {
    if (queue.count < queue.capacity) {
        queue.commands[queue.count++] = cmd;
    }
}

// Sort and submit
void sortRenderQueue(RenderQueue& queue);
void submitRenderQueue(const RenderQueue& queue);

} // namespace ffe::renderer
