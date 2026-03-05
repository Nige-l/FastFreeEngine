# ADR-001: Core Engine Skeleton

**Status:** APPROVED
**Author:** architect
**Date:** 2026-03-05
**Tiers:** ALL (RETRO, LEGACY, STANDARD, MODERN)

This is the foundational architecture document for FastFreeEngine. Every subsequent system builds on the decisions made here. Engine-dev should be able to implement the entire skeleton from this document alone without asking clarifying questions.

---

## 1. Directory Layout

```
FastFreeEngine/
├── CMakeLists.txt                  # Root build file
├── vcpkg.json                      # Dependency manifest
├── cmake/
│   ├── CompilerFlags.cmake         # Shared compiler/linker flags
│   └── TierDefinitions.cmake       # Hardware tier preprocessor defines
├── engine/
│   ├── CMakeLists.txt              # Collects all engine submodules into ffe::engine
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── .context.md
│   │   ├── application.h           # Application class, main loop
│   │   ├── application.cpp
│   │   ├── arena_allocator.h       # Per-frame scratch allocator
│   │   ├── arena_allocator.cpp
│   │   ├── logging.h               # Logging macros and sink
│   │   ├── logging.cpp
│   │   ├── types.h                 # Core typedefs, Result, EntityId, tier enum
│   │   ├── ecs.h                   # Thin EnTT wrapper — header only
│   │   └── system.h                # System base concept / registration
│   ├── renderer/
│   │   ├── CMakeLists.txt
│   │   ├── .context.md
│   │   └── (renderer-specialist owns — placeholder for now)
│   ├── audio/
│   │   ├── CMakeLists.txt
│   │   └── .context.md
│   ├── physics/
│   │   ├── CMakeLists.txt
│   │   └── .context.md
│   ├── scripting/
│   │   ├── CMakeLists.txt
│   │   ├── .context.md
│   │   └── (engine-dev + api-designer own)
│   └── editor/
│       ├── CMakeLists.txt
│       └── .context.md
├── tests/
│   ├── CMakeLists.txt
│   ├── test_main.cpp               # Catch2 main entry point
│   ├── core/
│   │   ├── test_arena_allocator.cpp
│   │   ├── test_logging.cpp
│   │   ├── test_application.cpp
│   │   └── test_ecs.cpp
│   └── (mirrors engine/ subdirectories as they are built)
├── examples/
│   ├── hello_triangle/
│   │   └── main.lua
│   └── spinning_cube/
│       └── main.lua
├── assets/
│   └── (runtime assets: textures, models, sounds)
├── shaders/
│   ├── legacy/                     # OpenGL 3.3 GLSL (version 330 core)
│   └── standard/                   # OpenGL 4.5 / SPIR-V
├── docs/
│   ├── architecture/               # architect owns
│   ├── agents/                     # director owns
│   ├── devlog.md                   # project-manager owns
│   └── environment.md              # system-engineer owns
└── games/                          # Example full games (game-dev-tester)
```

### Rules

- Every engine subdirectory gets its own `CMakeLists.txt` and `.context.md`.
- No source files live directly in `engine/` — everything lives in a subdirectory.
- `tests/` mirrors the `engine/` directory structure. Tests for `engine/core/arena_allocator.cpp` live in `tests/core/test_arena_allocator.cpp`.
- The `cmake/` directory is new. It holds reusable CMake modules. It is owned by engine-dev.
- The `examples/` directory holds small Lua games. Owned by game-dev-tester.
- The `shaders/` directory is owned by renderer-specialist.

---

## 2. CMake Build System Design

### 2.1 Root CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.25)

# vcpkg toolchain — must be set before project()
# Users set VCPKG_ROOT env var or pass -DCMAKE_TOOLCHAIN_FILE
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

project(FastFreeEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Hardware tier selection ---
set(FFE_TIER "LEGACY" CACHE STRING "Hardware tier: RETRO, LEGACY, STANDARD, MODERN")
set_property(CACHE FFE_TIER PROPERTY STRINGS RETRO LEGACY STANDARD MODERN)

include(cmake/CompilerFlags.cmake)
include(cmake/TierDefinitions.cmake)

# --- Find dependencies ---
find_package(EnTT CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
# imgui, stb, joltphysics, vulkan-memory-allocator found when needed

# --- Engine library ---
add_subdirectory(engine)

# --- Tests ---
option(FFE_BUILD_TESTS "Build unit tests" ON)
if(FFE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### 2.2 cmake/CompilerFlags.cmake

```cmake
# Compiler warning flags — non-negotiable
add_compile_options(-Wall -Wextra -Wpedantic)

# No RTTI, no exceptions in engine core
add_compile_options(-fno-rtti -fno-exceptions)

# Linker: use mold if available
find_program(MOLD_LINKER mold)
if(MOLD_LINKER)
    add_link_options(-fuse-ld=mold)
    message(STATUS "Using mold linker: ${MOLD_LINKER}")
else()
    message(WARNING "mold linker not found — falling back to default linker")
endif()

# ccache: detected automatically by CMake if CMAKE_<LANG>_COMPILER_LAUNCHER is set,
# but we set it explicitly for clarity.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher")
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
endif()

# Per-config flags
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DFFE_DEBUG=1")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -DFFE_RELEASE=1")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -DFFE_RELWITHDEBINFO=1")

# Tracy is enabled only in Debug and RelWithDebInfo
# In Release, TRACY_ENABLE is not defined, so all Tracy macros become no-ops
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(TRACY_ENABLE)
endif()
```

**Why `-O2` and not `-O3` for Release:** `-O3` enables aggressive vectorization and loop unrolling that can blow out the instruction cache on LEGACY-tier CPUs with small L1i caches. `-O2` is the right default. If profiling shows a specific translation unit benefits from `-O3`, it can be set per-file.

**Why `-fno-exceptions`:** Exception handling has non-zero cost even when exceptions are not thrown (unwind tables increase binary size, and on some architectures, exception-aware code is slightly less optimizable). Since we are targeting constrained hardware and have committed to error-code-based error handling, we disable exceptions globally.

### 2.3 cmake/TierDefinitions.cmake

```cmake
# Define FFE_TIER_<name> preprocessor macro based on the selected tier
# Exactly one of these will be defined at compile time.

if(FFE_TIER STREQUAL "RETRO")
    add_compile_definitions(FFE_TIER_RETRO=1 FFE_TIER_VALUE=0)
elseif(FFE_TIER STREQUAL "LEGACY")
    add_compile_definitions(FFE_TIER_LEGACY=1 FFE_TIER_VALUE=1)
elseif(FFE_TIER STREQUAL "STANDARD")
    add_compile_definitions(FFE_TIER_STANDARD=1 FFE_TIER_VALUE=2)
elseif(FFE_TIER STREQUAL "MODERN")
    add_compile_definitions(FFE_TIER_MODERN=1 FFE_TIER_VALUE=3)
else()
    message(FATAL_ERROR "Invalid FFE_TIER: ${FFE_TIER}. Must be RETRO, LEGACY, STANDARD, or MODERN.")
endif()

message(STATUS "FFE Hardware Tier: ${FFE_TIER}")
```

The `FFE_TIER_VALUE` integer allows compile-time `if constexpr` comparisons:

```cpp
if constexpr (FFE_TIER_VALUE >= 2) {
    // STANDARD or MODERN only
}
```

### 2.4 engine/CMakeLists.txt

```cmake
add_subdirectory(core)
add_subdirectory(renderer)
add_subdirectory(audio)
add_subdirectory(physics)
add_subdirectory(scripting)
add_subdirectory(editor)

# Umbrella target that links all engine modules
add_library(ffe_engine INTERFACE)
target_link_libraries(ffe_engine INTERFACE
    ffe_core
    # ffe_renderer   — added when renderer is implemented
    # ffe_audio      — added when audio is implemented
    # ffe_physics    — added when physics is implemented
    # ffe_scripting  — added when scripting is implemented
    # ffe_editor     — added when editor is implemented
)
```

### 2.5 engine/core/CMakeLists.txt

```cmake
add_library(ffe_core STATIC
    application.cpp
    arena_allocator.cpp
    logging.cpp
)

target_include_directories(ffe_core
    PUBLIC ${CMAKE_SOURCE_DIR}/engine  # so includes are: #include "core/types.h"
)

target_link_libraries(ffe_core
    PUBLIC
        EnTT::EnTT
        glm::glm
        Tracy::TracyClient
)
```

**Include path convention:** All engine includes use paths relative to `engine/`. This means:

```cpp
#include "core/types.h"
#include "core/logging.h"
#include "renderer/rhi.h"
```

This prevents ambiguity and makes dependencies between modules visible at the include site.

### 2.6 Placeholder Module CMakeLists.txt

For modules not yet implemented (audio, physics, scripting, editor), use:

```cmake
# engine/audio/CMakeLists.txt (placeholder — no sources yet)
add_library(ffe_audio INTERFACE)
```

Replace `INTERFACE` with `STATIC` and add sources when the module is implemented.

### 2.7 tests/CMakeLists.txt

```cmake
add_executable(ffe_tests
    test_main.cpp
    core/test_arena_allocator.cpp
    core/test_logging.cpp
    core/test_application.cpp
    core/test_ecs.cpp
)

target_link_libraries(ffe_tests
    PRIVATE
        ffe_core
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(ffe_tests)
```

**Note:** `test_main.cpp` can be empty if using `Catch2::Catch2WithMain` (which provides `main()`). If we need a custom main (e.g., to initialize Tracy), define it in `test_main.cpp` and link `Catch2::Catch2` instead.

### 2.8 Building

```bash
# Configure (clang-18, Debug, LEGACY tier — the default)
cmake -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFFE_TIER=LEGACY

# Build
cmake --build build

# Test
cd build && ctest --output-on-failure

# GCC-13 build (must also pass clean)
cmake -B build-gcc -G Ninja \
    -DCMAKE_CXX_COMPILER=g++-13 \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build-gcc
```

---

## 3. Core Engine Loop

### 3.1 Application Class

The `Application` class owns the main loop and the lifetime of all engine systems. There is exactly one `Application` instance per process. It is not a singleton — it is created in `main()` and passed by reference where needed.

```cpp
// engine/core/application.h
#pragma once

#include "core/types.h"
#include "core/arena_allocator.h"
#include "core/logging.h"
#include "core/ecs.h"

#include <tracy/Tracy.hpp>

namespace ffe {

struct ApplicationConfig {
    const char* windowTitle = "FastFreeEngine";
    int32_t windowWidth     = 1280;
    int32_t windowHeight    = 720;
    float tickRate          = 60.0f;     // Fixed update Hz
    HardwareTier tier       = HardwareTier::LEGACY;
    bool headless           = false;     // true for tests / CI
};

class Application {
public:
    explicit Application(const ApplicationConfig& config);
    ~Application();

    // Non-copyable, non-movable — there is exactly one
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Run the main loop. Returns exit code.
    int32_t run();

    // Request shutdown at end of current frame
    void requestShutdown();

    // Access subsystems
    World& world();
    ArenaAllocator& frameAllocator();
    const ApplicationConfig& config() const;

private:
    Result startup();
    void shutdown();
    void tick(float dt);      // Fixed-rate update
    void render(float alpha); // Variable-rate render with interpolation factor

    ApplicationConfig m_config;
    World m_world;
    ArenaAllocator m_frameAllocator;
    bool m_running = false;
};

} // namespace ffe
```

### 3.2 Main Loop — Fixed Timestep with Interpolation

This is the most important code in the engine. Get this wrong and everything built on top suffers.

```cpp
// Inside Application::run()

int32_t Application::run() {
    const Result startResult = startup();
    if (!startResult.ok()) {
        FFE_LOG_FATAL("Core", "Startup failed: {}", startResult.message());
        return 1;
    }

    m_running = true;

    const float fixedDt = 1.0f / m_config.tickRate;   // e.g., 1/60 = 0.01667s
    const float maxFrameTime = 0.25f;                  // Spiral-of-death clamp

    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<float>;

    auto previousTime = Clock::now();
    float accumulator = 0.0f;

    while (m_running) {
        ZoneScoped; // Tracy: marks entire frame

        const auto currentTime = Clock::now();
        float frameTime = Duration(currentTime - previousTime).count();
        previousTime = currentTime;

        // Clamp to prevent spiral of death (e.g., debugger breakpoint)
        if (frameTime > maxFrameTime) {
            frameTime = maxFrameTime;
        }

        accumulator += frameTime;

        // --- Fixed-rate update ---
        while (accumulator >= fixedDt) {
            ZoneScopedN("FixedTick");
            tick(fixedDt);
            accumulator -= fixedDt;
        }

        // --- Variable-rate render ---
        {
            const float alpha = accumulator / fixedDt; // Interpolation factor [0, 1)
            ZoneScopedN("Render");
            render(alpha);
        }

        // --- Per-frame cleanup ---
        m_frameAllocator.reset();

        FrameMark; // Tracy: end of frame
    }

    shutdown();
    return 0;
}
```

**Why this loop design:**

1. **Fixed timestep** ensures deterministic physics and gameplay regardless of frame rate. A LEGACY machine running at 30 fps and a MODERN machine running at 144 fps compute the same game state.
2. **Interpolation factor `alpha`** allows the renderer to interpolate between the previous and current state for smooth visuals even when the render rate differs from the tick rate.
3. **Spiral-of-death clamp** at 0.25s prevents the case where a slow frame causes the accumulator to grow unboundedly, triggering more ticks, which causes more slow frames.
4. **`steady_clock`** is monotonic. Never use `system_clock` for frame timing — it can jump when NTP adjusts or the user changes the system time.

### 3.3 System Registration and Execution

Systems are plain functions (or function-pointer-like callables). No virtual dispatch. No inheritance hierarchy.

```cpp
// engine/core/system.h
#pragma once

#include "core/types.h"
#include "core/ecs.h"

namespace ffe {

// A system is a function that operates on the world.
// No base class. No virtual functions. Just a function pointer.
using SystemUpdateFn = void(*)(World& world, float dt);

// System registration with explicit execution order via priority.
// Lower priority runs first. Systems at the same priority run in registration order.
struct SystemDescriptor {
    const char* name;           // For logging and Tracy zones
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
```

The `World` class (see Section 4) owns a `std::vector<SystemDescriptor>` that is populated during startup and sorted once by priority. During `tick()`, each system is invoked in order:

```cpp
void Application::tick(const float dt) {
    for (const auto& system : m_world.systems()) {
        ZoneScopedN(system.name);
        system.updateFn(m_world, dt);
    }
}
```

**Why function pointers, not `std::function`:** `std::function` allocates on the heap for non-trivial callables and has a virtual dispatch internally. A raw function pointer is one indirection, fits in a register, and the call site is trivially predictable by the branch predictor after the first invocation.

**Why a flat sorted array, not a dependency graph:** Dependency graphs are over-engineered for this use case. Priority numbers are explicit, simple to debug, and the sorted array gives us O(1) amortized iteration with perfect cache behavior. If we ever need parallel system execution (STANDARD/MODERN tiers), we can partition the array into dependency levels without changing the interface.

### 3.4 Startup and Shutdown

```cpp
Result Application::startup() {
    // Order matters. Each step may depend on the previous.
    // 1. Initialize logging (must be first — everything else logs)
    // 2. Log the hardware tier and build config
    // 3. Initialize the frame allocator
    // 4. Create the window (unless headless)
    // 5. Initialize the renderer (unless headless)
    // 6. Initialize the scripting engine
    // 7. Register built-in systems
    // 8. Sort system list by priority
    // 9. Call user init callback (if any)
    return Result::ok();
}

void Application::shutdown() {
    // Reverse order of startup
    // 9. Call user shutdown callback
    // 8. (nothing)
    // 7. Unregister systems (clear the vector)
    // 6. Shutdown scripting
    // 5. Shutdown renderer
    // 4. Destroy window
    // 3. (arena allocator destructor handles it)
    // 2. (nothing)
    // 1. Flush and close log file
}
```

---

## 4. ECS Design (EnTT Wrapper)

### 4.1 Why We Wrap EnTT

We wrap EnTT for exactly three reasons:

1. **Stable API surface for Lua bindings.** EnTT's API is template-heavy and changes between major versions. Our Lua bindings (via sol2) need a stable, non-template interface. The wrapper provides this.
2. **Tier-aware optimizations.** On RETRO/LEGACY tiers, we may want to limit entity counts or disable certain EnTT features (groups, sorting) that assume cache sizes we do not have.
3. **Controlled exposure.** EnTT has a large API. We expose only what FFE needs. This makes the engine learnable — a student reading `ecs.h` sees 15 functions, not 150.

We do NOT wrap EnTT to "be able to swap it out later." EnTT is the right choice and we commit to it. The wrapper exists for the reasons above and no others.

### 4.2 The World Class

```cpp
// engine/core/ecs.h
#pragma once

#include "core/types.h"
#include "core/system.h"

#include <entt/entt.hpp>
#include <vector>
#include <algorithm>

namespace ffe {

class World {
public:
    // --- Entity lifecycle ---
    EntityId createEntity();
    void destroyEntity(EntityId id);
    bool isValid(EntityId id) const;

    // --- Component access (templates — resolved at compile time, zero overhead) ---
    template<typename T, typename... Args>
    T& addComponent(EntityId id, Args&&... args);

    template<typename T>
    void removeComponent(EntityId id);

    template<typename T>
    T& getComponent(EntityId id);

    template<typename T>
    const T& getComponent(EntityId id) const;

    template<typename T>
    bool hasComponent(EntityId id) const;

    // --- Views (iterate entities with specific components) ---
    // Returns an EnTT view. This is intentionally not wrapped further —
    // views are the hot path and any wrapper adds overhead.
    template<typename... Components>
    auto view();

    template<typename... Components>
    auto view() const;

    // --- System management ---
    void registerSystem(const SystemDescriptor& desc);
    void sortSystems(); // Called once after all systems registered
    const std::vector<SystemDescriptor>& systems() const;

    // --- Access to raw registry (escape hatch for renderer/advanced use) ---
    entt::registry& registry();
    const entt::registry& registry() const;

private:
    entt::registry m_registry;
    std::vector<SystemDescriptor> m_systems;
};

// --- Implementation (header-only for template functions) ---

inline EntityId World::createEntity() {
    return static_cast<EntityId>(m_registry.create());
}

inline void World::destroyEntity(const EntityId id) {
    m_registry.destroy(static_cast<entt::entity>(id));
}

inline bool World::isValid(const EntityId id) const {
    return m_registry.valid(static_cast<entt::entity>(id));
}

template<typename T, typename... Args>
T& World::addComponent(const EntityId id, Args&&... args) {
    return m_registry.emplace<T>(
        static_cast<entt::entity>(id),
        std::forward<Args>(args)...
    );
}

template<typename T>
void World::removeComponent(const EntityId id) {
    m_registry.remove<T>(static_cast<entt::entity>(id));
}

template<typename T>
T& World::getComponent(const EntityId id) {
    return m_registry.get<T>(static_cast<entt::entity>(id));
}

template<typename T>
const T& World::getComponent(const EntityId id) const {
    return m_registry.get<T>(static_cast<entt::entity>(id));
}

template<typename T>
bool World::hasComponent(const EntityId id) const {
    return m_registry.all_of<T>(static_cast<entt::entity>(id));
}

template<typename... Components>
auto World::view() {
    return m_registry.view<Components...>();
}

template<typename... Components>
auto World::view() const {
    return m_registry.view<Components...>();
}

inline void World::registerSystem(const SystemDescriptor& desc) {
    m_systems.push_back(desc);
}

inline void World::sortSystems() {
    std::sort(m_systems.begin(), m_systems.end(),
        [](const SystemDescriptor& a, const SystemDescriptor& b) {
            return a.priority < b.priority;
        });
}

inline const std::vector<SystemDescriptor>& World::systems() const {
    return m_systems;
}

inline entt::registry& World::registry() {
    return m_registry;
}

inline const entt::registry& World::registry() const {
    return m_registry;
}

} // namespace ffe
```

### 4.3 How Components Are Defined

Components are plain structs. No base class. No virtual functions. No inheritance. Components should be small — ideally 64 bytes or less (one cache line) for components accessed in tight loops.

```cpp
// Example components — these would live in their respective module headers

namespace ffe {

struct Transform {
    glm::vec3 position{0.0f};   // 12 bytes
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // 16 bytes
    glm::vec3 scale{1.0f};     // 12 bytes
    // Total: 40 bytes — fits in one cache line with room to spare
};

struct Velocity {
    glm::vec3 linear{0.0f};    // 12 bytes
    glm::vec3 angular{0.0f};   // 12 bytes
    // Total: 24 bytes
};

struct Sprite {
    uint32_t textureId = 0;    // 4 bytes
    glm::vec2 size{1.0f};      // 8 bytes
    glm::vec4 color{1.0f};     // 16 bytes
    int32_t layer = 0;         // 4 bytes
    // Total: 32 bytes — half a cache line
};

} // namespace ffe
```

**Memory layout reasoning:** EnTT uses a sparse set internally. Each component type gets its own dense packed array. When you iterate a `view<Transform, Velocity>()`, EnTT iterates the smaller set and probes the larger. The dense arrays are contiguous in memory, giving excellent cache utilization for the common case of iterating all entities with a given component set.

This is why components should be small. If `Transform` were 256 bytes, only one would fit per cache line, and a system iterating 10,000 transforms would touch 10,000 cache lines. At 40 bytes, we fit ~1.5 per line, and the prefetcher has a chance of staying ahead.

**Rule:** If a component exceeds 64 bytes, split it. Put frequently-accessed data in the main component and rarely-accessed data in a separate component (e.g., `TransformExtra`).

### 4.4 Entity Limits by Tier

| Tier | Max Recommended Entities | Rationale |
|------|--------------------------|-----------|
| RETRO | 5,000 | Limited CPU cache, single-threaded iteration |
| LEGACY | 20,000 | Larger caches but still single-threaded |
| STANDARD | 100,000 | Multi-threaded iteration possible |
| MODERN | 500,000 | Large caches, wide SIMD, multi-threaded |

These are not hard limits enforced by code — they are budgets. If a game on LEGACY has 20,001 entities and runs at 60 fps, that is fine. If it has 20,001 and runs at 45 fps, the entity count is the first thing to investigate.

---

## 5. Arena Allocator

### 5.1 Design

The arena allocator is a linear bump allocator. It hands out memory by advancing a pointer. Individual allocations cannot be freed. The entire arena is reset at the end of each frame.

This is the correct allocator for per-frame transient data: temporary arrays, string formatting buffers, intermediate computation results, draw call lists. It has zero fragmentation, zero per-allocation overhead beyond a pointer bump, and perfect cache locality for sequential allocations.

```cpp
// engine/core/arena_allocator.h
#pragma once

#include "core/types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

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
```

### 5.2 arena_allocator.cpp

```cpp
// engine/core/arena_allocator.cpp
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
        FFE_LOG_FATAL("Arena", "Failed to allocate {} bytes for arena", capacityBytes);
        // In a no-exceptions world, we set capacity to 0 and every allocate() returns nullptr.
        m_capacity = 0;
    }
}

ArenaAllocator::~ArenaAllocator() {
    std::free(m_buffer);
}

} // namespace ffe
```

### 5.3 Size Budgets by Tier

| Tier | Frame Arena Size | Rationale |
|------|-----------------|-----------|
| RETRO | 512 KB | Minimal transient data, small draw lists |
| LEGACY | 2 MB | Moderate draw lists, Lua interop buffers |
| STANDARD | 8 MB | Larger scenes, particle buffers, multi-threaded scratch |
| MODERN | 16 MB | Large scenes, ray tracing data, async compute scratch |

### 5.4 Threading Model

- **RETRO / LEGACY:** A single global `ArenaAllocator` owned by `Application`. All engine code runs single-threaded. Simple.
- **STANDARD / MODERN:** Each worker thread gets its own `thread_local ArenaAllocator`. The main thread's arena is still the one owned by `Application`. Worker thread arenas are created when the thread pool starts and destroyed when it stops. All arenas are reset at the frame barrier.

For the initial skeleton, we implement only the single-threaded path. The `thread_local` path is designed here so engine-dev does not paint us into a corner.

```cpp
// Future addition for STANDARD/MODERN tiers:
// thread_local ArenaAllocator* t_threadArena = nullptr;
//
// ArenaAllocator& threadFrameAllocator() {
//     return *t_threadArena; // Initialized by thread pool at thread start
// }
```

---

## 6. Logging System

### 6.1 Requirements

- Zero heap allocations in the hot path.
- Compile-time severity filtering so TRACE/DEBUG calls vanish entirely in Release builds.
- Thread-safe writes (single mutex for the output sink — logging is not the hot path).
- Format: `[HH:MM:SS.mmm] [LEVEL] [SYSTEM] message`

### 6.2 Interface

```cpp
// engine/core/logging.h
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdarg>

namespace ffe {

enum class LogLevel : uint8_t {
    TRACE  = 0,
    DEBUG  = 1,
    INFO   = 2,
    WARN   = 3,
    ERR    = 4,   // Not ERROR — that's a Windows macro
    FATAL  = 5
};

// Set the minimum severity that will be emitted at runtime.
// Messages below this level are discarded (but still compiled-out via macros in Release).
void setLogLevel(LogLevel level);

// Set the output file. Pass nullptr to log to stdout (the default).
// The engine does NOT own this file pointer — caller is responsible for fclose().
void setLogFile(FILE* file);

// Core log function. Do not call directly — use the macros below.
void logMessage(LogLevel level, const char* system, const char* fmt, ...);

// Initialize/shutdown logging subsystem
void initLogging();
void shutdownLogging();

} // namespace ffe

// --- Compile-time filtering macros ---
// In Release builds, TRACE and DEBUG are compiled out entirely.
// The do-while(0) idiom ensures the macro is a single statement.

#if defined(FFE_DEBUG) || defined(FFE_RELWITHDEBINFO)
    #define FFE_LOG_TRACE(system, fmt, ...) \
        do { ffe::logMessage(ffe::LogLevel::TRACE, system, fmt, ##__VA_ARGS__); } while(0)
    #define FFE_LOG_DEBUG(system, fmt, ...) \
        do { ffe::logMessage(ffe::LogLevel::DEBUG, system, fmt, ##__VA_ARGS__); } while(0)
#else
    #define FFE_LOG_TRACE(system, fmt, ...) do {} while(0)
    #define FFE_LOG_DEBUG(system, fmt, ...) do {} while(0)
#endif

#define FFE_LOG_INFO(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::INFO, system, fmt, ##__VA_ARGS__); } while(0)
#define FFE_LOG_WARN(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::WARN, system, fmt, ##__VA_ARGS__); } while(0)
#define FFE_LOG_ERROR(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::ERR, system, fmt, ##__VA_ARGS__); } while(0)
#define FFE_LOG_FATAL(system, fmt, ...) \
    do { ffe::logMessage(ffe::LogLevel::FATAL, system, fmt, ##__VA_ARGS__); } while(0)
```

### 6.3 Implementation Notes

```cpp
// engine/core/logging.cpp

// Internal state — file-scope, not exported
static ffe::LogLevel s_minLevel = ffe::LogLevel::TRACE;
static FILE* s_output = stdout;
static std::mutex s_logMutex; // Only lock for writes, never in hot path

void ffe::logMessage(LogLevel level, const char* system, const char* fmt, ...) {
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(s_minLevel)) {
        return;
    }

    // Get timestamp — use steady_clock offset from process start, not wall clock
    // This avoids the cost of a syscall to get wall time every log call.
    // For file logging, we prepend the start time once at the top of the log file.

    static const auto s_startTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_startTime);

    const int64_t totalMs = elapsed.count();
    const int32_t hours   = static_cast<int32_t>(totalMs / 3600000);
    const int32_t minutes = static_cast<int32_t>((totalMs % 3600000) / 60000);
    const int32_t seconds = static_cast<int32_t>((totalMs % 60000) / 1000);
    const int32_t millis  = static_cast<int32_t>(totalMs % 1000);

    // Format the user message into a stack buffer — no heap allocation
    char messageBuffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(messageBuffer, sizeof(messageBuffer), fmt, args);
    va_end(args);

    static constexpr const char* LEVEL_NAMES[] = {
        "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
    };

    // Lock and write. This is the only mutex in the logging system.
    std::lock_guard<std::mutex> lock(s_logMutex);
    fprintf(s_output, "[%02d:%02d:%02d.%03d] [%s] [%s] %s\n",
        hours, minutes, seconds, millis,
        LEVEL_NAMES[static_cast<uint8_t>(level)],
        system,
        messageBuffer
    );

    // Flush FATAL immediately
    if (level == LogLevel::FATAL) {
        fflush(s_output);
    }
}
```

**Why `vsnprintf` and not `std::format`:** `std::format` in libstdc++ and libc++ (as of the compilers we target) can allocate on the heap for large format strings. `vsnprintf` into a stack buffer is guaranteed zero-allocation. When/if `std::format_to` with a fixed buffer is reliable across both compilers, we can switch.

**Why a 1024-byte stack buffer:** This is 1/64th of a typical L1 cache. It is large enough for any reasonable log message. Messages that exceed it are truncated — this is intentional. If you need to log more than 1 KB, you are logging wrong.

**Why `fprintf` and not `write()`:** `fprintf` with a mutex is sufficient for our needs. We are not logging at a rate where the stdio lock matters. The mutex is only held for the duration of the `fprintf` call (microseconds).

### 6.4 Log Output Rules

| Build Config | Default Log Level | Output Destination |
|-------------|-------------------|-------------------|
| Debug | TRACE | stdout |
| RelWithDebInfo | DEBUG | stdout |
| Release | INFO | file (`ffe.log` in working directory) |

---

## 7. Type Definitions

```cpp
// engine/core/types.h
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
inline constexpr bool tierAtLeast(HardwareTier minimum) {
    return static_cast<u8>(CURRENT_TIER) >= static_cast<u8>(minimum);
}

inline constexpr bool tierAtMost(HardwareTier maximum) {
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

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const char* message() const { return m_message; }

private:
    Result(bool ok, const char* msg) : m_ok(ok), m_message(msg) {}

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
```

**Why `Result` stores `const char*` and not `std::string`:** String allocates on the heap. Our error messages are always string literals (compile-time constants) or formatted into a static buffer. The `const char*` is a pointer to existing memory — zero allocation, fits in a register pair with `m_ok`.

**Why separate `ok()` and `operator bool()`:** The explicit `operator bool()` prevents accidental implicit conversions. `ok()` is the named method for clarity in conditionals. Having both means `if (result)` and `if (result.ok())` both work, and the developer picks whichever is more readable at the call site.

---

## 8. What This Prevents Us From Doing (And Why That Is OK)

### 8.1 No Plugin Architecture

We do not support dynamically loaded engine plugins (DLLs/SOs loaded at runtime). This means third parties cannot extend the engine without recompiling.

**Why that is OK:** Plugin systems require virtual interfaces (which we ban in hot paths), ABI stability across compiler versions (which is a maintenance nightmare), and add complexity that directly conflicts with our mission of being simple and learnable. If someone wants to extend FFE, they modify the source and rebuild. This is an open-source engine, not a commercial middleware SDK.

### 8.2 No Multithreaded ECS in Initial Skeleton

The system execution loop is single-threaded. Systems run one after another. This means STANDARD/MODERN tiers cannot fully utilize their multi-core CPUs yet.

**Why that is OK:** Single-threaded correctness first. The system priority model we defined is forward-compatible with parallel execution — systems at different priority levels are guaranteed to not run concurrently, and systems at the same priority level have no ordering guarantee (by convention). When we add a thread pool, we partition the sorted system array into priority bands and run each band in parallel. No API changes needed.

### 8.3 No Hot Reloading of C++ Code

Changing engine code requires a full rebuild. There is no live code patching.

**Why that is OK:** Lua is the hot-reload path for gameplay. Engine internals should not change at runtime — that is a source of bugs, not productivity. `ccache` and `mold` make incremental rebuilds fast enough (sub-second for single-file changes).

### 8.4 No Abstract Factory / Strategy Pattern for Systems

Systems are raw function pointers, not polymorphic objects. You cannot swap system implementations at runtime (e.g., switching between "arcade physics" and "realistic physics" at runtime).

**Why that is OK:** This is a compile-time decision, not a runtime one. If a game needs two physics modes, it implements one system function with an internal branch, or uses different component types. The function pointer call is one indirection; a virtual call through an abstract base class is two (vtable lookup + function call) and prevents inlining.

### 8.5 No Custom Allocator Integration with `std::` Containers

We do not provide custom `std::allocator` implementations that back `std::vector` or `std::string` with our arena. Standard containers use the default heap allocator.

**Why that is OK:** Integrating custom allocators with `std::` containers is a notorious source of complexity and subtle bugs (allocator propagation on move, rebinding, stateful allocators). Instead, we use raw arrays from the arena where we need per-frame sequential data, and use `std::vector` with `reserve()` for persistent data that outlives a frame. This is simpler, faster, and harder to misuse.

### 8.6 Fixed Timestep Is Not Configurable at Runtime

The tick rate is set at startup and cannot change during execution. A game cannot switch from 60 Hz to 30 Hz ticks mid-session.

**Why that is OK:** Changing the tick rate at runtime invalidates assumptions made by physics, animation, and networking code. If a game truly needs a different tick rate, it restarts the application with a new `ApplicationConfig`. In practice, 60 Hz is correct for nearly all games.

---

## Appendix A: Dependency Map

```
ffe_core
├── EnTT::EnTT       (ECS)
├── glm::glm          (math types used in components)
└── Tracy::TracyClient (profiling)

ffe_renderer (future)
├── ffe_core
├── OpenGL / Vulkan    (graphics API)
└── stb                (image loading)

ffe_scripting (future)
├── ffe_core
└── sol2 / LuaJIT      (Lua bindings)

ffe_physics (future)
├── ffe_core
└── joltphysics        (physics simulation)

ffe_editor (future)
├── ffe_core
├── ffe_renderer
└── imgui              (editor UI)

ffe_tests
├── ffe_core
└── Catch2             (test framework)
```

---

## Appendix B: File Implementation Checklist for engine-dev

This is the exact set of files to create for the initial skeleton. Each file is listed with its purpose and approximate line count.

| File | Purpose | ~Lines |
|------|---------|--------|
| `CMakeLists.txt` (root) | Project setup, vcpkg, deps, subdirs | 40 |
| `cmake/CompilerFlags.cmake` | Warning flags, linker, ccache | 25 |
| `cmake/TierDefinitions.cmake` | Tier preprocessor defines | 20 |
| `engine/CMakeLists.txt` | Umbrella target | 15 |
| `engine/core/CMakeLists.txt` | Core library target | 15 |
| `engine/core/types.h` | All type definitions from Section 7 | 80 |
| `engine/core/logging.h` | Logging macros from Section 6.2 | 50 |
| `engine/core/logging.cpp` | Logging implementation from Section 6.3 | 70 |
| `engine/core/arena_allocator.h` | Arena allocator from Section 5.2 | 70 |
| `engine/core/arena_allocator.cpp` | Arena constructor/destructor from Section 5.2 | 25 |
| `engine/core/system.h` | System descriptor and priority conventions | 30 |
| `engine/core/ecs.h` | World class wrapping EnTT from Section 4.2 | 90 |
| `engine/core/application.h` | Application class from Section 3.1 | 50 |
| `engine/core/application.cpp` | Main loop from Section 3.2, startup/shutdown | 100 |
| `engine/renderer/CMakeLists.txt` | Placeholder INTERFACE library | 3 |
| `engine/audio/CMakeLists.txt` | Placeholder INTERFACE library | 3 |
| `engine/physics/CMakeLists.txt` | Placeholder INTERFACE library | 3 |
| `engine/scripting/CMakeLists.txt` | Placeholder INTERFACE library | 3 |
| `engine/editor/CMakeLists.txt` | Placeholder INTERFACE library | 3 |
| `tests/CMakeLists.txt` | Test executable and discovery | 15 |
| `tests/test_main.cpp` | Empty or minimal (Catch2WithMain provides main) | 1 |
| `tests/core/test_arena_allocator.cpp` | Arena allocator tests | 60 |
| `tests/core/test_logging.cpp` | Logging tests | 40 |
| `tests/core/test_application.cpp` | Application lifecycle test (headless) | 40 |
| `tests/core/test_ecs.cpp` | World wrapper tests | 60 |

**Total:** ~911 lines of code. This is the entire initial skeleton.

After all files compile clean on both clang-18 and gcc-13 with zero warnings, the skeleton is done. Do not add features beyond what is described here — the next ADR will define the renderer interface.
