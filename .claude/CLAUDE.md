# FastFreeEngine — Engine Constitution

Every agent reads this file at the start of every session. This document is authoritative. If it conflicts with any other document, this one wins.

---

## 1. Project Identity

FastFreeEngine (FFE) is an open-source, MIT-licensed C++ game engine. It is performance-first, always. It targets older hardware by default so that indie developers, hobbyist developers, students, and people in lower-income situations can build real games without expensive hardware.

The engine ships with AI-native documentation. Every subsystem includes a `.context.md` file written for LLMs to consume, so that developers can use AI assistants to write correct game code against FFE immediately.

FFE is built by people who love games and believe everyone deserves to make them.

---

## 2. Hardware Tier System

Every system, feature, and rendering effect must declare which tiers it supports. No exceptions.

| Tier | Era | GPU API | Min VRAM | Threading | Default |
|------|-----|---------|----------|-----------|---------|
| **RETRO** | ~2005 | OpenGL 2.1 | 512 MB | Single-core safe | No |
| **LEGACY** | ~2012 | OpenGL 3.3 | 1 GB | Single-core safe | **YES — this is the default tier** |
| **STANDARD** | ~2016 | OpenGL 4.5 / Vulkan | 2 GB | Multi-threaded | No |
| **MODERN** | ~2022 | Vulkan | 4 GB+ | Multi-threaded, ray tracing optional | No |

**Rules:**

- If you are unsure which tier to target, target **LEGACY**.
- No feature may silently degrade performance on a lower tier. If enabling a feature drops a lower tier below 60 fps, the feature must either be disabled on that tier or optimised until it meets budget.
- A feature that cannot sustain 60 fps on its declared minimum tier does not ship.
- Tier support is declared in the system's `.context.md` and in `docs/architecture/` design documents.

---

## 3. Performance Philosophy

This section is not negotiable. Every agent writing or reviewing code enforces these rules.

- **No hidden heap allocations in hot paths.** No `new`, `delete`, `malloc`, or `vector::push_back` without `reserve` in code that runs every frame.
- **No virtual function calls in per-frame code.** Use templates, function pointers in cold paths, or data-driven dispatch.
- **No `std::function` in hot paths.** It hides heap allocations.
- **Arena allocators preferred over general-purpose heap.** Per-frame scratch allocators for transient data.
- **Data-oriented design.** Think about memory layout before functionality. If the memory access pattern is wrong, the feature is wrong.
- **Cache-friendly data structures.** Arrays over linked lists. Structs of arrays over arrays of structs where performance requires it. No pointer-chasing in tight loops.
- **Measure before optimising.** Always measure after. No optimisation lands without profiling data.
- **No unnecessary copies.** Use moves or references. Pass large types by `const&`.
- **Const everything.** If it can be `const`, it is `const`.
- **The `performance-critic` agent must return PASS or MINOR ISSUES before any system merges.** BLOCK means the code does not merge until addressed.

---

## 4. C++ Standards

### Language and Compiler

- **C++20** throughout the engine.
- **Clang-18** is the primary compiler. All code must compile cleanly on Clang-18.
- **GCC-13** is the secondary compiler. All code must also compile cleanly on GCC-13.
- **Zero warnings** with `-Wall -Wextra`. No exceptions. No pragmas to suppress warnings.

### Build System

- **CMake** for project generation.
- **Ninja** as the build backend.
- **mold** as the linker.
- **ccache** for compilation caching.

### Code Rules

- No RTTI (`dynamic_cast`, `typeid`) in engine core. Disable with `-fno-rtti`.
- `const` correctness enforced everywhere — parameters, member functions, local variables.
- No raw owning pointers. Use `std::unique_ptr`, `std::shared_ptr` (sparingly), or arena allocation.
- Prefer value semantics. Move over copy.
- No exceptions in engine core for performance reasons. Use return values or error codes.
- All new vcpkg dependencies must be explicitly flagged in the commit message and added to `vcpkg.json`. Never introduce a dependency silently.

### Naming Conventions

- Types: `PascalCase` — `RenderPass`, `PhysicsWorld`
- Functions and methods: `camelCase` — `createBuffer()`, `submitDrawCall()`
- Variables and parameters: `camelCase` — `frameCount`, `maxEntities`
- Constants and enum values: `UPPER_SNAKE_CASE` — `MAX_DRAW_CALLS`, `DEFAULT_VIEWPORT_WIDTH`
- Private member variables: `m_` prefix — `m_frameAllocator`, `m_entityCount`
- Namespaces: `lowercase` — `ffe::renderer`, `ffe::physics`
- File names: `snake_case` — `render_pass.h`, `physics_world.cpp`
- Header guards: `#pragma once`

---

## 5. Security Standards

The engine will ship networking code. Games built on FFE may be played by children. Security is a first-class concern, not an afterthought.

### Threat Model

- **All external input is untrusted.** Asset files, network packets, save files, Lua scripts from untrusted sources — all must be validated before processing.
- **The Lua scripting sandbox must be escape-proof.** Game scripts must never access the filesystem, network, or OS directly.
- **Network packet parsing must be hardened.** Bounds-checked reads, validated length fields, no integer overflow in size calculations.
- **File I/O must prevent path traversal.** Asset loading and save files must not be able to read or write outside their designated directories.
- **Memory safety is paramount.** No use-after-free, double-free, or uninitialised reads.

### Review Requirements

- The `security-auditor` agent reviews anything touching: networking, file I/O, asset loading, the Lua scripting layer, or any code that processes external input.
- **CRITICAL or HIGH findings from `security-auditor` block merging.** No exceptions.

---

## 6. Agent Team and File Ownership

Each directory has a clear owner. Agents do not write to directories they do not own without explicit coordination.

| Directory / File | Owner |
|---|---|
| `engine/core/` | `engine-dev` |
| `engine/renderer/` | `renderer-specialist` |
| `engine/audio/` | `engine-dev` |
| `engine/physics/` | `engine-dev` |
| `engine/scripting/` | `engine-dev` + `api-designer` |
| `engine/editor/` | `engine-dev` |
| `tests/` | `test-engineer` |
| `docs/architecture/` | `architect` |
| `docs/agents/` | `director` |
| `docs/devlog.md` | `project-manager` |
| `docs/environment.md` | `system-engineer` |
| `.claude/agents/` | `director` |
| `.context.md` files (all directories) | `api-designer` |

### Review-Only Agents (No Write Access to Engine Code)

- `performance-critic` — reads and reports, never fixes
- `security-auditor` — reads and reports, never fixes
- `game-dev-tester` — writes test games only, never modifies engine code

---

## 7. Agent Routing Rules

### Parallel Dispatch

Dispatch agents in parallel when **all** of these are true:

- Tasks touch different directories with no shared headers
- No task depends on output from another
- No shared state or files being written concurrently

Example: `engine-dev` working on `engine/audio/` while `renderer-specialist` works on `engine/renderer/` — these can run in parallel.

### Sequential Dispatch

Dispatch agents sequentially when **any** of these are true:

- Task B needs output, headers, or interfaces defined by task A
- Tasks modify shared files or interfaces
- Scope is unclear and needs `architect` review first
- A review agent (`performance-critic`, `security-auditor`) needs to see completed work

Example: `architect` designs the ECS interface, then `engine-dev` implements it, then `performance-critic` reviews it — this is sequential.

### Shift-Left Security Review

When `architect` produces an ADR that touches any attack surface listed in Section 5 (asset loading, networking, scripting, file I/O, or external input), `security-auditor` reviews the ADR **before** implementation begins. This is in addition to the existing post-implementation security review. The goal is to bake security constraints (input validation, safe integer arithmetic, sandboxing boundaries) into the design so that implementation starts with those constraints, rather than discovering them in review of finished code.

### API Review Before Examples

When a feature adds or changes public API surface, the sequencing is: implement -> `api-designer` reviews the public API and writes/updates `.context.md` files -> `game-dev-tester` writes usage examples against the reviewed API. Examples are the artifact of `game-dev-tester`'s usage, not the implementer's. This ensures examples test API discoverability, not just correctness.

### game-dev-tester in Session Plans

`project-manager` must include `game-dev-tester` in the session plan for any feature that adds user-facing API surface. `game-dev-tester` writes examples and usage reports — engine-dev and renderer-specialist do not write end-user examples. If `game-dev-tester` cannot run (e.g., no Lua layer exists yet), document the skip explicitly in the devlog.

### Environment Failures

**Always route environment failures to `system-engineer` before retrying anything else.** If a build fails due to a missing header, broken linker, or misconfigured tool, do not retry the build. Send the error to `system-engineer`, wait for the fix, then retry.

---

## 8. Definition of Done

A feature is **not done** until every applicable item is satisfied:

- [ ] Compiles clean with zero warnings on Clang-18 (`-Wall -Wextra`)
- [ ] Compiles clean with zero warnings on GCC-13 (`-Wall -Wextra`)
- [ ] All existing tests in `tests/` pass
- [ ] New unit tests and integration tests written by `test-engineer`
- [ ] `performance-critic` returns **PASS** or **MINOR ISSUES**
- [ ] `security-auditor` has reviewed the ADR/design for attack surface features (shift-left review) — before implementation
- [ ] `security-auditor` has reviewed the implementation for anything touching the attack surface — no CRITICAL or HIGH findings
- [ ] `api-designer` has reviewed the public API and written/updated `.context.md` before examples are written
- [ ] Lua bindings exist where applicable
- [ ] `api-designer` has reviewed the Lua bindings
- [ ] A working Lua usage example exists
- [ ] `game-dev-tester` has built something with it and reported no blockers — examples are written by `game-dev-tester`, not engine-dev
- [ ] A `.context.md` file exists in the system's directory, written for LLM consumption
- [ ] Changes are committed with a clear [Conventional Commit](https://www.conventionalcommits.org/) message

---

## 9. AI-Native Documentation Philosophy

Every engine subsystem ships a `.context.md` file in its directory. These files are the primary interface between FFE and the developer's AI assistant.

### What `.context.md` Files Must Contain

1. **System purpose** — one paragraph explaining what this subsystem does
2. **Public API** — every public function/class with parameter types, return types, and brief descriptions
3. **Common usage patterns** — 3-5 code examples covering the most frequent use cases
4. **What not to do** — explicit anti-patterns and common mistakes with explanations
5. **Tier support** — which hardware tiers this system supports and any tier-specific behaviour
6. **Dependencies** — what other FFE systems this depends on

### Quality Standard

The goal: a developer points any LLM at their FFE project directory and gets correct, idiomatic game code generated immediately. If the LLM produces wrong code, the `.context.md` is insufficient and must be improved.

These files are as important as the code itself. `api-designer` owns them and keeps them accurate as APIs evolve.

---

## 10. The Mission

FFE exists to unlock creativity.

A student with a ten-year-old laptop should be able to build a game that runs beautifully and share it with the world. A kid who discovers FFE because they want to make games should come out the other side understanding systems programming, graphics, and architecture — because the engine was designed to be learned from, not just used.

We may build commercial games on FFE to sustain the project. The engine stays free and open source forever. MIT licensed. That is not up for debate.

Performance is how we deliver on this mission. By running well on old hardware, we remove the barrier between someone's idea and their ability to build it. Every performance decision we make is an accessibility decision.

---

## Quick Reference for Agents

| Question | Answer |
|---|---|
| What tier do I target? | **LEGACY** unless told otherwise |
| Can I add a dependency? | Flag it explicitly, add to `vcpkg.json`, mention in commit message |
| Can I use `virtual` in hot code? | **No** |
| Can I allocate on the heap per-frame? | **No** — use arena/scratch allocators |
| Can I suppress a warning? | **No** — fix the code |
| Can I use RTTI? | **No** in engine core |
| Can I use exceptions? | **No** in engine core |
| Who do I ask about architecture? | `architect` |
| Build is broken by environment? | `system-engineer` |
| Is my code fast enough? | Ask `performance-critic` |
| Is my code secure? | Ask `security-auditor` |
| Is my API good? | Ask `api-designer`, then `game-dev-tester` |
