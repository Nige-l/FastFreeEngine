#include "renderer/render_queue.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <tracy/Tracy.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace ffe::renderer {

void initRenderQueue(RenderQueue& queue, const u32 maxCommands) {
    // Single heap allocation — lives until destroyRenderQueue.
    void* const mem = std::malloc(static_cast<size_t>(maxCommands) * sizeof(DrawCommand));
    if (mem == nullptr) {
        FFE_LOG_FATAL("Renderer", "Failed to allocate render queue (%u commands)", maxCommands);
        queue = {};
        return;
    }
    queue.commands = static_cast<DrawCommand*>(mem);
    queue.capacity = maxCommands;
    queue.count    = 0;
}

void destroyRenderQueue(RenderQueue& queue) {
    std::free(queue.commands);
    queue.commands = nullptr;
    queue.capacity = 0;
    queue.count    = 0;
}

void sortRenderQueue(RenderQueue& queue) {
    ZoneScopedN("SortRenderQueue");
    std::sort(queue.commands, queue.commands + queue.count,
        [](const DrawCommand& a, const DrawCommand& b) {
            return a.sortKey < b.sortKey;
        });
}

// NOTE: This function is currently unused — the sprite batch path is used for rendering.
// Retained for future use with draw-call-level render queue submission.
// TODO(session5): remove if no use case emerges.
void submitRenderQueue(const RenderQueue& queue) {
    ZoneScopedN("SubmitRenderQueue");

    rhi::ShaderHandle currentShader   = {};
    rhi::TextureHandle currentTexture = {};
    u8 currentPipeline                = 0xFF; // Force first apply

    for (u32 i = 0; i < queue.count; ++i) {
        const DrawCommand& cmd = queue.commands[i];

        // Change pipeline state only if different
        if (cmd.pipelineBits != currentPipeline) {
            rhi::applyPipelineState(unpackPipelineBits(cmd.pipelineBits));
            currentPipeline = cmd.pipelineBits;
        }

        // Change shader only if different
        if (cmd.shader.id != currentShader.id) {
            rhi::bindShader(cmd.shader);
            currentShader = cmd.shader;
        }

        // Change texture only if different
        if (cmd.texture.id != currentTexture.id) {
            rhi::bindTexture(cmd.texture, 0);
            currentTexture = cmd.texture;
        }

        // Issue draw call
        if (rhi::isValid(cmd.indexBuffer)) {
            rhi::drawIndexed(cmd.vertexBuffer, cmd.indexBuffer,
                             cmd.vertexCount, cmd.vertexOffset, cmd.indexOffset);
        } else {
            rhi::drawArrays(cmd.vertexBuffer, cmd.vertexCount, cmd.vertexOffset);
        }
    }
}

} // namespace ffe::renderer
