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


} // namespace ffe::renderer
