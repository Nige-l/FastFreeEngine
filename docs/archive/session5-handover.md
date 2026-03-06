# FastFreeEngine — Session 5 Handover Document

**Prepared by:** project-manager + architect
**Date:** 2026-03-05
**Picking up from:** Session 4 complete + between-session demo review

This document is the authoritative starting point for Session 5. Read it cold and you will know exactly what to do.

---

## 1. State of the Engine

FastFreeEngine has a working, tested engine core through input handling. All features listed below compile clean on **Clang-18** and **GCC-13** with `-Wall -Wextra -Wpedantic` and zero warnings.

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
| hello_sprites demo | Working | 20 bouncing sprites — confirmed on real hardware (see Section 2) |
| headless_test demo | Working | CI-safe, no GPU required |

### Test Count: 84

| File | Tests | Coverage |
|------|-------|---------|
| test_types | 7 | Result type, sizeof assertions, tier helpers |
| test_arena_allocator | 13 | Allocation, alignment, edge cases, reset |
| test_logging | 2 | Basic operations |
| test_ecs | 9 | Entity lifecycle, components, views, systems |
| test_application | 5 | Headless lifecycle, config, shutdown |
| test_renderer_headless | 10 | RHI init/shutdown, resource creation, render queue, sort keys |
| test_input | 37 | Keyboard state, mouse state, scroll, action mapping, edge cases |

All 84 tests pass on both compilers.

### Codebase Size

- 27 engine source files (.h/.cpp)
- ~3,500 lines of engine code
- 4 Architecture Decision Records (ADRs)
- 2 complete `.context.md` files (core, renderer); 4 placeholder files remain (audio, editor, physics, scripting)
- 14 git commits

---

## 2. User Feedback from Demo

The user ran `hello_sprites` on real hardware between sessions 4 and 5.

**Confirmed working:** Sprites are visible and the demo runs.

### Observation 1: Occasional Flickering

**What was seen:** Intermittent flickering during the demo.

**Hypotheses (in order of likelihood):**
1. VSync not engaged or tearing — check `glfwSwapInterval(1)` is called and the context supports VSync
2. Frame timing desync — the fixed-timestep accumulator may allow double-steps on some ticks, causing visual discontinuity
3. Hardware/display issue — cable or GPU instability on the test machine (lower priority, diagnose software first)
4. Render queue not fully cleared between frames — possible stale draw commands

**Investigation approach:** Enable VSync explicitly, add a frame timing log to detect double-steps, verify `renderQueue.clear()` is called before each frame. If software changes eliminate the flicker, hardware is ruled out.

### Observation 2: Slight Movement Jitter

**What was seen:** Very slight jitter in sprite movement when watching closely.

**Hypotheses (in order of likelihood):**
1. Interpolation alpha not applied to sprite positions — the render system may be using the ECS transform position directly instead of the interpolated position. This is the most common cause of jitter with fixed-timestep loops.
2. Accumulator boundary artifacts — if the accumulator occasionally sits at exactly 0.0 or 1.0, sprites may snap to simulation positions
3. Frame-to-frame delta time variance from the OS scheduler introducing irregular accumulator increments

**Investigation approach:** Audit `renderPrepareSystem` — verify it receives and applies the interpolation `alpha` from `Application::render(alpha)`. If the ECS transform component stores only the current position and not the previous-frame position, interpolation cannot be applied correctly; a `previousTransform` or similar concept is needed.

**Note:** Both observations are non-blocking. The renderer is functional. Investigate in Session 5 as a lower-priority task after the primary goals are complete.

---

## 3. Session 5 Goals (Priority Order)

### Goal 1: Lua Scripting Implementation (HIGH — primary deliverable)

ADR-004 is written, security-reviewed (Revision 1 — all CRITICAL and HIGH resolved), and ready for implementation. This is the most impactful feature for game-dev-tester productivity.

**Relevant files:**
- `docs/architecture/004-lua-scripting.md` — authoritative design document, read this first
- `engine/scripting/` — implementation directory (currently a placeholder)
- `engine/scripting/.context.md` — placeholder, will need a real file after implementation

**Key ADR-004 constraints to enforce:**
- sol2 + LuaJIT via vcpkg (flag in commit message and add to `vcpkg.json`)
- Whitelist sandbox: Lua scripts have access only to what is explicitly exposed; `os`, `io`, `debug`, `loadfile`, `dofile`, `package` must be removed from the Lua state
- Infinite loop protection must be active in ALL builds (not just debug) — this was a CRITICAL finding
- `load()` must not allow loading of arbitrary bytecode or scripts with upvalues — CRITICAL finding, resolved in Revision 1
- `setmetatable` must not be exposed to untrusted scripts — HIGH finding, resolved in Revision 1
- FFI must not be available — HIGH finding, resolved in Revision 1
- Per-frame Lua execution must have a time budget (configurable, default 2ms for LEGACY tier)
- Error handling: Lua errors log and skip the script, never crash the engine

**Definition of done:** Lua scripts can run as ECS systems with access to transform/sprite components and input state. Sandbox is enforced. security-auditor has reviewed the implementation.

### Goal 2: Texture/Image Loading (HIGH — game-dev-tester friction)

game-dev-tester cannot build visually interesting demos without real texture loading. stb_image is the right fit (single-header, no extra vcpkg dependency needed beyond the header).

**Relevant files:**
- `engine/renderer/rhi.h` — `createTexture()` exists but takes raw pixel data; needs a loading path
- `third_party/` — good place for the stb_image header (same pattern as glad)

**Key design constraints:**
- Path traversal prevention: asset paths must be validated against a root asset directory before any file open
- Texture dimensions must be validated (existing rule: 0 < dim <= 8192)
- Integer overflow check: width * height * channels must be checked before allocation
- This touches file I/O — security-auditor must review the ADR before engine-dev implements (shift-left)

**Definition of done:** Game code can call a function with a file path and receive a usable `TextureHandle`. Path traversal is prevented. security-auditor has reviewed both the ADR and the implementation. stb_image header is in `third_party/`.

### Goal 3: api-designer Reviews Input API (MEDIUM)

api-designer reviewed the renderer and core APIs in Session 4 but the input system was implemented after that review. The input API needs the same treatment before game-dev-tester writes examples against it.

**What to do:**
- api-designer reads `engine/core/input.h` and the input system public surface
- Reports any discoverability, naming, or usability concerns
- Updates `engine/core/.context.md` to include the input system API and common usage patterns

**Definition of done:** `engine/core/.context.md` includes input system documentation with all six required sections from CLAUDE.md Section 9. api-designer has reported API findings.

### Goal 4: game-dev-tester Builds Interactive Demo (MEDIUM)

game-dev-tester was blocked in Session 4 due to no input system. That blocker is now resolved. After Goal 3 (api-designer review), game-dev-tester builds a small interactive demo using input + sprites.

**What to do (after api-designer completes Goal 3):**
- Build an example where player input moves a sprite (keyboard or mouse)
- Test the input API discoverability from the `.context.md` files
- Report any friction, bugs, or API gaps found during usage

**Note:** If Lua scripting (Goal 1) is complete before game-dev-tester starts, the demo should be written in Lua. If Lua is not ready, a C++ demo is acceptable and noted in the devlog.

**Definition of done:** A working interactive demo exists (in `examples/`). game-dev-tester provides a written usage report.

### Goal 5: Investigate hello_sprites Flickering and Jitter (LOW)

See Section 2 for full hypothesis lists. Investigate and fix if software-side.

**Definition of done:** Root cause identified and documented. If software-fixable, fixed and confirmed resolved. If hardware-related (display/cable), documented as "hardware-dependent, not a software bug."

### Goal 6: Fix SystemDescriptor Boilerplate (LOW)

The manual `nameLength` field in `SystemDescriptor` was flagged by both api-designer and game-dev-tester as friction. A macro or constexpr helper should compute it from the string literal.

**Relevant file:** `engine/core/system.h`

**Approach:** A `SYSTEM_DESCRIPTOR(name, fn, priority)` macro that calls `sizeof(name) - 1` for the length, or a constexpr constructor that deduces it. Must not regress performance (the length is used by Tracy for zone naming).

**Definition of done:** SystemDescriptor boilerplate reduced. Existing systems updated. All 84 tests still pass. performance-critic has confirmed no regression.

---

## 4. Agent Dispatch Plan for Session 5

### Phase 1 — Parallel (no implementation yet)

All three run simultaneously. No shared files.

**A. security-auditor** reviews ADR-005 (texture loading) for:
- Path traversal attack vectors
- Integer overflow in size calculations
- stb_image attack surface assessment
- Whether extension allowlisting is needed

Output: PASS/BLOCK verdict on docs/architecture/ADR-005-texture-loading.md

**B. api-designer** reviews existing input system public API:
- Read engine/core/input.h
- Read engine/core/.context.md
- Identify any gaps or misleading documentation
- Update engine/core/.context.md with corrections

Output: updated .context.md, written notes on any API issues found

**C. architect** (complete — ADR-005 written this session)

### Phase 2 — Sequential (after Phase 1 clears)

security-auditor must PASS ADR-005 before engine-dev touches texture loading.
ADR-004 (Lua) was security-reviewed in a prior session — implementation may begin.

**engine-dev Task A: Lua scripting (ADR-004)**
- Add sol2 to vcpkg.json (flag in commit message)
- Create engine/scripting/ subsystem
- Implement ScriptEngine class with sandbox (no io, os, require, debug, package, dofile, loadfile)
- Expose ECS entity/component API to Lua
- Enforce instruction count hook for infinite loop protection
- Key files: engine/scripting/script_engine.h, script_engine.cpp

**engine-dev Task B: Texture loading (after security-auditor PASS on ADR-005)**
- Add stb_image to vcpkg.json or embed in third_party/ (flag in commit)
- Create engine/renderer/texture_loader.h + texture_loader.cpp
- Enforce all security constraints from ADR-005
- Key constraint: path traversal check runs BEFORE any file open call

**engine-dev Task C: Performance quick wins (can run parallel to A and B)**
- sprite_batch.cpp:98-99 — skip cos/sin when rotation == 0.0f
- render_system.cpp:54-59 — hoist packPipelineBits out of per-entity loop
- render_queue.cpp:40 — remove or clearly comment submitRenderQueue() dead code
- input.cpp:179 — use sizeof(g_keyboard.current) not bare MAX_KEYS
- SystemDescriptor boilerplate friction — investigate macro or helper approach

**engine-dev Task D: Investigate hello_sprites visual bugs**
- Flickering hypothesis: vsync not requested, or monitor tearing
- Jitter hypothesis: interpolation alpha not applied to sprite world positions
- Check: is glfwSwapInterval(1) called? Is alpha passed to the sprite transform?

### Phase 3 — Review (after Phase 2)

Run in parallel:

**performance-critic** reviews Lua scripting implementation:
- Any virtual calls or std::function in the script dispatch path?
- Any per-frame heap allocations via sol2?

**security-auditor** reviews Lua scripting implementation:
- Sandbox escape vectors
- C++ symbol/path leakage in error messages
- sol2 error handling policy (panics vs recoverable errors)

**security-auditor** reviews texture loading implementation:
- Path traversal prevention actually implemented?
- Integer overflow guards present?
- stb_image outputs validated?

**test-engineer** writes tests (can start in parallel with review):
- tests/scripting/test_lua_sandbox.cpp — 14 sandbox escape tests
- tests/renderer/test_texture_loader.cpp — 10 security + correctness tests
- Fill priority gaps from test audit: input capacity exhaustion, render queue bounds, createTexture dimension bounds, renderPrepareSystem headless, camera correctness

### Phase 4 — API Review (after Phase 3 clears)

**api-designer** reviews:
- Lua scripting public API (ScriptEngine, Lua bindings for ECS)
- Texture loading public API (loadTexture, unloadTexture)
- Write/update engine/scripting/.context.md
- Write/update engine/renderer/.context.md with texture loading section

api-designer approval is a GATE before Phase 5.

### Phase 5 — Demo (after Phase 4)

**game-dev-tester** builds an interactive demo:
- Input (keyboard/mouse) + sprites + loaded textures (PNG file) + optional Lua script
- Report: what worked, what was friction, what was missing
- Demo lives in examples/interactive_demo/

### Phase 6 — Housekeeping (parallel to Phase 5)

- project-manager: update devlog, write session 6 handover
- engine-dev: address any findings from game-dev-tester usage report
- director: validate session 5 definition of done checklist

---

### Phase 7 — Final Validation (after all phases complete)

Before committing and closing Session 5:

```bash
# Clang-18 full build and test
cmake -B build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFFE_TIER=LEGACY \
  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"
cmake --build build
cd build && ctest --output-on-failure
cd ..

# GCC-13 full build and test
cmake -B build-gcc -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFFE_TIER=LEGACY
cmake --build build-gcc
cd build-gcc && ctest --output-on-failure
cd ..

# Headless baseline
./build/examples/headless_test/ffe_headless_test
```

**Expected baseline after Session 5:** All tests pass on both compilers (count increases from 84), zero warnings, headless demo exits cleanly, new interactive demo builds.

All commits use Conventional Commit messages. project-manager updates devlog, writes session 6 handover.

---

## 5. Architect Notes — Session 5 Design Constraints

This section records interface decisions that must be made before implementation starts. Engine-dev reads this section before writing any code.

### 5.1 Lua Scripting — sol2 Binding Patterns

**sol::state construction order matters.** Build the Lua state in this exact sequence:
1. Create `sol::state` with the tracking allocator (replaces the default LuaJIT allocator via `lua_newstate`)
2. Set the instruction hook via `lua_sethook` immediately — before any libraries are opened
3. Open only the safe standard library subsets (ADR-004 Section 2.2). Do NOT call `lua.open_libraries(sol::lib::base, sol::lib::io, ...)` and remove things after — open libraries individually and only the safe ones
4. Register C++ type bindings (vec2, vec3, Transform, Sprite, World proxy, InputState proxy)
5. Build the sandbox environment table — copy allowed globals into a new table, set it as the environment for all loaded scripts
6. Load `scripts/main.lua`

**Performance note on `world:each()`.** The Lua iterator for entity queries will be called every frame for every scripted system. The implementation must not create a new Lua table per entity or per component access. Use sol2's coroutine-based iterator or a stateful C++ iterator object that yields into Lua without intermediate tables. Benchmark against a C++ baseline — if overhead exceeds 3x a pure C++ view iteration for 1000 entities on LEGACY hardware, escalate to performance-critic before merging.

**Binding the World proxy, not the World directly.** Do not expose `ffe::World` to Lua directly — it exposes template member functions that sol2 cannot bind cleanly and would give scripts access to the raw EnTT registry. Instead, create `LuaWorld` (or `WorldProxy`) as a thin wrapper that exposes only the API in ADR-004 Section 2.2. The proxy holds a reference to `ffe::World` by pointer and validates it is not null on every call.

**Sandbox restrictions to enforce at the binding layer (not just by omission):**
- `s.texture` on a Sprite proxy is read-only from Lua. The binding must mark it as read-only (no setter). Lua scripts set textures via the asset loading API, not by assigning raw handles.
- Entity IDs returned from `world:spawn()` and `world:each()` are opaque. Do not expose a constructor that takes a raw integer. Lua cannot fabricate entity IDs — it can only use ones it received from the engine.
- The `World` proxy's `__newindex` metamethod (if any) must not allow scripts to add arbitrary fields to the world object.

### 5.2 Texture Loading — Public API Design

**Proposed function signature:**

```cpp
namespace ffe::renderer {

// Load a texture from a file path relative to the configured asset root.
// Returns a valid TextureHandle on success (handle.id != 0).
// Returns TextureHandle{0} and logs an error on failure.
// The caller owns the TextureHandle and must call rhi::destroyTexture() when done.
//
// Thread safety: NOT thread-safe. Call from the main thread only.
// Performance: Synchronous file I/O. Do not call per-frame. Call at scene load time.
TextureHandle loadTexture(const char* relativePath);

// Set the asset root directory. Must be called before any loadTexture() call.
// The path must be an absolute path to an existing directory.
// Returns false if the path does not exist or is not a directory.
bool setAssetRoot(const char* absolutePath);

// Query the current asset root (for debugging).
const char* getAssetRoot();

} // namespace ffe::renderer
```

**Why `TextureHandle` rather than `Result<TextureHandle>`:** The existing `createTexture()` returns `TextureHandle` with id=0 on failure. For consistency within the renderer API, `loadTexture()` follows the same pattern. The error is logged with details. If a more structured error type is needed in a future session, it can be added without breaking existing callers.

**Why `relativePath` not `absolutePath`:** All game assets must live within the asset root. Accepting absolute paths would require validating them against the root anyway. Accepting relative paths and constructing the full path inside the function makes the constraint explicit and eliminates the class of bugs where a caller accidentally passes an absolute path that happens to be outside the asset root.

**The `setAssetRoot()` call must be made during `Application::startup()` before any scripting or game systems run.** The asset root is stored as a module-level static (acceptable — it is set once at startup and never changes). The default asset root (if `setAssetRoot` is never called) is the current working directory — this is a reasonable development default and is documented as such.

**VRAM budget awareness:** After loading, `rhi::textureVramUsed()` can be queried to check total VRAM usage. `loadTexture()` itself does not enforce a VRAM budget — that is a higher-level concern for a future asset manager. For Session 5, simply ensure the function does not silently succeed when the GPU is out of memory (the `createTexture()` function already handles this — `loadTexture()` propagates the failure).

**stb_image channel handling:** Request RGBA (4 channels) from stb_image unconditionally (`desired_channels = 4`). This simplifies the RHI path — `createTexture()` always receives RGBA8 data. RGB and greyscale images will be promoted to RGBA by stb_image at load time. The extra memory cost is acceptable for LEGACY tier and eliminates a class of format-mismatch bugs.

### 5.3 Interface Decisions Required Before Implementation

The following decisions must be locked before engine-dev writes any code. If any of these are unclear, the agent must stop and ask the director (human) before proceeding.

**Decision 1: sol2 version.** Use sol2 v3.x (not v2.x). sol2 v3 has improved LuaJIT support and a cleaner API. Confirm the vcpkg port name is `sol2` and pin to a specific version in `vcpkg.json` for reproducibility.

**Decision 2: LuaJIT vs PUC Lua.** ADR-004 specifies LuaJIT. This is confirmed. The vcpkg port is `luajit`. The sol2 vcpkg port must be configured with LuaJIT as the backend, not PUC Lua.

**Decision 3: Transform lives in core, not renderer.** ADR-004 requires Lua scripts to access Transform. Transform is currently defined in `engine/renderer/render_system.h` (a known issue flagged by api-designer in Session 4). Before the Lua bindings can be written cleanly, Transform must be moved to `engine/core/components.h`. This creates a dependency on core from the scripting layer (acceptable) without creating a dependency on the renderer from the scripting layer (not acceptable — renderer is optional for a headless scripting context). Engine-dev moves Transform as the first step of Task A.

**Decision 4: ScriptEngine ownership.** `ScriptEngine` is owned by `Application` (similar to how the renderer is). Application creates it during startup (step 6), runs it during the tick loop (Lua systems run via the normal ECS dispatch), and destroys it during shutdown. The scripting engine is not a singleton — it is accessed through `Application::scriptEngine()`.

**Decision 5: Hot reload scope.** Hot reload (`inotify`-based file watcher) is a stretch goal for Session 5 — implement it only if the core scripting layer is complete with time remaining. It is guarded by `#ifdef FFE_DEV`. If not implemented in Session 5, document it as a Session 6 item.

**Decision 6: Lua error display.** In Session 5, Lua errors go to the FFE logging system only (no on-screen overlay). The on-screen error display mentioned in ADR-004 Section 5.1 is a future editor feature, not a Session 5 requirement.

---

## 6. Known Issues Carried Forward

These are tracked non-blocking issues from previous sessions. None block Session 5 work.

| ID | Severity | Description | Origin |
|----|----------|-------------|--------|
| M-7 | MEDIUM (security) | `updateBuffer` offset + sizeBytes has potential integer overflow before bounds check. Use checked arithmetic. | Session 3, security-auditor |
| M-1 | MEDIUM (security) | Uniform cache uses FNV-1a without string verification on collision. A hash collision silently serves the wrong uniform location. | Session 3, security-auditor |
| M-3 | MEDIUM (security) | Sort key truncates `textureId` to 12 bits. Safe as long as `MAX_TEXTURES <= 4096`; a future increase would silently corrupt sort keys. | Session 3, security-auditor |
| DESIGN-1 | Low (design) | `requestShutdown()` is not accessible from within ECS systems. Systems cannot cleanly trigger engine shutdown. Workaround: pass a shutdown flag pointer into system user data. Note: Lua scripts can call `engine.requestShutdown()` via the scripting layer — this design gap only affects C++ systems. | Session 3, test-engineer |

Additionally:
- `submitRenderQueue()` is dead code — addressed in Phase 6.
- Performance-critic flagged minor issues in the input system: bitfield packing for `KeyboardState` and multiple-tick-per-frame input update concern. These are LOW priority.

---

## 7. First Commands to Run at Session Start

Run these at the start of Session 5 to verify the baseline before making any changes.

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

# 5. Run headless demo (CI-safe baseline visual check)
./build/examples/headless_test/ffe_headless_test

# 6. (Optional, requires display) Run hello_sprites
./build/examples/hello_sprites/ffe_hello_sprites
# Or with virtual display:
# xvfb-run ./build/examples/hello_sprites/ffe_hello_sprites
```

**Expected baseline:** 84/84 tests pass on both compilers, zero warnings, headless demo exits cleanly.

If the baseline does not pass, route to `system-engineer` before starting any feature work.

---

## 8. Key File Locations

| File | Purpose |
|------|---------|
| `.claude/CLAUDE.md` | Engine constitution — read this first, always |
| `docs/architecture/004-lua-scripting.md` | ADR-004, authoritative Lua design (Revision 1, security-cleared) |
| `docs/architecture/ADR-005-texture-loading.md` | ADR-005, texture loading design (written — awaiting security-auditor review) |
| `docs/architecture/003-input-system.md` | ADR-003, input system design |
| `engine/core/input.h` | Input system public API |
| `engine/renderer/rhi.h` | RHI public API |
| `engine/core/.context.md` | Core API docs (needs input system section added — Phase 1) |
| `engine/renderer/.context.md` | Renderer API docs (needs texture loading section added — Phase 4) |
| `engine/scripting/` | Target directory for Lua implementation |
| `engine/scripting/.context.md` | Placeholder — to be replaced in Phase 4 |
| `third_party/` | stb_image header goes here; glad is the precedent |
| `vcpkg.json` | Add sol2 and LuaJIT here when implementing Lua |

---

*End of handover. Session 5 is ready to start.*

---

## 9. Architect Design Notes — Session 5 Constraints

### Lua Scripting (ADR-004)

sol2 binding pattern for safe function exposure:
```cpp
// CORRECT — explicit binding, no automatic exposure
lua.set_function("getTransform", [](EntityId id) -> Transform* { ... });

// WRONG — never do this, exposes entire C++ namespace
lua.open_libraries(sol::lib::package);
```

Sandbox: open ONLY these sol2 libraries: `sol::lib::base`, `sol::lib::math`, `sol::lib::string`, `sol::lib::table`. Do NOT open: `sol::lib::io`, `sol::lib::os`, `sol::lib::package`, `sol::lib::debug`, `sol::lib::ffi`, `sol::lib::jit`.

Instruction count hook (infinite loop protection):
```cpp
lua.set_hook([](lua_State*, lua_Debug*) {
    luaL_error(L, "script exceeded instruction budget");
}, LUA_MASKCOUNT, 1'000'000);
```

### Texture Loading (ADR-005)

Path validation must run as the FIRST operation — before any file system call:
```cpp
// Pseudo-code — implement in texture_loader.cpp
bool isPathSafe(const char* path) {
    if (!path || path[0] == '\0') return false;
    if (path[0] == '/' || path[0] == '\\') return false;      // absolute
    if (path[1] == ':') return false;                          // drive letter
    if (strstr(path, "../") || strstr(path, "..\\")) return false;
    if (strstr(path, "/..") || strstr(path, "\\..")) return false;
    return true;
}
```

VRAM size check before createTexture call:
```cpp
u64 requiredBytes = static_cast<u64>(width) * height * channels;
if (requiredBytes > VRAM_BUDGET_BYTES) { return TextureHandle{0}; }
```
