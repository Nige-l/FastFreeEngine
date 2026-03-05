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
