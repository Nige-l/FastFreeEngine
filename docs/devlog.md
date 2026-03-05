# FastFreeEngine Development Log

## 2026-03-05 — Session 1: Core Engine Skeleton

### Planned
- Architect designs core engine skeleton (ADR-001)
- Engine-dev implements the full skeleton from the architecture doc
- Performance-critic and test-engineer review the implementation

### Completed
- **ADR-001 written and approved** by architect — covers directory layout, CMake build system, fixed-timestep game loop, ECS (EnTT wrapper), arena allocator, logging system, type definitions, and architectural constraints
- **30 files implemented** by engine-dev:
  - Build system: root CMakeLists.txt, cmake/CompilerFlags.cmake, cmake/TierDefinitions.cmake, per-module CMakeLists.txt files
  - Engine core: types.h, logging.h/.cpp, arena_allocator.h/.cpp, system.h, ecs.h, application.h/.cpp
  - Tests: 17 Catch2 test cases across 4 test files
  - Placeholder .context.md files for all engine modules
- **Both compilers build clean** — clang-18 and gcc-13, zero warnings with -Wall -Wextra -Wpedantic
- **All 17 tests pass** on both compilers
- **Performance-critic verdict: MINOR ISSUES** — no blockers. Fixed strlen-per-tick (added nameLength to SystemDescriptor). Remaining minors: push_back without reserve in cold path, logging mutex scope, m_running not atomic (future concern)
- **Test-engineer assessment:** good coverage of happy paths, identified gaps for future (Result type tests, stale entity handles, requestShutdown integration test, arena edge cases)

### Deviations from ADR-001
1. `##__VA_ARGS__` replaced with `__VA_OPT__(,) __VA_ARGS__` (C++20 standard vs GNU extension)
2. `ZoneScopedN(system.name)` replaced with `ZoneScoped` + `ZoneName` (Tracy requires compile-time literals for ZoneScopedN)
3. `Result::ok()` instance method renamed to `Result::isOk()` to avoid ambiguity with static factory
4. Added `nameLength` field to `SystemDescriptor` (performance-critic feedback)

### Next Session Should Start With
- Add .gitignore for build/, build-gcc/, .cache/
- Fill test gaps identified by test-engineer (Result type, stale entity handles, requestShutdown)
- Architect designs the renderer RHI abstraction for LEGACY tier (OpenGL 3.3)
- Consider adding a README.md for the GitHub repo

---

## 2026-03-05 — Session 2 Plan: Test Hardening + Renderer RHI Design

### Goals

1. **Close all test gaps** from Session 1 so the core skeleton is fully validated before building on top of it.
2. **Design the Rendering Hardware Interface (RHI)** for LEGACY tier (OpenGL 3.3) — produce ADR-002 so renderer-specialist can implement in Session 3.
3. **Address remaining performance-critic minor issues** from Session 1 (push_back without reserve, logging mutex scope, m_running atomicity).
4. **Ship a README.md** so the GitHub repo is presentable.

### Carryover from Session 1

| Item | Status | Action |
|------|--------|--------|
| .gitignore for build/, build-gcc/, .cache/ | **Already done** — `.gitignore` at repo root covers `build/`, `build-*/`, `.cache/` | No work needed |
| Fill test gaps (Result type, stale entity handles, requestShutdown, arena edge cases) | Open | test-engineer, Task A |
| Architect designs renderer RHI for LEGACY tier | Open | architect, Task C |
| Consider README.md | Open | api-designer, Task D |

### Task Breakdown

#### Task A — Test Gap Closure (test-engineer)
**Files to create/modify:**
- `tests/core/test_types.cpp` — new file for Result type tests:
  - `Result::ok()` returns true from `isOk()` and `operator bool()`
  - `Result::fail("msg")` returns false, message matches
  - `Result::fail("")` with empty message
  - `Result::ok().message()` returns empty string
  - Verify Result fits in two registers (static_assert on sizeof)
- `tests/core/test_ecs.cpp` — add stale entity handle tests:
  - Create entity, destroy it, verify handle is no longer valid via `registry.valid()`
  - Create entity, destroy it, create new entity — verify new entity has different generation/version
- `tests/core/test_application.cpp` — add requestShutdown integration test:
  - This requires a system that can call `requestShutdown()` on the Application. If the current design does not support this (the Session 1 test acknowledged this limitation), document the gap and create a minimal workaround: e.g., pass a shutdown callback or `std::atomic<bool>*` into the system. Coordinate with engine-dev (Task B) if an interface change is needed.
- `tests/core/test_arena_allocator.cpp` — add edge case tests:
  - Allocate exactly capacity bytes — should succeed
  - Allocate capacity + 1 bytes — should return nullptr (or however the allocator signals failure)
  - Zero-byte allocation
  - Alignment: allocate 1 byte then request an 8-byte-aligned allocation, verify alignment
  - Reset then re-allocate — verify memory is reusable
- `tests/CMakeLists.txt` — add `test_types.cpp` to the build

**Definition of done:** All new tests compile on both Clang-18 and GCC-13 with zero warnings. All tests pass. No existing tests broken.

#### Task B — Performance Minor Fixes (engine-dev)
**Files to modify:**
- `engine/core/application.cpp` — three fixes:
  1. **push_back without reserve:** Find any `std::vector::push_back` in cold paths (system registration) and add `reserve()` calls or switch to a fixed-capacity container. The performance-critic flagged this as minor but it should be cleaned up now before more systems are added.
  2. **Logging mutex scope:** Review `engine/core/logging.cpp` — narrow the mutex lock scope so formatting happens outside the lock, only the final write to the sink is locked.
  3. **m_running atomicity:** Change `m_running` from `bool` to `std::atomic<bool>` in `engine/core/application.h`. Even though the engine is currently single-threaded, this is cheap insurance and the performance-critic flagged it as a future concern. Do it now while the surface area is small.
- If Task A (test-engineer) needs a design change to support requestShutdown from within a system, coordinate on the minimal interface change here.

**Definition of done:** All three issues resolved. Builds clean on both compilers. All existing + new tests pass. Ready for performance-critic re-review.

#### Task C — Renderer RHI Architecture (architect)
**File to create:**
- `docs/architecture/002-renderer-rhi.md` — ADR-002 covering:
  - RHI abstraction layer design — thin wrapper over graphics APIs, not a thick abstraction
  - LEGACY tier target: OpenGL 3.3 as the first and default backend
  - Core RHI types: Buffer, Texture, Shader, Pipeline, RenderPass, CommandList (or equivalent)
  - Resource creation and destruction patterns (no RAII pitfalls with GPU resources)
  - How the RHI integrates with the existing Application/game loop (render(alpha) call)
  - Draw call submission model — how systems submit draw commands
  - Shader management: GLSL 330 core for LEGACY, path for SPIR-V in STANDARD/MODERN
  - Window/context creation: GLFW integration point
  - Tier-conditional compilation strategy (how LEGACY vs STANDARD backends coexist)
  - Memory budget constraints per tier
  - What vcpkg dependencies are needed (GLFW, glad/glbinding, etc.)
  - File layout under `engine/renderer/` and `shaders/legacy/`

**Definition of done:** ADR-002 is written with enough detail that renderer-specialist can implement the LEGACY backend from it without clarifying questions — same standard as ADR-001. Reviewed for consistency with CLAUDE.md performance rules.

#### Task D — README.md (api-designer)
**File to create:**
- `README.md` at repo root covering:
  - Project name, one-line description, mission statement (draw from CLAUDE.md Section 10)
  - Hardware tier table (from CLAUDE.md Section 2)
  - Build instructions (CMake + Ninja + vcpkg, both Clang-18 and GCC-13)
  - How to run tests
  - Project status: "Early development — core skeleton complete, renderer in design"
  - License: MIT
  - Link to `docs/architecture/` for design documents
  - Note about AI-native documentation (.context.md files)

**Definition of done:** README renders correctly on GitHub. Build instructions are accurate and tested against the current build system.

#### Task E — Performance-Critic Re-review (performance-critic)
**Scope:** Review the changes from Task B only. Confirm the three minor issues from Session 1 are resolved.

**Definition of done:** Returns PASS or MINOR ISSUES with no new findings on the changed code.

### Agent Assignments

| Agent | Task | Depends On |
|-------|------|------------|
| **architect** | Task C — Renderer RHI ADR | Nothing — can start immediately |
| **api-designer** | Task D — README.md | Nothing — can start immediately |
| **engine-dev** | Task B — Perf minor fixes | Nothing — can start immediately |
| **test-engineer** | Task A — Test gap closure | Task B must finish first IF requestShutdown needs an interface change; otherwise arena/Result/ECS tests can start immediately in parallel with Task B |
| **performance-critic** | Task E — Re-review | Task B must be complete |

### Parallel vs Sequential

**Phase 1 — Parallel (all start immediately):**
- architect: Task C (renderer RHI design) — touches only `docs/architecture/`, no code conflicts
- api-designer: Task D (README) — touches only `README.md`, no conflicts
- engine-dev: Task B (perf fixes) — touches `engine/core/` files
- test-engineer: Task A (partial) — can start on Result type tests (`tests/core/test_types.cpp`), arena edge cases, and ECS stale handle tests immediately since these do not depend on Task B changes

**Phase 2 — Sequential (after Task B completes):**
- test-engineer: Task A (requestShutdown test) — may depend on Task B interface change
- performance-critic: Task E — must see Task B completed code

**Phase 3 — Final validation:**
- Build and run full test suite on both compilers
- Commit all changes

### Definition of Done for Session 2

All of the following must be true before the session is complete:

- [x] All Session 1 test gaps are closed: Result type, stale entity handles, requestShutdown (documented design limitation), arena edge cases
- [x] New test file `tests/core/test_types.cpp` exists and passes (7 test cases)
- [x] All three performance-critic minor issues from Session 1 are resolved in code
- [x] performance-critic returns PASS on Task B changes
- [x] ADR-002 (Renderer RHI) is written and ready for Session 3 implementation
- [x] README.md exists at repo root with accurate build instructions
- [x] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra -Wpedantic`
- [x] All tests pass on both compilers (17 -> 37 tests)
- [x] All changes committed with Conventional Commit messages
- [x] This devlog entry is updated with actual outcomes at session end

---

## 2026-03-05 — Session 3: Renderer Implementation + First Visual Demo

### Planned
- Renderer-specialist implements LEGACY tier OpenGL 3.3 backend from ADR-002
- Performance-critic and security-auditor review renderer
- Fix all blocking issues
- Create runnable demo for game-dev-tester

### Completed
- **35 new files** implementing the full LEGACY renderer:
  - RHI abstraction (rhi.h, rhi_types.h) with compile-time backend selection
  - OpenGL 3.3 backend (rhi_opengl.cpp) with fixed-size resource pools
  - Sprite batching (2048 sprites/batch, texture-break flushing)
  - Render queue with 64-byte DrawCommands and packed u64 sort keys
  - Camera (ortho/perspective), shader library (3 built-in shaders)
  - ECS render system collecting Transform+Sprite components
  - GLFW window creation, glad GL loader (embedded in third_party/)
  - GLSL 330 core shaders (sprite, solid, textured)
- **Performance-critic first review: BLOCK** — found 2 blocking issues:
  1. Use-after-free: render queue allocated from frame arena after tick() used it
  2. Sprite batch and render queue not connected (nothing actually rendered)
  Plus 3 must-fix: 512KB unnecessary zeroing, per-draw-call vertex attribs, redundant shader binds
- **Security-auditor first review: 2 HIGH** findings:
  1. Integer overflow in VRAM calculation (u32 overflow on large textures)
  2. No texture dimension validation
  Plus 4 MEDIUM findings
- **All blockers fixed by engine-dev:**
  - Render queue now uses persistent malloc'd storage, cleared per frame
  - Sprite batch drives actual rendering from render queue commands
  - VAO caches vertex attribs, shader bound once per batch
  - VRAM calc uses u64, textures validated (0 < dim <= 8192)
  - Null pointer checks on buffer/shader creation
- **Performance-critic re-review: PASS**
- **Security-auditor re-review: All HIGH resolved, merge unblocked**
- **Test count: 47** (37 core + 10 renderer headless tests), all passing on both compilers
- **Two example programs created:**
  - `examples/hello_sprites/` — 20 colored bouncing sprites in a window
  - `examples/headless_test/` — CI-safe headless demo (50 entities, 10 frames)
- Default 1x1 white texture for solid-color sprite rendering

### Known Issues / Tracked for Future
- M-7 (security): updateBuffer offset+sizeBytes potential integer overflow
- M-1 (security): uniform cache hash collisions (FNV-1a, no string verify)
- M-3 (security): sort key truncates textureId to 12 bits (safe if MAX_TEXTURES <= 4096)
- Redundant glBindBuffer(target, 0) after updateBuffer (minor perf nit)
- submitRenderQueue() is dead code (sprite batch path used instead)
- requestShutdown() not accessible from systems (design gap — needs app reference)

### Next Session Should Start With
- Run hello_sprites demo on a real display and verify visual output
- game-dev-tester builds a small game with the sprite system
- api-designer reviews the public API and writes engine/core/.context.md
- Begin Lua scripting layer (sol2 integration) so game logic moves to Lua
- Consider input handling (keyboard/mouse via GLFW)

---

## 2026-03-05 — Session 4 Plan: Input System + Scripting Design + Process Debt

### Goals

1. **Close process debt from Session 3:** api-designer reviews renderer and core public APIs, writes real `.context.md` files (not placeholders). game-dev-tester runs hello_sprites and provides user feedback.
2. **Design the input system** (keyboard/mouse via GLFW) — architect writes ADR-003, security-auditor reviews it before implementation (shift-left), engine-dev implements.
3. **Begin Lua scripting design** — architect writes ADR-004 for sol2 integration, security-auditor reviews it (Lua sandbox is a major attack surface). Implementation deferred to Session 5.

### Carryover from Session 3 (Process Debt)

| Item | Status | Action |
|------|--------|--------|
| api-designer reviews renderer public API surface | Open | Task A |
| api-designer writes `engine/renderer/.context.md` (real content, not placeholder) | Open | Task A |
| api-designer writes `engine/core/.context.md` (real content, not placeholder) | Open | Task A |
| game-dev-tester runs hello_sprites and provides user feedback | Open | Task B |

### Task Breakdown

#### Task A — API Review and .context.md Files (api-designer)
**Files to create/modify:**
- `engine/renderer/.context.md` — full content per CLAUDE.md Section 9: system purpose, public API (RHI types, sprite batch, render queue, camera, shader library), common usage patterns (creating a sprite, setting up a camera, submitting draw calls), anti-patterns, tier support, dependencies
- `engine/core/.context.md` — full content: Application lifecycle, ECS usage (EnTT wrapper), arena allocator, logging, Result type, system registration, common patterns, anti-patterns, tier support
- Review the public API surface of `engine/renderer/` and `engine/core/` — report any discoverability or naming concerns

**Definition of done:** Both `.context.md` files contain all six required sections from CLAUDE.md Section 9. api-designer has reviewed the public API and reported findings. Files are written for LLM consumption, not as placeholder stubs.

#### Task B — hello_sprites User Feedback (game-dev-tester)
**Steps:**
1. Build the project (`cmake --preset default && ninja -C build`)
2. Run `xvfb-run ./build/examples/hello_sprites/hello_sprites` (or with a real display if available)
3. If headless, run `./build/examples/headless_test/headless_test` as fallback
4. Report: Was it easy to understand the example code? What was confusing? What would a first-time user struggle with? Any crashes or unexpected behaviour?

**Definition of done:** Written feedback report appended to this devlog entry at session end. game-dev-tester has attempted to run the demo and reported results.

#### Task C — Input System Design (architect)
**File to create:**
- `docs/architecture/003-input-system.md` — ADR-003 covering:
  - GLFW key/mouse callback registration and polling
  - Input state model: pressed, held, released for keys and mouse buttons
  - Mouse position and scroll wheel
  - How input integrates with the game loop (polled at start of tick, before systems run)
  - Input mapping / action system (key -> action abstraction for rebinding)
  - ECS integration: InputState as a singleton component or global resource
  - Thread safety considerations (GLFW callbacks run on main thread)
  - File layout under `engine/core/` (input is core, not renderer)
  - Tier support (all tiers — input is not GPU-dependent)

**Definition of done:** ADR-003 is written with enough detail that engine-dev can implement without clarifying questions. Same standard as ADR-001 and ADR-002.

#### Task D — Security Review of ADR-003 (security-auditor)
**Scope:** Review ADR-003 for attack surface concerns. Input is external data — keyboard/mouse events come from the OS via GLFW. Consider:
- Can malformed input events cause buffer overflows or out-of-bounds access?
- Are there bounds on input state arrays (key count, button count)?
- Does the action mapping system accept untrusted configuration (e.g., from a config file)?
- Any denial-of-service vectors (e.g., flooding input events)?

**Definition of done:** security-auditor returns assessment with any findings classified as CRITICAL/HIGH/MEDIUM/LOW/INFO. No CRITICAL or HIGH findings, or findings are addressed in ADR-003 revisions before implementation begins.

#### Task E — Lua Scripting Design (architect)
**File to create:**
- `docs/architecture/004-lua-scripting.md` — ADR-004 covering:
  - sol2 integration strategy (header-only, vcpkg dependency)
  - Lua sandbox design: what Lua scripts CAN access (ECS queries, input state, transform/sprite components, math, logging) and what they CANNOT (filesystem, network, OS, raw pointers, C FFI)
  - Script lifecycle: load, init, update(dt), shutdown
  - How Lua scripts register as ECS systems
  - Binding generation pattern (manual sol2 bindings vs automated)
  - Error handling: Lua errors must not crash the engine
  - Performance budget: Lua script execution time cap per frame
  - Hot-reload strategy for development
  - File layout under `engine/scripting/`
  - Dependencies: sol2, Lua 5.4 (via vcpkg)

**Definition of done:** ADR-004 is written with enough detail for engine-dev + api-designer to implement the scripting layer. Sandbox boundaries are explicit and exhaustive.

#### Task F — Security Review of ADR-004 (security-auditor)
**Scope:** This is the most security-critical review so far. The Lua sandbox is a major attack surface per CLAUDE.md Section 5. Review ADR-004 for:
- Sandbox escape vectors: can a Lua script access `os`, `io`, `debug`, `loadfile`, `dofile`, `package`?
- Memory exhaustion: can a Lua script allocate unbounded memory?
- CPU exhaustion: can a Lua script run an infinite loop and freeze the engine?
- Can Lua scripts access raw pointers or corrupt engine memory through sol2 bindings?
- Are all exposed C++ objects properly lifetime-managed when accessed from Lua?
- Can untrusted scripts be loaded (e.g., mods)? If so, what is the threat model?

**Definition of done:** security-auditor returns assessment. Any CRITICAL or HIGH findings must be resolved in ADR-004 before implementation begins in a future session.

#### Task G — Input System Implementation (engine-dev)
**Files to create/modify:**
- Files as specified in ADR-003 (likely under `engine/core/`):
  - `engine/core/input.h` — input state types, action mapping
  - `engine/core/input.cpp` — GLFW callback registration, state management
  - Integration with Application game loop (poll input at start of tick)
- Update `engine/core/CMakeLists.txt` to include new files

**Definition of done:** Input system compiles clean on both compilers. Key press/release/held detection works. Mouse position and button state works. Integration with game loop is functional.

#### Task H — Input System Reviews (performance-critic, security-auditor, test-engineer)
**Performance-critic:** Review input system for hot-path allocations, cache friendliness of input state, overhead per frame.
**Security-auditor:** Final implementation review (post shift-left ADR review).
**Test-engineer:** Write unit tests for input state transitions (press -> held -> release), action mapping, edge cases.

**Definition of done:** performance-critic returns PASS or MINOR ISSUES. security-auditor returns no CRITICAL or HIGH. Tests written and passing on both compilers.

### Agent Assignments and Dispatch Order

#### Phase 1 — Parallel (all start immediately)
These tasks have no dependencies on each other:

| Agent | Task | Touches |
|-------|------|---------|
| api-designer | Task A — API review + .context.md files | `engine/renderer/.context.md`, `engine/core/.context.md` |
| game-dev-tester | Task B — hello_sprites feedback | Read-only engine code, runs examples |
| architect | Task C — ADR-003 input system | `docs/architecture/003-input-system.md` |
| architect | Task E — ADR-004 Lua scripting | `docs/architecture/004-lua-scripting.md` |

Note: Tasks C and E are both architect tasks. If the orchestrator runs a single architect instance, these are sequential (C then E). If separate instances are possible, they can be parallel since they write to different files.

#### Phase 2 — Sequential (after Phase 1 ADRs are written)
Security-auditor reviews both ADRs:

| Agent | Task | Depends On |
|-------|------|------------|
| security-auditor | Task D — Review ADR-003 | Task C complete |
| security-auditor | Task F — Review ADR-004 | Task E complete |

Tasks D and F can run in parallel if security-auditor can handle both, since they review different documents. If a single instance, run D then F (ADR-003 is likely simpler and faster to review).

#### Phase 3 — Sequential (after ADR-003 security review clears)

| Agent | Task | Depends On |
|-------|------|------------|
| engine-dev | Task G — Implement input system | Task D complete (ADR-003 security-cleared) |

#### Phase 4 — Sequential (after implementation)

| Agent | Task | Depends On |
|-------|------|------------|
| performance-critic | Task H (perf review) | Task G complete |
| security-auditor | Task H (security review) | Task G complete |
| test-engineer | Task H (tests) | Task G complete |

Performance-critic and security-auditor can review in parallel. Test-engineer can start writing tests as soon as Task G is complete (or even in parallel with reviews if the interface is stable).

#### Phase 5 — Final validation
- Build and run full test suite on both compilers
- api-designer reviews input system public API (can be deferred to Session 5 if time-constrained, but flag it)
- Commit all changes

### Definition of Done for Session 4

All of the following must be true before the session is complete:

- [x] `engine/renderer/.context.md` contains all six required sections (not a placeholder)
- [x] `engine/core/.context.md` contains all six required sections (not a placeholder)
- [x] game-dev-tester has attempted to run hello_sprites and provided written feedback
- [x] ADR-003 (input system) is written and security-reviewed before implementation
- [x] ADR-004 (Lua scripting) is written and security-reviewed (implementation deferred to Session 5)
- [x] Input system is implemented from ADR-003
- [x] performance-critic returns PASS or MINOR ISSUES on input system
- [x] security-auditor returns no CRITICAL or HIGH on input system implementation
- [x] test-engineer has written input system tests, all passing on both compilers
- [x] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra -Wpedantic`
- [x] All tests pass on both compilers
- [x] All changes committed with Conventional Commit messages
- [x] This devlog entry is updated with actual outcomes at session end

---

## 2026-03-05 — Session 4 Outcomes

### Completed

**Phase 1 — Parallel (api-designer, game-dev-tester, architect x2):**
- **api-designer** wrote full `.context.md` files for `engine/core/` and `engine/renderer/` — all six required sections (purpose, public API, usage patterns, anti-patterns, tier support, dependencies). Reviewed public API surface and reported 8 issues (naming inconsistencies, missing error returns, boilerplate friction).
- **game-dev-tester** ran headless_test successfully, reviewed hello_sprites source. Reported friction: SystemDescriptor boilerplate, no input system, no texture loading API, empty .context.md files (since fixed). Could not provide interactive gameplay feedback without input system.
- **architect** wrote ADR-003 (input system) — GLFW-backed state-based input with keyboard/mouse/scroll, action mapping, all bounds-checked.
- **architect** wrote ADR-004 (Lua scripting) — sol2/LuaJIT, whitelist sandbox, proxy-based component access, infinite loop protection.

**Phase 2 — Security shift-left reviews:**
- **security-auditor reviewed ADR-003:** 1 HIGH (pending buffer overflow in GLFW callbacks without bounds checks), 1 MEDIUM, 3 LOW. HIGH resolved in ADR-003 Revision 1 before implementation.
- **security-auditor reviewed ADR-004:** 2 CRITICAL (load() sandbox escape, infinite loop protection disabled in release), 2 HIGH (setmetatable sandbox escape, FFI available), 3 MEDIUM. All CRITICAL and HIGH resolved in ADR-004 Revision 1.

**Phase 3 — Implementation:**
- **engine-dev** implemented input system from ADR-003: `engine/core/input.h` (~200 lines), `engine/core/input.cpp` (~270 lines). GLFW callbacks with bounds checks, test hooks under `#ifdef FFE_TEST`, integrated with Application game loop.

**Phase 4 — Reviews:**
- **performance-critic:** MINOR ISSUES — bitfield packing suggestion for KeyboardState, multiple-tick-per-frame input update concern. No blockers.
- **security-auditor:** PASS — all 5 hardening requirements verified (bounds checks, scroll clamping, test hook isolation, validated indices, enum cast range check). 3 INFORMATIONAL notes.
- **test-engineer:** 37 new Catch2 tests for input system. Total test count: 84. All passing on both clang-18 and gcc-13.

**Phase 5 — CLAUDE.md process updates:**
- Added shift-left security review rule (Section 7)
- Added api-designer-before-examples sequencing rule (Section 7)
- Added game-dev-tester enforcement in session plans (Section 7)
- Updated Definition of Done (Section 8) with new checkboxes

### Session 4 Stats
- **New files:** 5 (input.h, input.cpp, test_input.cpp, ADR-003, ADR-004)
- **Modified files:** 4 (CMakeLists x2, application.cpp, CLAUDE.md)
- **New tests:** 37 (total: 84)
- **Security findings resolved:** 3 CRITICAL, 3 HIGH (all pre-implementation via shift-left)
- **ADRs produced:** 2 (003-input-system, 004-lua-scripting)
- **.context.md files completed:** 2 (core, renderer — no longer placeholders)

### Next Session Should Start With
- Implement Lua scripting layer from ADR-004 (sol2/LuaJIT integration)
- api-designer reviews input system public API and updates `engine/core/.context.md`
- game-dev-tester builds a small interactive demo using the input system
- Address api-designer's 8 API review findings (SystemDescriptor boilerplate, naming)
- Consider texture loading API (game-dev-tester friction point)

---

## 2026-03-05 — Between-Session Review (Sessions 4 → 5)

### Summary

User ran the `hello_sprites` demo on real hardware and confirmed sprites are visible and working. Two visual observations reported:

1. **Occasional flickering** — appears intermittently. Hypotheses: hardware health (display/cable/GPU stability), vsync not enabled or vsync tearing, frame timing desync with monitor refresh. To be investigated in Session 5.
2. **Slight movement jitter** — visible when watching sprite motion closely. Most likely candidate: interpolation alpha not being applied correctly, or fixed-timestep accumulator producing minor timing irregularities. To be investigated in Session 5.

Both observations are logged as known issues to investigate in Session 5. Neither is blocking — the renderer is functional and sprites render correctly.

### Session 5 Plan Confirmed

Session 5 priorities (in order):
1. Lua scripting implementation (ADR-004, sol2/LuaJIT)
2. Texture/image loading (stb_image integration)
3. api-designer reviews input API, updates `engine/core/.context.md`
4. game-dev-tester builds interactive demo (input + sprites)
5. Investigate hello_sprites flickering and jitter
6. Fix SystemDescriptor boilerplate friction

Session 5 handover document written at `docs/session5-handover.md`.
