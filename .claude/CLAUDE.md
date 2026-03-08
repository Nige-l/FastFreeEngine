# FastFreeEngine — CLAUDE.md

Operating manual for AI agents working on this codebase. This document is authoritative — if it conflicts with any other document, this one wins.

---

## 1. Project Identity

FastFreeEngine (FFE) is a performance-first, open-source C++ game engine targeting older/budget hardware. MIT licensed. AI-native by design — every subsystem has a `.context.md` file for LLM consumption.

**Stack:** C++20, Clang-18 (primary) / GCC-13 (secondary), CMake + Ninja + mold + ccache, OpenGL 3.3 (Legacy default) + Vulkan (compile-time `FFE_BACKEND`), GLFW, LuaJIT + sol2, Jolt Physics, miniaudio, EnTT ECS, vcpkg.

---

## 2. Build Commands

```bash
# Configure + build (Clang-18, debug)
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel $(nproc)

# Configure + build (GCC-13, release)
cmake -B build-gcc -G Ninja -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Release
cmake --build build-gcc --parallel $(nproc)

# Run all tests
cd build && ctest --parallel $(nproc) --output-on-failure

# Run a specific demo
./build/examples/3d_demo/ffe_3d_demo
./build/examples/showcase/ffe_showcase

# Build with sanitizers
cmake -B build-san -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build-san --parallel $(nproc)
```

---

## 3. Code Standards

**Language:** C++20. Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra`. No exceptions.

**Commits:** Conventional commits — `feat:`, `fix:`, `refactor:`, `perf:`, `test:`, `ci:`, `docs:`

**Performance (non-negotiable):**
- Zero heap allocations in hot paths — no `new`, `malloc`, unguarded `push_back` in per-frame code
- No `virtual` calls in per-frame code — use function pointers or templates
- No `std::function` in hot paths (hides heap allocs)
- Arena/scratch allocators for transient per-frame data
- Data-oriented design — memory layout before functionality
- `const` everything that can be `const`

**Safety:**
- No RTTI in engine core (`-fno-rtti`)
- No exceptions in engine core — return values or error codes
- No raw owning pointers — `unique_ptr`, `shared_ptr` (sparingly), or arena allocation

**Documentation:**
- Every new subsystem or API change requires a `.context.md` update (Architect owns these)
- Every non-trivial design decision requires an ADR in `docs/architecture/`

**Naming:** `PascalCase` types, `camelCase` functions/variables, `UPPER_SNAKE_CASE` constants, `m_` prefix for private members, `snake_case` file names, `#pragma once`.

---

## 4. Architecture Quick Reference

**Hardware tiers:**

| Tier | GPU API | Min VRAM | Default |
|------|---------|----------|---------|
| LEGACY | OpenGL 3.3 | 1 GB | YES |
| STANDARD | OpenGL 4.5 / Vulkan | 2 GB | No |
| MODERN | Vulkan | 4 GB+ | No |

RETRO (OpenGL 2.1) is deprecated pending ADR review. If unsure which tier to target, target LEGACY.

**Key subsystems:**
- **ECS:** EnTT with thin `World` wrapper, function-pointer system dispatch
- **Scripting:** LuaJIT + sol2, sandboxed (instruction budget, blocked globals), 200+ `ffe.*` bindings
- **Rendering:** OpenGL 3.3 backend (Legacy) with PBR, shadow mapping, skybox, post-processing (bloom, ACES tone mapping, SSAO, FXAA), GPU instancing, sprite batching 2.0. Vulkan backend via `FFE_BACKEND` compile-time switch.
- **Physics:** Jolt Physics (3D), custom 2D collision
- **Audio:** miniaudio backend, positional audio
- **Networking:** Client-server UDP, snapshot interpolation
- **Dependencies:** vcpkg for compiled libs, `third_party/` for header-only/single-file libs (see `docs/dependency-policy.md`)
- **Tests:** Catch2, tagged by subsystem (`[core]`, `[renderer]`, etc.), must run headless

---

## 5. Agent Workflow

**5 specialist agents:** Architect, Implementer, Critic, Tester, Ops

The main Claude session (human + Claude Code) handles planning and coordination. Agents do NOT self-dispatch — all coordination flows through the main session.

**Workflow for new features:**
1. **Architect** writes spec + updates `.context.md`
2. **Implementer** codes from the spec
3. **Critic** reviews for performance, security, correctness
4. **Tester** writes Catch2 tests
5. **Ops** ensures CI passes, handles build/release

**Rules:**
- Each agent stays in its lane: Implementer doesn't write tests, Tester doesn't refactor code, Critic doesn't fix bugs
- **Critic runs after EVERY batch of implementer changes — no exceptions.** Dispatch Critic in parallel with Tester whenever implementers finish, before Ops builds.
- Critic must return PASS, PASS WITH WARNINGS, or BLOCKED before code is considered done
- BLOCKER findings must be fixed before merge — no exceptions
- Parallel dispatch: when agents have no dependencies on each other, dispatch them simultaneously
- All Lua sandbox changes require Critic red-teaming AND Tester adversarial scripts
- **Standard post-implementation round (always parallel):** Critic + Tester + Ops(build) — dispatch all three simultaneously once implementers are done

**In the GIF refinement loop, Critic has two jobs every cycle (both in the same invocation):**
1. **Read every new GIF** — assess for rendering artifacts, Z-fighting, wrong colors, missing geometry, animation bugs, UI glitches, gameplay not visible, camera problems
2. **Review changed code** — wrong API calls, spawn positions, state machine bugs, perf anti-patterns, missing cleanup
Critic must assess GIFs AND code before implementers are dispatched for the next fix round.

**Critic feedback always triggers implementers — no exceptions:**
- Critic returns findings → implementers dispatched immediately to fix ALL findings in parallel
- **Implementers must report a concise change summary to Claude** (files changed, lines modified, what was done) — Claude passes this directly to Critic so Critic knows exactly where to look
- Implementers done → Critic reviews again (GIFs + code), given the specific file list from implementer reports
- This loop repeats until Critic returns PASS or PASS WITH WARNINGS (minor only)
- Code is never considered done until Critic is happy

<!-- ============================================================ -->
<!-- IMPORTANT: DO NOT REMOVE OR WEAKEN THIS SECTION             -->
<!-- This rule saves significant time and electricity.           -->
<!-- Preserve exactly during any CLAUDE.md refactor or update.  -->
<!-- ============================================================ -->
**Build strategy — two modes:**

| Mode | When | What Ops does |
|------|------|---------------|
| **FAST (refinement loop)** | GIF-driven iteration — fixing visual bugs, polishing demos | Incremental Clang-18 build only (`cmake --build build --parallel $(nproc)`). No test run. Record GIFs immediately after. Tests are outsourced to GitHub CI on push. |
| **FULL (pre-push)** | Before final git push, end of sprint, or after structural changes (CMake, new deps, compiler flags) | Clang-18 + GCC-13 builds, full `ctest` run locally, then push. GitHub CI also runs on every push as the definitive gate. |

**Default during demo refinement loop: FAST.** Ops must NOT run the full test suite between GIF iterations — it wastes time and electricity. Trust GitHub CI for test coverage.
<!-- ============================================================ -->

---

## 6. Current Sprint

See `docs/current-sprint.md` for the active task list with priorities, ownership, and acceptance criteria.

---

## Quick Reference

| Question | Answer |
|---|---|
| What tier do I target? | LEGACY unless told otherwise |
| Can I add a dependency? | Justify in `docs/dependency-policy.md`, add to `vcpkg.json`, mention in commit |
| Can I use `virtual` in hot code? | No |
| Can I allocate on heap per-frame? | No — arena/scratch allocators |
| Can I suppress a warning? | No — fix the code |
| Can I use RTTI? | No in engine core |
| Can I use exceptions? | No in engine core |
| Who writes specs? | Architect |
| Who writes code? | Implementer |
| Who reviews code? | Critic |
| Who writes tests? | Tester |
| Who handles CI/builds? | Ops |
| Who writes `.context.md`? | Architect |
| Who writes demo Lua code? | Implementer (with Architect's API spec) |
