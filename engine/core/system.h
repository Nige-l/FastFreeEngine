#pragma once

#include "core/types.h"

namespace ffe {

// Forward declaration — ecs.h includes this header
class World;

// A system is a function that operates on the world.
// No base class. No virtual functions. Just a function pointer.
using SystemUpdateFn = void(*)(World& world, float dt);

// System registration with explicit execution order via priority.
// Lower priority runs first. Systems at the same priority run in registration order.
struct SystemDescriptor {
    const char* name;           // For logging and Tracy zones
    size_t nameLength;          // Cached strlen(name) — avoids per-tick strlen
    SystemUpdateFn updateFn;
    int32_t priority;           // Lower = runs earlier. Use multiples of 100.
};

// Priority conventions:
// 0-99:     Input polling
// 100-199:  Gameplay / scripting
// 200-299:  Physics
// 300-399:  Animation
// 400-499:  Audio
// 500+:     Render preparation (not rendering itself — that's in render())

} // namespace ffe

// ---------------------------------------------------------------------------
// FFE_SYSTEM(name_str, fn, prio) — convenience macro for system registration.
//
// Avoids manual nameLength computation. sizeof(literal) - 1 gives the correct
// length for string literals at compile time.
//
// Usage:
//   world.registerSystem(FFE_SYSTEM("MySystem", mySystemFn, 100));
//
// Do NOT pass a runtime string pointer as name_str — sizeof() will return the
// pointer size, not the string length. Only string literals are safe here.
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FFE_SYSTEM(name_str, fn, prio) \
    []() { \
        ::ffe::SystemDescriptor _desc{}; \
        _desc.name       = (name_str); \
        _desc.nameLength = sizeof(name_str) - 1; \
        _desc.updateFn   = (fn); \
        _desc.priority   = (prio); \
        return _desc; \
    }()
