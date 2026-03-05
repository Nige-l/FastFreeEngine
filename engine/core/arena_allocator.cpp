#include "core/arena_allocator.h"
#include "core/logging.h"

#include <cstdlib>  // std::aligned_alloc / std::free

namespace ffe {

ArenaAllocator::ArenaAllocator(const size_t capacityBytes)
    : m_buffer(nullptr)
    , m_capacity(capacityBytes)
    , m_offset(0)
{
    // Align the buffer to a cache line boundary (64 bytes)
    m_buffer = static_cast<uint8_t*>(
        std::aligned_alloc(64, capacityBytes)
    );

    if (!m_buffer) {
        FFE_LOG_FATAL("Arena", "Failed to allocate %zu bytes for arena", capacityBytes);
        // In a no-exceptions world, we set capacity to 0 and every allocate() returns nullptr.
        m_capacity = 0;
    }
}

ArenaAllocator::~ArenaAllocator() {
    std::free(m_buffer);
}

} // namespace ffe
