# FastFreeEngine — Session 7 Handover Document

**Prepared by:** project-manager
**Date:** 2026-03-06
**Picking up from:** Session 6 complete — Lua ECS bindings, ShutdownSignal, FFE_SYSTEM macro, TextureLoadParams, sample PNG assets, lua_demo example

This document is the authoritative starting point for Session 7. Read it cold and you will know exactly what to do.

---

## 1. State of the Engine

FastFreeEngine has a working core, renderer, input system, Lua scripting subsystem with ECS bindings, and texture loader. All features compile clean on **Clang-18** and **GCC-13** with `-Wall -Wextra -Wpedantic` and zero warnings.

### What Is Built and Working

| System | Status | Notes |
|--------|--------|-------|
| Build system | Working | CMake + Ninja + mold + ccache, vcpkg, dual-compiler |
| Game loop | Working | Fixed-timestep 60Hz, spiral-of-death clamp, interpolation alpha (alpha unused in render — jitter fix pending) |
| Arena allocator | Working | 64-byte cache-line aligned, per-frame reset |
| ECS (EnTT wrapper) | Working | Entity lifecycle, components, views, system registration, ShutdownSignal in ctx |
| Logging | Working | printf-style, compile-time level filtering |
| Result type | Working | Two-register error return, no exceptions |
| Renderer (OpenGL 3.3) | Working | Sprite batching (2048/batch), render queue, camera, 3 built-in shaders |
| Input system | Working | Keyboard/mouse/scroll, action mapping, GLFW callbacks, all bounds-checked |
| Lua scripting | Working | ScriptEngine, sandboxed LuaJIT, ffe.log, ffe.isKeyHeld/isKeyPressed/isKeyReleased, ffe.KEY_* constants, ffe.getMouseX/Y, ffe.getTransform, ffe.setTransform (NaN/Inf rejected) |
| Texture loading | Working | loadTexture with TextureLoadParams (filter/wrap), stb_image v2.30, NEAREST filter available |
| ShutdownSignal | Working (NEW) | Any system can set ShutdownSignal::requested = true via ECS ctx |
| FFE_SYSTEM macro | Working (NEW) | Compile-time nameLength — no manual boilerplate |
| Sample PNG assets | Working (NEW) | assets/textures/white.png (1x1), assets/textures/checkerboard.png (64x64) |
| hello_sprites demo | Working | 20 bouncing sprites — flickering = AMD driver (hardware reboot needed) |
| interactive_demo | Working | WASD player + ESC quit in C++ |
| lua_demo | Working (NEW) | WASD movement fully in game.lua, checkerboard PNG loaded, first confirmed end-to-end PNG load |
| headless_test demo | Working | CI-safe, no GPU required |

### Test Count: 166

| File | Tests | Coverage |
|------|-------|---------|
| test_types | 7 | Result type, sizeof assertions, tier helpers |
| test_arena_allocator | 13 | Allocation, alignment, edge cases, reset |
| test_logging | 2 | Basic operations |
| test_ecs | 9 | Entity lifecycle, components, views, systems |
| test_application | 5 | Headless lifecycle, config, shutdown |
| test_renderer_headless | 11 | RHI init/shutdown, resource creation, render queue, sort keys, camera |
| test_input | 37 | Keyboard state, mouse state, scroll, action mapping, edge cases |
| test_lua_sandbox | 25 | Sandbox escapes, instruction budget, error recovery, ffe.log |
| test_texture_loader | 17 | Path traversal, dimension bounds, overflow guard, TextureLoadParams |
| test_lua_ecs (new Session 6) | 4 | getTransform/setTransform round-trip, NaN/Inf rejection, ShutdownSignal |

All 166 tests pass on both compilers.

---

## 2. Demo Notes for the User

### hello_sprites (existing)
```bash
./build/examples/hello_sprites/ffe_hello_sprites
```
20 coloured sprites bounce. **Flickering = AMD driver issue — reboot hardware to resolve (not a software bug).**

### interactive_demo (existing)
```bash
./build/examples/interactive_demo/ffe_interactive_demo
```
WASD player, ESC quits. Game logic in C++.

### lua_demo (NEW this session)
```bash
./build/examples/lua_demo/ffe_lua_demo
```
WASD player, ESC quits (handled in C++ — ffe.requestShutdown not yet exposed). Movement logic runs entirely in `game.lua`. Checkerboard PNG texture on the player sprite.

### headless_test (CI-safe)
```bash
./build/examples/headless_test/ffe_headless_test
```

Both windowed demos require a display. Use `xvfb-run` for headless testing.

---

## 3. Known Issues Carried Forward

| ID | Severity | Description | Target |
|----|----------|-------------|--------|
| FRICTION-1 | HIGH (usability) | No `ScriptEngine::callFunction()` — per-frame `doString` recompiles the call string 60x/second via `luaL_loadstring`. See game-dev-tester Session 6 report for full analysis. | **Session 7** (P0) |
| FRICTION-2 | MEDIUM (usability) | No `ffe.requestShutdown()` Lua binding — Lua scripts cannot trigger engine exit. ESC must be handled in C++. | **Session 7** |
| FRICTION-3 | LOW (usability) | Entity creation is C++-only. Lua cannot spawn enemies, bullets, or pickups. | **Session 7** (design + implement) |
| Jitter | LOW (visual) | `Application::render()` has `(void)alpha;` — interpolation alpha is not applied to sprite positions. Sprites jitter on sub-frame positions. Architect design needed (prev-frame storage approach). | **Session 7** |
| M-1 (renderer) | MEDIUM (security) | Uniform cache uses FNV-1a hash without string verification on collision — silently serves wrong uniform location. | **Session 8** |
| M-1 (perf, scripting) | MINOR | `ffe.getTransform` allocates a Lua table per call — per-frame heap allocation in hot path. | **Session 8** |
| M-2 (perf, scripting) | MINOR | Registry context probe on every `ffe.setTransform` call. | **Session 8** |
| doFile path buffer | LOW (security) | 512-byte path buffer is below PATH_MAX. Long legitimate paths fail silently. | **Session 8** |
| hello_sprites flickering | LOW (visual) | AMD driver issue — user hardware reboot needed. Not a software bug. | User action |
| sol2 iteration overhead | INFO (perf) | `world:each()` Lua iterator overhead at 500+ scripted entities on LEGACY hardware. | **Session 8** |

---

## 4. Session 7 Goals (Priority Order)

### Goal 1 (P0): ScriptEngine::callFunction() — FRICTION-1 HIGH

**Why this is P0:** Every Lua-driven game that calls a per-frame update function is currently recompiling a string through `luaL_loadstring` 60 times per second. This is measurably worse than `lua_getglobal + lua_pcall`. It is the highest-impact missing API for any performance-sensitive Lua gameplay.

**Proposed API:**
```cpp
// Call a named global Lua function with two arguments: entity ID and delta time.
// The function must have been defined by a prior doString/doFile call.
// Returns false and logs if the function is not found or raises a Lua error.
// Does NOT recompile anything — uses lua_getglobal + lua_pcall directly.
bool callFunction(const char* name, lua_Integer entityId, lua_Number dt);
```

Or a more general variadic form if the API designer prefers it. Either removes the `doString` workaround entirely.

**Files to modify:**
- `engine/scripting/script_engine.h` — add `callFunction` declaration
- `engine/scripting/script_engine.cpp` — implement via `lua_getglobal + lua_pcall`

**Definition of done:** `lua_demo` updated to call `scriptEngine.callFunction("update", entityId, dt)` instead of `doString`. Zero warnings. All 166 tests pass. `callFunction` returns false gracefully when the named function does not exist.

---

### Goal 2: ffe.requestShutdown() Lua Binding — FRICTION-2

**Why:** A Lua game script that cannot control its own exit is not in full control of game state. Every game needs this. The underlying `ShutdownSignal` already exists (fixed in Session 6) — this is a one-binding addition.

**Implementation note:** Retrieve the `World*` from the Lua registry (same pattern as `ffe.setTransform`), then set `world->registry().ctx().get<ffe::ShutdownSignal>().requested = true`.

**Security note:** Can a Lua script DoS the engine by calling `ffe.requestShutdown()` in a tight loop? Yes, but this is by design — the shutdown is requested on the first call and the engine exits cleanly on the next tick. Document this explicitly: calling it multiple times is safe and idempotent. No crash, no corruption.

**Files to modify:**
- `engine/scripting/script_engine.cpp` — add binding in `registerEcsBindings()`

**Definition of done:** `ffe.requestShutdown()` in Lua sets `ShutdownSignal::requested = true`. `lua_demo` `game.lua` updated to handle `ffe.KEY_ESCAPE` via `ffe.requestShutdown()`. The C++ ESC workaround in `lua_demo/main.cpp` is removed. api-designer updates `engine/scripting/.context.md`.

---

### Goal 3: Jitter Fix — Interpolation Alpha

**Why:** `Application::render()` currently has `(void)alpha;`. The interpolation alpha from the fixed-timestep accumulator is computed correctly but never used. Sprite positions are snapped to the last physics tick position rather than interpolated between ticks, causing visible jitter.

**Design question for architect:** Where does previous-frame position live?

- **Option A (simplest):** Add `prevX`, `prevY` fields to `Transform`. `Application::tick()` copies current to prev before each tick. `renderPrepareSystem` uses `lerp(prev, current, alpha)` for `DrawCommand` position.
- **Option B (cleaner separation):** Add a separate `PreviousTransform` component. Systems that need interpolation add both. Entities that do not move can skip it.

**Architect must produce a short design note** (not a full ADR — a paragraph and a struct sketch is enough) before engine-dev begins implementation.

**Files likely to modify:**
- `engine/core/components.h` — add prev fields to Transform or add PreviousTransform
- `engine/renderer/render_system.cpp` — apply alpha in renderPrepareSystem
- `engine/core/application.cpp` — copy current to prev before tick (if Option A)

**Definition of done:** hello_sprites jitter is visibly eliminated (test on real hardware). Interpolation alpha flows from Application::run() accumulator through to DrawCommand positions.

---

### Goal 4: ffe.createEntity() / ffe.destroyEntity() — FRICTION-3

**Why:** Lua game logic that spawns enemies, bullets, or pickups requires entity creation from scripts. Without it, all entity counts are hardcoded in C++.

**Design constraint:** Entities created from Lua must be tracked. If the script is reloaded (hot-reload), Lua-created entities must be destroyed cleanly to avoid leaks in the ECS. A `ScriptEngine`-owned registry of Lua-created entity IDs is the simplest approach.

**Architect design note needed:** How are Lua-created entities cleaned up on script reload? Options:
- ScriptEngine keeps a `std::vector<entt::entity>` of Lua-spawned entities, destroyed in `unloadScript()` / `doFile()` before reload.
- Or: tag Lua-created entities with a `LuaOwned` component; cleanup query destroys all tagged entities.

**Security constraint:** `ffe.destroyEntity(id)` must validate the entity exists before calling `world.registry().destroy()`. Passing an invalid ID must be a no-op (log a warning, do not crash).

**Definition of done:** `ffe.createEntity()` creates an ECS entity and returns its u32 ID. `ffe.destroyEntity(id)` safely destroys it. Lua-created entities are cleaned up on script unload. api-designer reviews the binding and updates `.context.md`.

---

### Goal 5: M-7 Verification

M-7 (updateBuffer integer overflow) was fixed in Session 6. Verify no regression in the current build before doing anything else:

```bash
cd build && ctest -R test_renderer --output-on-failure
```

If it fails, route to system-engineer before starting feature work.

---

## 5. Session 7 Dispatch Plan

### Phase 1 — Parallel (no shared headers, non-overlapping)

All of the following can start simultaneously:

| Agent | Task | Touches |
|-------|------|---------|
| **architect** | Design jitter fix: write a short design note (struct sketch + approach decision). Option A vs Option B. Must finish before engine-dev begins jitter implementation. | `docs/architecture/` or inline note — no code |
| **engine-dev** | Implement `callFunction()` (Goal 1) | `engine/scripting/script_engine.h`, `script_engine.cpp` |
| **engine-dev** | Implement `ffe.requestShutdown()` binding (Goal 2) | `engine/scripting/script_engine.cpp` |
| **engine-dev** | Begin `ffe.createEntity()` / `ffe.destroyEntity()` design + implementation (Goal 4) | `engine/scripting/script_engine.h`, `script_engine.cpp` |

Note: callFunction and ffe.requestShutdown both touch `script_engine.cpp` — these must be done by the same engine-dev instance or coordinated carefully. Treat them as a single sequential sub-task within Phase 1.

### Phase 2 — Sequential (after Phase 1)

| Agent | Task | Depends On |
|-------|------|------------|
| **security-auditor** | Review `ffe.requestShutdown()` — can a Lua script DoS the engine by calling it repeatedly? (Expected answer: yes, by design, document it.) | Phase 1 complete |
| **performance-critic** | Review `callFunction()` — is `lua_getglobal + lua_pcall` overhead acceptable per frame? (Expected: PASS — this is the standard Lua call pattern.) | Phase 1 complete |
| **engine-dev** | Implement jitter fix (after architect design note) | Phase 1 architect note complete |

### Phase 3 — Sequential (after Phase 2)

| Agent | Task | Depends On |
|-------|------|------------|
| **api-designer** | Review `callFunction` API and `ffe.requestShutdown` binding. Update `engine/scripting/.context.md` with callFunction pattern, ffe.requestShutdown usage, ffe.createEntity/destroyEntity API. | Phase 2 complete |
| **test-engineer** | Write tests for callFunction (function-not-found returns false, correct argument passing, error recovery), ffe.requestShutdown (ShutdownSignal set after call, idempotent), ffe.createEntity/destroyEntity (valid ID returned, cleanup on unload). | Phase 2 complete |

### Phase 4 — Sequential (after Phase 3)

| Agent | Task | Depends On |
|-------|------|------------|
| **game-dev-tester** | Build demo using callFunction + Lua-driven shutdown. Attempt: Lua script that uses `callFunction` for the update loop, `ffe.requestShutdown()` on ESC, `ffe.createEntity()` to spawn a second entity at startup. Report friction. | Phase 3 complete |

---

## 6. Known Issues Target Session Assignment

| ID | Description | Target | Priority |
|----|-------------|--------|----------|
| FRICTION-1 callFunction | Per-frame doString overhead | **Session 7** | P0 |
| FRICTION-2 ffe.requestShutdown | Lua cannot trigger engine exit | **Session 7** | P1 |
| FRICTION-3 entity creation from Lua | No ffe.createEntity/destroyEntity | **Session 7** | P1 |
| Jitter (interpolation alpha) | (void)alpha in render path | **Session 7** — architect design + engine-dev impl | P1 |
| M-1 (renderer) uniform hash collision | FNV-1a no string verify | **Session 8** | P2 |
| M-1 (perf) getTransform table alloc | Per-frame heap alloc in hot path | **Session 8** | P2 |
| M-2 (perf) registry probe per setTransform | Per-call ctx lookup | **Session 8** | P2 |
| doFile 512-byte path buffer | Below PATH_MAX, silent failure | **Session 8** | P3 |
| sol2 iteration overhead | world:each() at 500+ entities | **Session 8** | P2 |
| hello_sprites flickering | AMD driver — user reboot | User action | — |

---

## 7. First Commands to Run at Session Start

Run these to verify the Session 6 baseline before making any changes.

```bash
# 1. Configure and build (Clang-18, primary)
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFFE_TIER=LEGACY
cmake --build build

# 2. Run all tests (Clang-18)
cd build && ctest --output-on-failure
cd ..

# 3. Build and test (GCC-13, secondary)
cmake -B build-gcc -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-gcc
cd build-gcc && ctest --output-on-failure
cd ..

# 4. Run headless demo (CI-safe)
./build/examples/headless_test/ffe_headless_test

# 5. Run lua_demo (requires display)
./build/examples/lua_demo/ffe_lua_demo
# Or headless:
# xvfb-run ./build/examples/lua_demo/ffe_lua_demo
```

**Expected baseline:** 166/166 tests pass on both compilers, zero warnings, headless demo exits cleanly.

If the baseline does not pass, route to `system-engineer` before starting any feature work.

---

## 8. Demos Available

| Demo | Command | Notes |
|------|---------|-------|
| hello_sprites | `./build/examples/hello_sprites/ffe_hello_sprites` | Bouncing sprites — flickering = AMD driver, reboot fixes |
| interactive_demo | `./build/examples/interactive_demo/ffe_interactive_demo` | WASD player, ESC quits (C++ logic) |
| lua_demo | `./build/examples/lua_demo/ffe_lua_demo` | WASD player, movement in Lua, checkerboard PNG texture (NEW Session 6) |
| headless_test | `./build/examples/headless_test/ffe_headless_test` | CI-safe, no display needed |

---

## 9. Key File Locations

| File | Purpose |
|------|---------|
| `.claude/CLAUDE.md` | Engine constitution — read first, always |
| `docs/architecture/004-lua-scripting.md` | ADR-004, Lua design (Revision 1, security-cleared) |
| `docs/architecture/ADR-005-texture-loading.md` | ADR-005, texture loading design |
| `engine/scripting/script_engine.h` | ScriptEngine public API — primary file to extend this session |
| `engine/scripting/script_engine.cpp` | ScriptEngine implementation — primary file to extend this session |
| `engine/scripting/.context.md` | Scripting API docs — update after callFunction and ffe.requestShutdown |
| `engine/core/components.h` | Transform and other core components |
| `engine/core/application.h/.cpp` | Game loop, interpolation alpha (jitter fix goes here) |
| `engine/renderer/render_system.cpp` | renderPrepareSystem — apply alpha for jitter fix |
| `engine/core/.context.md` | Core + Input API docs |
| `engine/renderer/.context.md` | Renderer + Texture API docs |
| `examples/lua_demo/main.cpp` | lua_demo C++ host — update to use callFunction |
| `examples/lua_demo/game.lua` | lua_demo Lua script — update to use ffe.requestShutdown |
| `assets/textures/checkerboard.png` | 64x64 sample PNG (committed) |
| `docs/game-dev-tester-session6-report.md` | game-dev-tester Session 6 friction report |
| `vcpkg.json` | sol2 and LuaJIT declared here |

---

## 10. Session 7 Definition of Done

A session is not done until every applicable item is satisfied.

- [ ] `ScriptEngine::callFunction(name, entityId, dt)` exists and works
- [ ] `callFunction` uses `lua_getglobal + lua_pcall` — no string compilation per call
- [ ] `lua_demo` updated to call `callFunction` instead of `doString` for the update loop
- [ ] `ffe.requestShutdown()` Lua binding exists and sets ShutdownSignal
- [ ] `lua_demo/game.lua` uses `ffe.requestShutdown()` for ESC — C++ ESC workaround removed
- [ ] security-auditor reviewed `ffe.requestShutdown()` — no CRITICAL or HIGH
- [ ] performance-critic reviewed `callFunction()` — PASS or MINOR ISSUES
- [ ] `ffe.createEntity()` / `ffe.destroyEntity()` exist with safety validation
- [ ] Lua-created entities are cleaned up on script unload
- [ ] Architect design note for jitter fix written and recorded
- [ ] Jitter fix implemented — interpolation alpha applied to sprite positions in renderPrepareSystem
- [ ] `api-designer` reviewed callFunction API, ffe.requestShutdown, ffe.createEntity/destroyEntity
- [ ] `engine/scripting/.context.md` updated with all new bindings
- [ ] test-engineer wrote tests for callFunction, ffe.requestShutdown, ffe.createEntity/destroyEntity — all passing
- [ ] `game-dev-tester` built demo using callFunction + ffe.requestShutdown — report written, no blockers
- [ ] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra -Wpedantic`
- [ ] All tests pass on both compilers (count increases from 166)
- [ ] All changes committed with Conventional Commit messages
- [ ] devlog updated, session 8 handover written

---

*End of handover. Session 7 is ready to start.*
