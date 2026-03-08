#include "core/arena_allocator.h"
#include "core/logging.h"

#include <cstdlib>  // free / _aligned_malloc / _aligned_free

// Aligned allocation portability:
// std::aligned_alloc is C++17 but is not in the std:: namespace on MinGW (GCC/Windows CRT).
// _aligned_malloc/_aligned_free are MSVC/MinGW Windows extensions available in <malloc.h>.
// macOS does not require size to be a multiple of alignment for posix_memalign.
// ::aligned_alloc (C11, no std:: prefix) is the POSIX/Linux approach.
// We branch on _WIN32 and __APPLE__ to handle all targets cleanly.
#ifdef _WIN32
#  include <malloc.h>
#  define FFE_ALIGNED_ALLOC(align, size) _aligned_malloc((size), (align))
#  define FFE_ALIGNED_FREE(ptr)          _aligned_free(ptr)
#elif defined(__APPLE__)
// posix_memalign has no size-must-be-multiple-of-alignment constraint.
// Available on all macOS versions.
static inline void* ffe_apple_aligned_alloc(std::size_t align, std::size_t size) noexcept {
    void* ptr = nullptr;
    return (::posix_memalign(&ptr, align, size) == 0) ? ptr : nullptr;
}
#  define FFE_ALIGNED_ALLOC(align, size) ffe_apple_aligned_alloc((align), (size))
#  define FFE_ALIGNED_FREE(ptr)          ::free(ptr)
#else
#  define FFE_ALIGNED_ALLOC(align, size) ::aligned_alloc((align), (size))
#  define FFE_ALIGNED_FREE(ptr)          ::free(ptr)
#endif

namespace ffe {

ArenaAllocator::ArenaAllocator(const size_t capacityBytes)
    : m_buffer(nullptr)
    , m_capacity(capacityBytes)
    , m_offset(0)
{
    // Align the buffer to a cache line boundary (64 bytes).
    // aligned_alloc (POSIX/Linux) requires size to be a multiple of alignment.
    constexpr size_t CACHE_LINE = 64;
    const size_t alignedSize = (capacityBytes + CACHE_LINE - 1) & ~(CACHE_LINE - 1);
    m_buffer = static_cast<uint8_t*>(
        FFE_ALIGNED_ALLOC(CACHE_LINE, alignedSize)
    );

    if (!m_buffer) {
        FFE_LOG_FATAL("Arena", "Failed to allocate %zu bytes for arena", capacityBytes);
        // In a no-exceptions world, we set capacity to 0 and every allocate() returns nullptr.
        m_capacity = 0;
    }
}

ArenaAllocator::~ArenaAllocator() {
    FFE_ALIGNED_FREE(m_buffer);
}

} // namespace ffe
