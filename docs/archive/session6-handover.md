# FastFreeEngine — Session 6 Handover Document

**Prepared by:** project-manager
**Date:** 2026-03-05
**Picking up from:** Session 5 complete — Lua scripting and texture loading delivered

This document is the authoritative starting point for Session 6. Read it cold and you will know exactly what to do.

---

## 1. State of the Engine

FastFreeEngine now has a working engine core, renderer, input system, Lua scripting subsystem, and texture loader. All features listed below compile clean on **Clang-18** and **GCC-13** with `-Wall -Wextra -Wpedantic` and zero warnings.

### What Is Built and Working

| System | Status | Notes |
|--------|--------|-------|
| Build system | Working | CMake + Ninja + mold + ccache, vcpkg, dual-compiler |
| Game loop | Working | Fixed-timestep 60Hz, spiral-of-death clamp, interpolation alpha |
| Arena allocator | Working | 64-byte cache-line aligned, per-frame reset, configurable capacity |
| ECS (EnTT wrapper) | Working | Entity lifecycle, components, views, system registration |
| Logging | Working | printf-style, compile-time level filtering, mutex only around fwrite |
| Result type | Working | Two-register error return, no exceptions |
| Renderer (OpenGL 3.3) | Working | Sprite batching (2048/batch), render queue, camera, 3 built-in shaders |
| Input system | Working | Keyboard/mouse/scroll, action mapping, GLFW callbacks, all bounds-checked |
| Lua scripting | Working | ScriptEngine, sandboxed LuaJIT, ffe.log binding, instruction budget (1M ops), doString/doFile |
| Texture loading | Working | setAssetRoot/loadTexture/unloadTexture, stb_image v2.30, 6 security conditions enforced |
| hello_sprites demo | Working | 20 bouncing sprites — flickering/jitter under investigation (see Section 2) |
| interactive_demo | Working (NEW) | WASD player + Lua startup message + ESC quit |
| headless_test demo | Working | CI-safe, no GPU required |

### Test Count: 144

| File | Tests | Coverage |
|------|-------|---------|
| test_types | 7 | Result type, sizeof assertions, tier helpers |
| test_arena_allocator | 13 | Allocation, alignment, edge cases, reset |
| test_logging | 2 | Basic operations |
| test_ecs | 9 | Entity lifecycle, components, views, systems |
| test_application | 5 | Headless lifecycle, config, shutdown |
| test_renderer_headless | 11 | RHI init/shutdown, resource creation, render queue, sort keys, camera correctness |
| test_input | 37 | Keyboard state, mouse state, scroll, action mapping, edge cases |
| test_lua_sandbox | 25 | Sandbox escape attempts, instruction budget, error recovery, ffe.log binding |
| test_texture_loader | 17 | Path traversal, absolute path rejection, dimension bounds, overflow guard, round-trips |

All 144 tests pass on both compilers.

### Codebase Size

- ~35 engine source files (.h/.cpp)
- ~4,500 lines of engine code (estimated)
- 5 Architecture Decision Records (ADRs)
- 3 complete `.context.md` files (core — updated with input, renderer — updated with texture loading, scripting — new); 3 placeholder files remain (audio, editor, physics)
- 5 ADRs in docs/architecture/

---

## 2. Demo Notes for the User

### hello_sprites (existing)

```bash
./build/examples/hello_sprites/ffe_hello_sprites
```

20 coloured sprites bounce around the window. **Visual observations from between-session testing: occasional flickering and slight movement jitter.** These are under investigation (vsync hypothesis for flicker, interpolation alpha hypothesis for jitter). Neither blocks other work.

For headless (CI-safe):

```bash
./build/examples/headless_test/ffe_headless_test
```

### interactive_demo (NEW this session)

```bash
./build/examples/interactive_demo/ffe_interactive_demo
```

Opens a 1280x720 window with:
- 1 bright-green player sprite (48x48) starting at screen centre
- 8 static slate-blue background sprites
- WASD movement with screen-edge clamping
- A Lua startup message logged on the first tick
- ESC to quit

All textures are generated from raw RGBA8 pixel data (no PNG assets in the repository yet — see Section 3, F-2).

Both windowed demos require a display. Use `xvfb-run` if running headlessly:

```bash
xvfb-run ./build/examples/interactive_demo/ffe_interactive_demo
```

---

## 3. Known Issues Carried Forward

These are tracked non-blocking issues. Each has a target session assignment (see Section 7).

| ID | Severity | Description | Target |
|----|----------|-------------|--------|
| DESIGN-1 | Medium (design) | `requestShutdown()` is not reachable from within ECS systems. Workaround: store `Application*` in ECS registry context (`world.registry().ctx()`). Undocumented and not discoverable. | Session 6 |
| M-7 | Medium (security) | `updateBuffer` offset + sizeBytes potential integer overflow before bounds check — use checked u64 arithmetic. | Session 6 |
| M-1 | Medium (security) | Uniform cache uses FNV-1a without string verification on hash collision — silently serves wrong uniform location on collision. | Session 7 |
| M-3 | Medium (design) | `SystemDescriptor.nameLength` is a manual `sizeof(name) - 1` field. Error-prone: if the name string changes without updating nameLength, profiling labels silently truncate. | Session 6 |
| F-2 | Low (usability) | No PNG assets in the repository. `loadTexture` path is implemented and security-reviewed but entirely untested end-to-end. A developer following `.context.md` will write `loadTexture("player.png")` and see a runtime failure with no sample file to test against. | Session 6 |
| F-4 | Low (usability) | `Transform` is defined in `engine/renderer/render_system.h`. A game developer writing a gameplay system expects it in core. Causes a compile error until the developer inspects the renderer header directly. | Session 6 |
| F-5 | Low (usability) | `loadTexture` always uses LINEAR filter. No NEAREST option. Pixel art games cannot use the file-loading API and get correct rendering. | Session 6 |
| M-2 | Medium (feature gap) | Lua ECS bindings are missing. Only `ffe.log()` is exposed. Scripts cannot create entities, query transforms, or read input state. Game logic cannot live in Lua yet. | Session 6 (primary goal) |
| hello_sprites flickering | Low (visual) | Intermittent flickering — vsync hypothesis (check `glfwSwapInterval(1)` is called). | Session 6 |
| hello_sprites jitter | Low (visual) | Slight sprite movement jitter — interpolation alpha not applied to sprite positions hypothesis. | Session 6 |
| submitRenderQueue dead code | Info | `submitRenderQueue()` exists but is never called (sprite batch path used). Annotated with a comment. Remove to reduce confusion. | Session 6 |
| sol2 iteration overhead | Info (perf) | `world:each()` Lua iterator has measurable overhead at 500+ scripted entities on LEGACY hardware. Not blocking for current demos. | Session 7 |
| doFile 512-byte path buffer | Low (security) | The path buffer in `doFile` is 512 bytes, stricter than PATH_MAX. Legitimate long paths will fail silently. Document or increase. | Session 7 |

---

## 4. Session 6 Goals (Priority Order)

### Goal 1: Lua ECS Bindings — PRIMARY DELIVERABLE

**Why:** This is the only Session 6 item that changes what Lua scripts can do. Without it, the scripting system is present but useless for game logic. Completing this unlocks game-dev-tester's primary objective: a demo where movement and game logic live in a `.lua` file, not hardcoded C++.

**What to expose:**
- `world:spawn()` — create a new entity, return opaque entity handle
- `world:destroy(entity)` — destroy an entity (validate handle before use)
- `world:getTransform(entity)` — return a proxy table `{x, y, rotation, scaleX, scaleY}`
- `world:setTransform(entity, x, y, rotation, scaleX, scaleY)` — write back to Transform component
- `world:getSprite(entity)` — return a proxy table `{layer, r, g, b, a}` (read-only texture handle)
- `input.isKeyHeld(key)`, `input.isKeyPressed(key)`, `input.isKeyReleased(key)` — key constants as integers or named table (`input.KEY_W`, `input.KEY_ESCAPE`, etc.)
- `engine.requestShutdown()` — triggers Application shutdown cleanly

**Security constraints for Lua ECS bindings:**
- Entity handles returned to Lua are opaque integers — Lua cannot fabricate a valid handle by arithmetic
- `getTransform` returns a value table, not a raw C++ pointer proxy — no pointer exposure
- `setTransform` validates the entity exists before writing (use `world.registry().valid(entity)`)
- Key constant values must match the underlying GLFW values — tested in unit tests
- `engine.requestShutdown()` must be idempotent (safe to call multiple times)

**Definition of done:** A Lua script can spawn an entity, move it with keyboard input, and quit the engine — all from `.lua` with no C++ game logic.

### Goal 2: Add assets/textures/ with Sample PNG

**Why:** The `loadTexture` → stb_image → GPU path is completely untested end-to-end. game-dev-tester cannot exercise it and any developer following the `.context.md` examples will fail immediately with no feedback.

**What to do:**
- Create `assets/textures/` directory in the repo
- Add at least one reference PNG: a 64x64 checkerboard or simple sprite-sheet-style image, committed to the repo
- Update `interactive_demo` to load it via `loadTexture("player.png")` (with the asset root set to the `assets/textures/` directory)
- This validates the full path end-to-end: file open, stb_image decode, `createTexture`, GPU upload

**Definition of done:** `loadTexture` is exercised in the interactive demo with a real PNG file. The texture renders visibly correctly.

### Goal 3: Fix DESIGN-1 — requestShutdown from Systems

**Why:** Every game that needs ESC-to-quit from a system (virtually all of them) currently requires the undocumented `world.registry().ctx()` workaround. This is a friction point that will affect every new developer.

**Recommended approach:** Add a `bool* m_shutdownSignal` to `World`. `Application` points it at its own `m_running` flag during construction. ECS systems call `world.requestShutdown()`, which sets `*m_shutdownSignal = false`. No virtual calls, no heap allocation, no additional indirection beyond one pointer dereference.

**Definition of done:** `world.requestShutdown()` exists and works from inside a system. interactive_demo updated to use it instead of the ctx workaround. Old workaround pattern removed from examples. `.context.md` updated.

### Goal 4: Fix SystemDescriptor Boilerplate

**Why:** Every system registration currently requires a hand-typed `nameLength = sizeof("SystemName") - 1`. This silently corrupts profiling data if the string changes without updating the length. It is the first thing a new developer must type and the error is silent.

**Recommended approach:** A `constexpr` constructor or macro:

```cpp
// Option A: constexpr constructor (preferred — no macro)
struct SystemDescriptor {
    const char* name;
    u32 nameLength;
    SystemFn fn;
    i32 priority;

    template<std::size_t N>
    constexpr SystemDescriptor(const char (&name)[N], SystemFn fn, i32 priority)
        : name(name), nameLength(N - 1), fn(fn), priority(priority) {}
};

// Usage:
SystemDescriptor desc{"MySystem", mySystemFn, 0};
```

**Definition of done:** All existing system registrations updated. nameLength is no longer hand-typed anywhere. All 144 tests still pass.

### Goal 5: Fix loadTexture Filter — Add TextureLoadParams

**Why:** Pixel art is a primary use case for FFE's LEGACY tier target hardware. LINEAR filter blurs pixel art. Developers must currently bypass `loadTexture` entirely and call `rhi::createTexture` manually to get NEAREST filter.

**Proposed API:**

```cpp
struct TextureLoadParams {
    TextureFilter filter = TextureFilter::LINEAR;
    TextureWrap   wrapS  = TextureWrap::CLAMP_TO_EDGE;
    TextureWrap   wrapT  = TextureWrap::CLAMP_TO_EDGE;
};

TextureHandle loadTexture(const char* relativePath, TextureLoadParams params = {});
```

Default-constructed `TextureLoadParams` preserves backward compatibility.

**Definition of done:** `loadTexture("sprite.png", {.filter = TextureFilter::NEAREST})` works. `.context.md` updated. api-designer has reviewed the API change.

### Goal 6: Move Transform to engine/core/components.h

**Why:** `Transform` is currently defined in `engine/renderer/render_system.h`. Every gameplay system that uses Transform must include a renderer header — wrong conceptual layer and causes compile errors when a developer writes `#include "core/ecs.h"` and tries to use Transform.

**What to do:**
- Create `engine/core/components.h` with `Transform` (and `Velocity` if the team wants to ship one)
- `engine/renderer/render_system.h` includes `engine/core/components.h` and removes the Transform definition
- All existing includes updated (grep for includes of `render_system.h` in non-renderer code)

**Definition of done:** `#include "core/components.h"` provides `ffe::Transform`. No gameplay system needs to include a renderer header for Transform. All 144 tests still pass.

### Goal 7: Investigate hello_sprites Flickering and Jitter

**Priority:** LOW — after Goals 1-3 are complete.

**Flickering hypothesis:** `glfwSwapInterval(1)` is not called, or the OpenGL context creation does not request VSync. Check `engine/renderer/rhi_opengl.cpp` window creation path. Enable explicitly and observe.

**Jitter hypothesis:** The `renderPrepareSystem` uses the ECS Transform position directly rather than an interpolated position. The interpolation `alpha` passed to `Application::render(alpha)` is not reaching the sprite transform. Audit the data flow from Application::render → renderPrepareSystem and verify alpha is applied.

**Definition of done:** Root cause identified and documented. If software-fixable, fixed and confirmed resolved.

---

## 5. Agent Dispatch Plan for Session 6

### Phase 1 — Parallel (security reviews + simpler fixes, no shared headers)

All of the following can start simultaneously:

**A. security-auditor** reviews Lua ECS binding design (shift-left, before implementation):
- Can Lua fabricate entity IDs? (must not be possible)
- Does `getTransform` expose raw pointers to Lua?
- Can `engine.requestShutdown()` be called in a way that corrupts engine state?
- Are key constants exposed safely (no arbitrary integer → key mapping attacks)?

Output: PASS or BLOCK before engine-dev begins Phase 2 Task A.

**B. engine-dev** — Fix M-7: updateBuffer integer overflow (small, isolated fix in rhi_opengl.cpp)

**C. engine-dev** — Fix M-3: SystemDescriptor boilerplate (constexpr constructor in system.h, update all registrations)

**D. engine-dev** — Move Transform to engine/core/components.h (Goal 6)

**E. engine-dev** — Add assets/textures/ with reference PNG and wire into interactive_demo (Goal 2). This can run in parallel because it only touches the examples/ directory and assets/.

**F. engine-dev** — Fix loadTexture LINEAR filter (TextureLoadParams struct) — Goal 5

### Phase 2 — Sequential (after Phase 1 security review clears)

**engine-dev** implements Lua ECS bindings (Goal 1) after security-auditor PASS on the design. This is the heaviest implementation task of the session.

Key files:
- `engine/scripting/script_engine.h` — add `registerWorld()` and `registerInput()` methods
- `engine/scripting/script_engine.cpp` — implement WorldProxy and InputProxy Lua bindings
- `engine/scripting/lua_world_proxy.h` / `lua_world_proxy.cpp` — if splitting is cleaner
- Add `engine.requestShutdown()` binding that calls back through to Application

Simultaneously:

**engine-dev** — Fix DESIGN-1: add `World::requestShutdown()` forwarding (Goal 3). This is a small change that can happen in parallel with the Lua ECS binding work, but the Lua `engine.requestShutdown()` binding should call this, so coordinate the interface first.

### Phase 3 — Reviews (parallel, after Phase 2)

**performance-critic** reviews Lua ECS binding implementation:
- Does `world:getTransform()` allocate a Lua table per call? (must avoid per-frame heap allocation)
- Does `world:each()` allocate per entity? (must avoid)
- Does `engine.requestShutdown()` introduce any locking or allocation?

**security-auditor** reviews Lua ECS binding implementation:
- Entity handle validation before component access
- No raw pointer exposure through getTransform/getSprite
- `engine.requestShutdown()` safe to call multiple times
- Key constants cannot be used to fabricate arbitrary input state

**test-engineer** writes tests (can begin in parallel with reviews):
- `tests/scripting/test_lua_ecs.cpp` — entity binding (spawn, destroy, invalid handle), getTransform/setTransform round-trip, input.isKeyHeld from Lua (using test hook), engine.requestShutdown from Lua
- Update `tests/renderer/test_texture_loader.cpp` with NEAREST filter test using TextureLoadParams
- Regression: all 144 existing tests must still pass

### Phase 4 — API Review (after Phase 3 clears)

**api-designer** reviews:
- Lua ECS binding public API (world:spawn, world:destroy, world:getTransform, world:setTransform, world:getSprite, input table, engine.requestShutdown)
- TextureLoadParams API change to loadTexture
- World::requestShutdown() C++ API
- Update `engine/scripting/.context.md` with ECS bindings section and usage patterns
- Update `engine/core/.context.md` with World::requestShutdown()
- Update `engine/renderer/.context.md` with TextureLoadParams

api-designer approval is a gate before Phase 5.

### Phase 5 — Demo (after Phase 4)

**game-dev-tester** builds a demo where game logic lives in Lua:
- Create `examples/lua_game/` with a main.cpp that sets up the engine and loads `scripts/game.lua`
- `game.lua` handles: entity spawning, WASD movement (via `input.isKeyHeld`), ESC quit (via `engine.requestShutdown()`), optional score counter displayed via `ffe.log`
- The demo must load a real PNG texture via `loadTexture` (requires Phase 1 Task E to be done)
- Report: what worked, what was friction, what was still missing

### Phase 6 — Housekeeping (parallel to Phase 5)

- **engine-dev** investigates hello_sprites flickering and jitter (Goal 7)
- **project-manager** updates devlog, writes session 7 handover
- **director** validates session 6 definition of done checklist

---

## 6. First Commands to Run at Session Start

Run these at the start of Session 6 to verify the baseline before making any changes.

```bash
# 1. Verify Clang-18 build is clean
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFFE_TIER=LEGACY \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build

# 2. Run all tests (Clang-18)
cd build && ctest --output-on-failure
cd ..

# 3. Verify GCC-13 build is clean
cmake -B build-gcc -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFFE_TIER=LEGACY
cmake --build build-gcc

# 4. Run all tests (GCC-13)
cd build-gcc && ctest --output-on-failure
cd ..

# 5. Run headless demo (CI-safe baseline)
./build/examples/headless_test/ffe_headless_test

# 6. Run interactive demo (requires display)
./build/examples/interactive_demo/ffe_interactive_demo
# Or headless:
# xvfb-run ./build/examples/interactive_demo/ffe_interactive_demo
```

**Expected baseline:** 144/144 tests pass on both compilers, zero warnings, headless demo exits cleanly, interactive demo builds and runs.

If the baseline does not pass, route to `system-engineer` before starting any feature work.

---

## 7. Known Issue Target Session Assignment

The director requested explicit session targets for all tracked issues before the backlog grows.

| ID | Description | Target Session | Priority |
|----|-------------|----------------|----------|
| M-2 | Lua ECS bindings missing | **Session 6** (Goal 1) | P0 |
| DESIGN-1 | requestShutdown from system | **Session 6** (Goal 3) | P1 |
| M-3 | SystemDescriptor nameLength boilerplate | **Session 6** (Goal 4) | P1 |
| M-7 | updateBuffer integer overflow | **Session 6** (Phase 1B) | P1 |
| F-2 | No PNG assets in repo | **Session 6** (Goal 2) | P1 |
| F-4 | Transform in renderer header | **Session 6** (Goal 6) | P1 |
| F-5 | loadTexture LINEAR hardcoded | **Session 6** (Goal 5) | P2 |
| submitRenderQueue dead code | Annotated — remove | **Session 6** (cleanup) | P3 |
| hello_sprites flickering | VSync investigation | **Session 6** (Goal 7, low pri) | P3 |
| hello_sprites jitter | Interpolation alpha investigation | **Session 6** (Goal 7, low pri) | P3 |
| sol2 iteration overhead | Per-frame Lua entity iteration at scale | **Session 7** | P2 |
| M-1 | Uniform cache hash collision | **Session 7** | P2 |
| doFile 512-byte path buffer | Below PATH_MAX, document or increase | **Session 7** | P3 |

---

## 8. Key File Locations

| File | Purpose |
|------|---------|
| `.claude/CLAUDE.md` | Engine constitution — read this first, always |
| `docs/architecture/004-lua-scripting.md` | ADR-004, Lua design (Revision 1, security-cleared) |
| `docs/architecture/ADR-005-texture-loading.md` | ADR-005, texture loading design |
| `docs/architecture/ADR-005-security-review.md` | Security-auditor shift-left review of ADR-005 |
| `docs/architecture/003-input-system.md` | ADR-003, input system design |
| `engine/scripting/script_engine.h` | ScriptEngine public API |
| `engine/scripting/script_engine.cpp` | ScriptEngine implementation |
| `engine/scripting/.context.md` | Scripting API docs (updated this session) |
| `engine/renderer/texture_loader.h` | Texture loading public API |
| `engine/renderer/texture_loader.cpp` | Texture loading implementation |
| `engine/core/input.h` | Input system public API |
| `engine/core/.context.md` | Core + Input API docs (updated this session) |
| `engine/renderer/.context.md` | Renderer + Texture API docs (updated this session) |
| `third_party/stb_image.h` | stb_image v2.30 (embedded) |
| `examples/interactive_demo/main.cpp` | Current interactive demo (new this session) |
| `vcpkg.json` | sol2 and LuaJIT added this session |

---

## 9. Session 6 Definition of Done

A session is not done until every applicable item is satisfied.

- [ ] Lua ECS bindings exist: spawn, destroy, getTransform, setTransform, getSprite, input table, engine.requestShutdown
- [ ] security-auditor reviewed Lua ECS binding design (shift-left) — PASS before implementation
- [ ] security-auditor reviewed Lua ECS binding implementation — no CRITICAL or HIGH findings
- [ ] performance-critic reviewed Lua ECS bindings — PASS or MINOR ISSUES
- [ ] test-engineer wrote tests for Lua ECS bindings — all passing
- [ ] api-designer reviewed Lua ECS API and updated scripting/.context.md
- [ ] engine/core/.context.md updated with World::requestShutdown()
- [ ] engine/renderer/.context.md updated with TextureLoadParams
- [ ] game-dev-tester built a demo where game logic lives in Lua — report written, no BLOCKERS
- [ ] assets/textures/ exists with at least one reference PNG
- [ ] loadTexture exercised end-to-end with a real PNG in a demo
- [ ] DESIGN-1 resolved: World::requestShutdown() works from systems
- [ ] M-7 fixed: updateBuffer uses checked arithmetic
- [ ] M-3 fixed: SystemDescriptor nameLength not hand-typed
- [ ] F-4 fixed: Transform lives in engine/core/components.h
- [ ] F-5 fixed: loadTexture accepts TextureLoadParams with filter option
- [ ] submitRenderQueue dead code removed
- [ ] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra -Wpedantic`
- [ ] All tests pass on both compilers (count increases from 144)
- [ ] All changes committed with Conventional Commit messages
- [ ] devlog updated, session 7 handover written

---

*End of handover. Session 6 is ready to start.*
