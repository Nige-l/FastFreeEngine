#include <catch2/catch_test_macros.hpp>
#include "core/arena_allocator.h"

TEST_CASE("ArenaAllocator basic allocation", "[arena]") {
    ffe::ArenaAllocator arena(1024);

    REQUIRE(arena.capacity() == 1024);
    REQUIRE(arena.used() == 0);
    REQUIRE(arena.remaining() == 1024);

    void* ptr = arena.allocate(64);
    REQUIRE(ptr != nullptr);
    REQUIRE(arena.used() >= 64);
}

TEST_CASE("ArenaAllocator alignment", "[arena]") {
    ffe::ArenaAllocator arena(4096);

    // Allocate 1 byte to offset the pointer
    void* first = arena.allocate(1, 1);
    REQUIRE(first != nullptr);

    // Next allocation with 16-byte alignment
    void* aligned = arena.allocate(32, 16);
    REQUIRE(aligned != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(aligned) % 16 == 0);

    // 64-byte alignment
    void* aligned64 = arena.allocate(64, 64);
    REQUIRE(aligned64 != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(aligned64) % 64 == 0);
}

TEST_CASE("ArenaAllocator reset", "[arena]") {
    ffe::ArenaAllocator arena(1024);

    arena.allocate(512);
    REQUIRE(arena.used() >= 512);

    arena.reset();
    REQUIRE(arena.used() == 0);
    REQUIRE(arena.remaining() == 1024);

    // Can allocate again after reset
    void* ptr = arena.allocate(256);
    REQUIRE(ptr != nullptr);
}

TEST_CASE("ArenaAllocator capacity exhaustion", "[arena]") {
    ffe::ArenaAllocator arena(128);

    // Fill the arena
    void* ptr = arena.allocate(128);
    REQUIRE(ptr != nullptr);

    // Next allocation should fail
    void* overflow = arena.allocate(1);
    REQUIRE(overflow == nullptr);
}

TEST_CASE("ArenaAllocator typed create", "[arena]") {
    ffe::ArenaAllocator arena(4096);

    struct TestStruct {
        int x;
        float y;
    };

    auto* obj = arena.create<TestStruct>(42, 3.14f);
    REQUIRE(obj != nullptr);
    REQUIRE(obj->x == 42);
    REQUIRE(obj->y == 3.14f);
}

TEST_CASE("ArenaAllocator array allocation", "[arena]") {
    ffe::ArenaAllocator arena(4096);

    auto* arr = arena.allocateArray<int>(10);
    REQUIRE(arr != nullptr);

    // Default-constructed ints should be zero
    for (int i = 0; i < 10; ++i) {
        REQUIRE(arr[i] == 0);
    }
}

TEST_CASE("ArenaAllocator allocate exactly capacity bytes succeeds", "[arena]") {
    ffe::ArenaAllocator arena(256);

    // Request exactly capacity bytes with alignment 1 to avoid padding
    void* ptr = arena.allocate(256, 1);
    REQUIRE(ptr != nullptr);
    REQUIRE(arena.remaining() == 0);
}

TEST_CASE("ArenaAllocator allocate capacity+1 bytes returns nullptr", "[arena]") {
    ffe::ArenaAllocator arena(256);

    void* ptr = arena.allocate(257, 1);
    REQUIRE(ptr == nullptr);
    REQUIRE(arena.used() == 0); // Arena state unchanged on failure
}

TEST_CASE("ArenaAllocator zero-byte allocation", "[arena]") {
    ffe::ArenaAllocator arena(256);

    // Zero-byte allocation should succeed (returns a valid pointer)
    void* ptr = arena.allocate(0);
    REQUIRE(ptr != nullptr);
}

TEST_CASE("ArenaAllocator sequential alignment after 1-byte alloc", "[arena]") {
    ffe::ArenaAllocator arena(4096);

    // Allocate 1 byte with minimal alignment to intentionally misalign
    void* first = arena.allocate(1, 1);
    REQUIRE(first != nullptr);

    // Request 64-byte aligned allocation
    void* aligned = arena.allocate(64, 64);
    REQUIRE(aligned != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(aligned) % 64 == 0);
}

TEST_CASE("ArenaAllocator reset then re-allocate", "[arena]") {
    ffe::ArenaAllocator arena(512);

    void* first = arena.allocate(256, 1);
    REQUIRE(first != nullptr);
    REQUIRE(arena.used() >= 256);

    arena.reset();
    REQUIRE(arena.used() == 0);
    REQUIRE(arena.remaining() == 512);

    // Re-allocate the full capacity — memory is reusable
    void* second = arena.allocate(512, 1);
    REQUIRE(second != nullptr);
    REQUIRE(arena.remaining() == 0);
}

TEST_CASE("ArenaAllocator create<T> when exhausted returns nullptr", "[arena]") {
    // Arena too small to hold the struct after alignment
    ffe::ArenaAllocator arena(4);

    struct Big {
        double a;
        double b;
        double c;
    };
    static_assert(sizeof(Big) > 4, "Big must exceed arena capacity for this test");

    auto* ptr = arena.create<Big>(1.0, 2.0, 3.0);
    REQUIRE(ptr == nullptr);
}

TEST_CASE("ArenaAllocator allocateArray<T>(0) behavior", "[arena]") {
    ffe::ArenaAllocator arena(256);

    // Zero-count array allocation — should succeed (degenerate case)
    auto* arr = arena.allocateArray<int>(0);
    // allocate(0) returns non-null, so this should too
    REQUIRE(arr != nullptr);
}
