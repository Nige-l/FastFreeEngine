#pragma once

#include "core/types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

namespace ffe {

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t capacityBytes);
    ~ArenaAllocator();

    // Non-copyable, non-movable
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) = delete;
    ArenaAllocator& operator=(ArenaAllocator&&) = delete;

    // Allocate `size` bytes with `alignment`.
    // Returns nullptr if the arena is exhausted.
    // Does NOT call constructors — caller must use placement new if needed.
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t));

    // Typed allocation helper — calls placement new with forwarded args.
    template<typename T, typename... Args>
    T* create(Args&&... args);

    // Allocate a contiguous array of `count` elements.
    // Elements are default-constructed.
    template<typename T>
    T* allocateArray(size_t count);

    // Reset the arena. All previous allocations are invalidated.
    // This is O(1) — just resets the offset pointer.
    void reset();

    // Query
    size_t capacity() const;
    size_t used() const;
    size_t remaining() const;

private:
    uint8_t* m_buffer;
    size_t m_capacity;
    size_t m_offset;
};

// --- Inline implementations ---

inline void* ArenaAllocator::allocate(const size_t size, const size_t alignment) {
    // Align the current offset upward
    const size_t alignedOffset = (m_offset + alignment - 1) & ~(alignment - 1);
    const size_t newOffset = alignedOffset + size;

    if (newOffset > m_capacity) {
        return nullptr; // Out of memory — caller must handle
    }

    m_offset = newOffset;
    return m_buffer + alignedOffset;
}

template<typename T, typename... Args>
T* ArenaAllocator::create(Args&&... args) {
    void* const mem = allocate(sizeof(T), alignof(T));
    if (!mem) return nullptr;
    return ::new(mem) T(std::forward<Args>(args)...);
}

template<typename T>
T* ArenaAllocator::allocateArray(const size_t count) {
    void* const mem = allocate(sizeof(T) * count, alignof(T));
    if (!mem) return nullptr;
    T* const arr = static_cast<T*>(mem);
    for (size_t i = 0; i < count; ++i) {
        ::new(arr + i) T();
    }
    return arr;
}

inline void ArenaAllocator::reset() {
    m_offset = 0;
    // Intentionally do NOT zero memory. Valgrind / ASan will catch use-after-reset.
}

inline size_t ArenaAllocator::capacity() const { return m_capacity; }
inline size_t ArenaAllocator::used() const { return m_offset; }
inline size_t ArenaAllocator::remaining() const { return m_capacity - m_offset; }

} // namespace ffe
