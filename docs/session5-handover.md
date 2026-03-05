# FastFreeEngine — Session 5 Handover Document

**Prepared by:** project-manager
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
- This touches file I/O — security-auditor must review

**Definition of done:** Game code can call a function with a file path and receive a usable `TextureHandle`. Path traversal is prevented. security-auditor has reviewed. stb_image header is in `third_party/`.

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

### Phase 1 — Parallel (start immediately, no dependencies)

| Agent | Task | Files |
|-------|------|-------|
| architect | Review ADR-004 scope, confirm implementation is ready; optionally produce a texture loading design note | `docs/architecture/` |
| security-auditor | Shift-left: review the texture loading design before implementation starts (file I/O is an attack surface per CLAUDE.md Section 5) | Read-only |
| api-designer | Review input system public API, update `engine/core/.context.md` | `engine/core/.context.md` |

Note: architect can also produce a short texture loading design note (not a full ADR unless warranted) documenting the path traversal mitigations and stb_image integration approach before engine-dev touches file I/O.

### Phase 2 — After Phase 1 security review clears (sequential)

| Agent | Task | Depends On |
|-------|------|------------|
| engine-dev | Implement Lua scripting from ADR-004 | Phase 1 (ADR-004 confirmed ready) |
| engine-dev | Implement texture loading with stb_image | Phase 1 security review of texture loading design |

If Lua scripting and texture loading have no shared headers, these can run in parallel with two engine-dev instances or be sequenced by priority (Lua first, then textures).

### Phase 3 — After implementation (sequential)

| Agent | Task | Depends On |
|-------|------|------------|
| performance-critic | Review Lua scripting implementation | Lua impl complete |
| security-auditor | Review Lua scripting implementation | Lua impl complete |
| security-auditor | Review texture loading implementation | Texture impl complete |
| test-engineer | Write tests for Lua scripting and texture loading | Implementations complete |

Performance-critic and security-auditor can review in parallel if both implementations are ready.

### Phase 4 — After api-designer review clears (sequential)

| Agent | Task | Depends On |
|-------|------|------------|
| game-dev-tester | Build interactive demo (input + sprites, Lua if available) | api-designer review complete (Goal 3), Lua impl complete (preferred) |

### Phase 5 — Low-priority tasks (can interleave with Phases 3-4)

| Agent | Task |
|-------|------|
| engine-dev | Fix SystemDescriptor boilerplate |
| engine-dev | Investigate hello_sprites flickering/jitter |
| performance-critic | Confirm no regression from SystemDescriptor change |

### Phase 6 — Final validation

- Build full test suite on both compilers: `cmake --build build && cd build && ctest --output-on-failure`
- Build full test suite on GCC-13: `cmake --build build-gcc && cd build-gcc && ctest --output-on-failure`
- Commit all changes with Conventional Commit messages
- project-manager updates devlog and writes session 6 handover

---

## 5. Known Issues Carried Forward

These are tracked non-blocking issues from previous sessions. None block Session 5 work, but they should not be forgotten.

| ID | Severity | Description | Origin |
|----|----------|-------------|--------|
| M-7 | MEDIUM (security) | `updateBuffer` offset + sizeBytes has potential integer overflow before bounds check. Use checked arithmetic. | Session 3, security-auditor |
| M-1 | MEDIUM (security) | Uniform cache uses FNV-1a without string verification on collision. A hash collision silently serves the wrong uniform location. | Session 3, security-auditor |
| M-3 | MEDIUM (security) | Sort key truncates `textureId` to 12 bits. Safe as long as `MAX_TEXTURES <= 4096`; a future increase would silently corrupt sort keys. | Session 3, security-auditor |
| DESIGN-1 | Low (design) | `requestShutdown()` is not accessible from within ECS systems. Systems cannot cleanly trigger engine shutdown. Workaround: pass a shutdown flag pointer into system user data. | Session 3, test-engineer |

Additionally:
- `submitRenderQueue()` is dead code — the sprite batch path is used directly. Either remove or wire it up.
- Performance-critic flagged minor issues in the input system: bitfield packing for `KeyboardState` (potential cache improvement) and multiple-tick-per-frame input update (input state changes mid-frame on spiral-of-death catch-up). These are LOW priority but should be addressed before the input system is considered fully hardened.

---

## 6. First Commands to Run at Session Start

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

## 7. Key File Locations

| File | Purpose |
|------|---------|
| `.claude/CLAUDE.md` | Engine constitution — read this first, always |
| `docs/architecture/004-lua-scripting.md` | ADR-004, authoritative Lua design |
| `docs/architecture/003-input-system.md` | ADR-003, input system design |
| `engine/core/input.h` | Input system public API |
| `engine/renderer/rhi.h` | RHI public API |
| `engine/core/.context.md` | Core API docs (needs input system section added) |
| `engine/renderer/.context.md` | Renderer API docs (complete) |
| `engine/scripting/` | Target directory for Lua implementation |
| `third_party/` | stb_image header goes here (see glad for precedent) |
| `vcpkg.json` | Add sol2 and LuaJIT here when implementing Lua |

---

*End of handover. Session 5 is ready to start.*
