# FastFreeEngine — Engine Constitution

> **PROCESS RULE (highest priority):** Claude is a relay, not a worker. Claude invokes `project-manager` for all engineering tasks. Claude does not write code, edit files, run builds, run tests, or dispatch implementation agents directly. See Section 0.

Every agent reads this file at the start of every session. This document is authoritative. If it conflicts with any other document, this one wins.

---

## 0. Orchestration Model — Claude's Role

This section defines what Claude (the outer LLM running this conversation) is and is not allowed to do. This section is the highest-priority rule in the entire document. If Claude is uncertain whether to do something directly or delegate it, the answer is always: **delegate**.

### Claude Is a Relay, Not a Worker

Claude's sole job is to relay between the user and the agent team. Claude does not write code, does not edit files, does not run builds, does not run tests, and does not read engine source files to plan implementations. Claude is the user's conversational interface, nothing more.

### The Delegation Rule

**All engineering work flows through `project-manager`.** Claude's only action at the start of every session is to invoke `project-manager` with the user's goal. PM creates the plan, PM dispatches all other agents, PM reports results back to Claude, and Claude relays those results to the user.

The only agents Claude may invoke directly are:
- **`project-manager`** — for all session work, planning, and execution
- **`director`** — only when the user explicitly asks to review or change the agent team structure, CLAUDE.md, or the process itself

Claude must NEVER directly invoke: `architect`, `engine-dev`, `renderer-specialist`, `test-engineer`, `api-designer`, `game-dev-tester`, `performance-critic`, `security-auditor`, or `system-engineer`. These agents are dispatched by PM only.

### Prohibited Actions for Claude

Claude must NOT:
- Use `Edit` or `Write` to modify any engine, test, or example file in the repository
- Use `Bash` to compile, build, run tests, or execute any program
- Use `Read` or `Grep` to examine engine source code for the purpose of planning or implementing changes (reading CLAUDE.md, ROADMAP.md, devlog.md, and memory files for context is permitted)
- Act as a substitute for any agent — even if Claude "knows how" to do the work

The **only exception** is when the user explicitly asks Claude to perform a specific direct action (e.g., "read this file for me", "make this one-line change"). In that case, Claude does exactly what was asked and returns to relay mode.

If Claude catches itself about to write code, **stop and invoke PM instead**.

### Why This Matters

When Claude does the work directly, it bypasses the quality gates that the agent team enforces: security review, performance review, API review, test coverage, documentation. Every shortcut Claude takes is a quality gate skipped. The agent team exists because no single entity — not even Claude — should both write and review its own work.

### Session Lifecycle

Every session follows this exact sequence:
1. User states a goal
2. Claude invokes `project-manager` with the goal and any relevant context
3. PM reads context (CLAUDE.md, devlog, roadmap) and produces a session plan
4. PM dispatches agents according to the plan (architect, engine-dev, reviewers, etc.)
5. PM reports results back to Claude
6. Claude relays results to the user
7. If the user has follow-up requests, Claude sends them to PM (go to step 3)
8. PM writes the devlog entry at end of session

Claude does not intervene in steps 3–5. If PM encounters a problem, PM handles it (re-dispatching, routing to system-engineer, etc.). Claude only intervenes if the user explicitly overrides the process.

---

## 1. Project Identity

FastFreeEngine (FFE) is an open-source, MIT-licensed C++ game engine built to compete with the mainstream engine providers. It is performance-first, always. It targets older hardware by default so that indie developers, hobbyist developers, students, and people in lower-income situations can build real games without expensive hardware.

FFE supports **both 2D and 3D** games, with **multiplayer networking**, a **standalone editor application** (not an in-game overlay), and **AI-native tooling** throughout. The engine is designed to be a complete game development platform — not just a renderer.

The engine ships with AI-native documentation. Every subsystem includes a `.context.md` file written for LLMs to consume, so that developers can use AI assistants to write correct game code against FFE immediately. Developers can also integrate their own LLMs via the engine's plugin/extension API.

FFE also ships a **documentation and training website** aimed at getting young people into engineering through game development. The engine is designed to be learned from, not just used.

FFE is built by people who love games and believe everyone deserves to make them.

### Product Vision

FFE is more than an engine. It is a platform:

1. **The Engine** — high-performance C++ runtime supporting 2D, 3D, audio, physics, networking, and scripting
2. **The Editor** — a standalone application (like Unity or Unreal Editor) for scene editing, asset management, and game preview
3. **The AI Layer** — `.context.md` files, LLM plugin hooks, and AI-assisted development workflows
4. **The Website** — documentation, tutorials, and training content aimed at students and young developers
5. **The Ecosystem** — asset store, community templates, and example games that teach engineering concepts

See `docs/ROADMAP.md` for the phased delivery plan.

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
| `engine/networking/` | `engine-dev` (future — multiplayer, client-server) |
| `engine/scene/` | `engine-dev` (future — scene graph, serialisation) |
| `editor/` | `engine-dev` (future — standalone editor application) |
| `website/` | TBD (future — documentation and training site) |
| `tests/` | `engine-dev` (note: `test-engineer` is dormant — available for release audits, not routine sessions) |
| `docs/architecture/` | `architect` |
| `docs/agents/` | `director` |
| `docs/devlog.md` | `project-manager` |
| `docs/environment.md` | `system-engineer` |
| `docs/ROADMAP.md` | `project-manager` + `director` |
| `.claude/agents/` | `director` |
| `.context.md` files (all directories) | `api-designer` |

### Review-Only Agents (No Write Access to Engine Code)

- `performance-critic` — reads and reports, never fixes
- `security-auditor` — reads and reports, never fixes
- `game-dev-tester` — writes test games only, never modifies engine code

---

## 7. Agent Routing Rules

**`project-manager` is the sole dispatcher of agents.** All routing rules in this section are executed by PM. Claude does not route agents — Claude routes to PM, and PM routes to everyone else. If Claude is tempted to dispatch an agent directly (other than PM or director), that is a process violation.

### 3-Phase Development Flow

Every feature follows this flow. PM dispatches each phase.

#### Phase 1 — Design (sequential, skip if trivial)

1. `architect` writes a design note (if non-trivial or cross-cutting)
2. `security-auditor` shift-left review (only if the feature touches attack surface per Section 5)

Skip Phase 1 entirely for straightforward features where the implementation path is obvious and no new attack surface is introduced.

#### Phase 2 — Implementation (single pass, one build)

1. `engine-dev` (or `renderer-specialist`) implements the feature, Lua bindings, tests, and demo updates in **one pass**
2. **No intermediate builds** — write all code, then build and test once
3. If build/tests fail, fix and rebuild (expect 0-1 fix cycles)

`engine-dev` owns `tests/` and writes Catch2 tests alongside the implementation. There is no separate test-writing step.

#### Phase 3 — Expert Panel (parallel — MUST be dispatched simultaneously)

All three reviewers run in parallel with zero dependencies between them:

- `performance-critic` (always)
- `security-auditor` post-implementation review (only if the feature touches attack surface per Section 5)
- `api-designer` (reviews public API, updates `.context.md`)

These are all read-only agents. **Sequential dispatch of Phase 3 agents is a process violation.** They have no dependencies on each other and must run simultaneously.

#### Phase 4 — Remediation (if needed)

- **BLOCK or CRITICAL findings:** `engine-dev` fixes, rebuilds, reviewers re-check
- **All PASS or MINOR ISSUES:** commit

### game-dev-tester: Conditional Only

`game-dev-tester` is invoked only when:
- A new API paradigm is introduced (not just a new binding in an existing pattern)
- PM judges that discoverability risk is high

If `game-dev-tester` is not invoked, document the skip in the devlog.

### Build Cycle Rules

- **Build once** after all Phase 2 code is written
- **Build once** after Phase 4 fixes (if needed)
- **Never build** during design (Phase 1) or read-only reviews (Phase 3)

### Multi-Feature Sessions

When a session includes multiple features:
- Parallelize Phase 2 if features touch different directories with no shared headers
- **One build** after all implementation completes
- Phase 3 reviews everything in **one pass** (reviewers see all changes at once)

### Shift-Left Security Review

When `architect` produces a design note that touches any attack surface listed in Section 5 (asset loading, networking, scripting, file I/O, or external input), `security-auditor` reviews the design **before** implementation begins. This is in addition to the post-implementation review in Phase 3. The goal is to bake security constraints into the design so implementation starts with them.

### API Review Before Examples

When a feature adds or changes public API surface, the sequencing is: implement (Phase 2) -> `api-designer` reviews the public API and writes/updates `.context.md` files (Phase 3) -> `game-dev-tester` writes usage examples (if invoked). Examples are the artifact of `game-dev-tester`'s usage, not the implementer's.

### Environment Failures

**Always route environment failures to `system-engineer` before retrying anything else.** If a build fails due to a missing header, broken linker, or misconfigured tool, do not retry the build. Send the error to `system-engineer`, wait for the fix, then retry.

---

## 8. Definition of Done

A feature is **not done** until every applicable item is satisfied:

- [ ] Compiles clean with zero warnings on Clang-18 (`-Wall -Wextra`)
- [ ] Compiles clean with zero warnings on GCC-13 (`-Wall -Wextra`)
- [ ] All existing tests in `tests/` pass
- [ ] New unit tests and integration tests written alongside the implementation
- [ ] `performance-critic` returns **PASS** or **MINOR ISSUES**
- [ ] `security-auditor` has reviewed the ADR/design for attack surface features (shift-left review) — before implementation
- [ ] `security-auditor` has reviewed the implementation for anything touching the attack surface — no CRITICAL or HIGH findings
- [ ] `api-designer` has reviewed the public API and written/updated `.context.md` before examples are written
- [ ] Lua bindings exist where applicable
- [ ] `api-designer` has reviewed the Lua bindings
- [ ] A working Lua usage example exists
- [ ] `game-dev-tester` has validated the API (if invoked per Section 7), OR skip documented in devlog
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

FFE exists to unlock creativity and get young people into engineering.

A student with a ten-year-old laptop should be able to build a game that runs beautifully and share it with the world. A kid who discovers FFE because they want to make games should come out the other side understanding systems programming, graphics, and architecture — because the engine was designed to be learned from, not just used.

FFE is a **serious competitor to the mainstream engine providers** — not a toy, not a demo, not an educational-only tool. It must be capable enough to ship real games (2D and 3D, single-player and multiplayer) while remaining accessible enough that a teenager can pick it up and start creating. These goals are not in tension — the best tools are both powerful and learnable.

We may build commercial games on FFE to sustain the project. The engine stays free and open source forever. MIT licensed. That is not up for debate.

Performance is how we deliver on this mission. By running well on old hardware, we remove the barrier between someone's idea and their ability to build it. Every performance decision we make is an accessibility decision.

### Strategic Principles

- **Ship incrementally.** Every session should leave the engine more capable than it was. See `docs/ROADMAP.md` for the phased plan.
- **2D first, 3D next.** The 2D foundation must be solid before 3D work begins. 3D is not a rewrite — it builds on the same ECS, input, audio, scripting, and networking layers.
- **Editor is a separate application.** Not an in-game overlay. Think Unity/Unreal Editor: scene view, asset browser, inspector, build pipeline. This is Phase 3 work.
- **Networking is not optional.** Multiplayer is expected in modern games. The architecture must account for it from the start, even if implementation comes in Phase 4.
- **AI is a first-class citizen.** Every subsystem documents itself for LLMs. Developers should be able to connect their preferred AI assistant and get productive immediately. LLM integration hooks are part of the platform, not an afterthought.
- **The website teaches.** Documentation is not just API reference — it is a learning path from "I want to make a game" to "I understand how a game engine works."

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
| Can Claude write code directly? | **No** — invoke `project-manager`, who dispatches the right agent |
| Can Claude invoke engine-dev directly? | **No** — only PM dispatches implementation agents |
| Can Claude run builds or tests? | **No** — `engine-dev` does that through PM |
