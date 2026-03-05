#pragma once

#include <cstdint>
#include <cstddef>

namespace ffe {

// --- Fixed-width integer aliases ---
// These exist for brevity and consistency, not abstraction.
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using f32 = float;
using f64 = double;

// --- Entity and Component IDs ---
// EntityId is a 32-bit opaque handle.
// The upper 12 bits are a version/generation counter (for detecting stale references).
// The lower 20 bits are the entity index.
// This matches EnTT's default entity type (entt::entity is a 32-bit enum).
using EntityId = u32;

// ComponentId is used for runtime type identification in Lua bindings.
// It is an index into a type registry, NOT typeid() — we have no RTTI.
using ComponentId = u32;

// Null entity sentinel
inline constexpr EntityId NULL_ENTITY = ~EntityId{0};

// --- Hardware Tier ---
enum class HardwareTier : u8 {
    RETRO    = 0,   // OpenGL 2.1, 512 MB VRAM, single-threaded
    LEGACY   = 1,   // OpenGL 3.3, 1 GB VRAM, single-threaded
    STANDARD = 2,   // OpenGL 4.5 / Vulkan, 2 GB VRAM, multi-threaded
    MODERN   = 3    // Vulkan, 4+ GB VRAM, multi-threaded + RT
};

// Compile-time tier constant — matches the CMake FFE_TIER setting
inline constexpr HardwareTier CURRENT_TIER = static_cast<HardwareTier>(FFE_TIER_VALUE);

// Tier comparison helpers
inline constexpr bool tierAtLeast(const HardwareTier minimum) {
    return static_cast<u8>(CURRENT_TIER) >= static_cast<u8>(minimum);
}

inline constexpr bool tierAtMost(const HardwareTier maximum) {
    return static_cast<u8>(CURRENT_TIER) <= static_cast<u8>(maximum);
}

// --- Result type (no exceptions) ---
// Lightweight error reporting. Fits in two registers on x86-64.
class Result {
public:
    // Success
    static Result ok() { return Result{true, ""}; }

    // Failure with message
    static Result fail(const char* msg) { return Result{false, msg}; }

    bool isOk() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const char* message() const { return m_message; }

private:
    Result(const bool ok, const char* msg) : m_ok(ok), m_message(msg) {}

    bool m_ok;
    const char* m_message; // Points to a string literal or static buffer — never heap-allocated
};

// --- Common constants ---
inline constexpr f32 DEFAULT_TICK_RATE    = 60.0f;
inline constexpr i32 DEFAULT_WINDOW_WIDTH  = 1280;
inline constexpr i32 DEFAULT_WINDOW_HEIGHT = 720;

// Arena allocator default sizes (bytes)
inline constexpr size_t ARENA_SIZE_RETRO    = 512 * 1024;       //  512 KB
inline constexpr size_t ARENA_SIZE_LEGACY   = 2 * 1024 * 1024;  //    2 MB
inline constexpr size_t ARENA_SIZE_STANDARD = 8 * 1024 * 1024;  //    8 MB
inline constexpr size_t ARENA_SIZE_MODERN   = 16 * 1024 * 1024; //   16 MB

inline constexpr size_t arenaDefaultSize() {
    if constexpr (CURRENT_TIER == HardwareTier::RETRO)    return ARENA_SIZE_RETRO;
    if constexpr (CURRENT_TIER == HardwareTier::LEGACY)   return ARENA_SIZE_LEGACY;
    if constexpr (CURRENT_TIER == HardwareTier::STANDARD) return ARENA_SIZE_STANDARD;
    if constexpr (CURRENT_TIER == HardwareTier::MODERN)   return ARENA_SIZE_MODERN;
}

} // namespace ffe
