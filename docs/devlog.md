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

- [ ] All Session 1 test gaps are closed: Result type, stale entity handles, requestShutdown (or documented design limitation), arena edge cases
- [ ] New test file `tests/core/test_types.cpp` exists and passes
- [ ] All three performance-critic minor issues from Session 1 are resolved in code
- [ ] performance-critic returns PASS or MINOR ISSUES on Task B changes
- [ ] ADR-002 (Renderer RHI) is written and ready for Session 3 implementation
- [ ] README.md exists at repo root with accurate build instructions
- [ ] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra -Wpedantic`
- [ ] All tests pass on both compilers (existing 17 + new tests from Task A)
- [ ] All changes committed with Conventional Commit messages
- [ ] This devlog entry is updated with actual outcomes at session end
