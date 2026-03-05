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
