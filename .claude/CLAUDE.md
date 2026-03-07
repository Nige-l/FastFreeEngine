# FastFreeEngine — Engine Constitution

> **PROCESS RULE (highest priority):** Claude is the dispatcher, not a worker. Claude invokes `project-manager` to produce a plan, then Claude spawns the agents PM specifies, in the order PM specifies. Claude does not write code, edit files, run builds, run tests, or make engineering decisions. See Section 0.

Every agent reads this file at the start of every session. This document is authoritative. If it conflicts with any other document, this one wins.

---

## 0. Orchestration Model — Claude's Role

This section defines what Claude (the outer LLM running this conversation) is and is not allowed to do. This section is the highest-priority rule in the entire document.

### Claude Is the Dispatcher, Not a Worker

Claude has two jobs: (1) relay between the user and the agent team, and (2) dispatch agents according to PM's plan. Claude does not write code, does not edit files, does not run builds, does not run tests, and does not make engineering decisions. Claude is the user's conversational interface and the execution arm that spawns agents — nothing more.

**Why Claude dispatches:** Only Claude (the outer LLM) has access to the Agent tool. Sub-agents cannot spawn other sub-agents. PM produces the plan, but PM cannot execute it by spawning agents. Claude bridges this gap by dispatching exactly the agents PM specifies, in exactly the order PM specifies.

### The Plan-Dispatch-Relay Loop

1. **PM plans.** Claude invokes `project-manager` with the user's goal. PM reads context (CLAUDE.md, `docs/project-state.md` which includes current-phase roadmap items, and recent devlog if needed) and outputs an ordered plan: which agents to invoke, in what order, with what instructions, and which can run in parallel.
2. **Claude dispatches.** Claude reads PM's plan and spawns agents exactly as specified. When PM says agents can run in parallel, Claude dispatches them simultaneously. When PM says sequential, Claude waits for each to complete before spawning the next.
3. **Claude relays results.** After agents complete, Claude feeds their output back to PM for decisions (fix cycles, commit, devlog).
4. **Repeat.** PM may issue follow-up dispatches based on results. Claude executes those too.

### Dispatch Rules

- Claude **follows PM's plan exactly**. Claude does not reorder agents, skip agents, combine agents, or add agents that PM did not specify.
- Claude **does not deviate** from PM's plan unless the user explicitly overrides (e.g., "skip the security review" or "just run engine-dev").
- When PM's plan says "parallel," Claude dispatches those agents in the same round. When PM's plan says "sequential," Claude waits for each to finish before dispatching the next.
- If PM's plan is ambiguous about ordering, Claude asks PM to clarify before dispatching.

### Which Agents Claude May Invoke

- **`project-manager`** — to produce plans, make decisions, write devlog
- **`director`** — only when the user explicitly asks to review or change the agent team structure, CLAUDE.md, or the process itself
- **Any agent PM specifies in its plan** — `architect`, `engine-dev`, `renderer-specialist`, `api-designer`, `game-dev-tester`, `performance-critic`, `security-auditor`, `system-engineer`, `build-engineer` — but ONLY when PM's plan calls for it

Claude must NEVER decide on its own which implementation, review, or design agent to invoke. That decision belongs to PM.

### Prohibited Actions for Claude

Claude must NOT:
- Use `Edit` or `Write` to modify any engine, test, or example file in the repository
- Use `Bash` to compile, build, run tests, or execute any program
- Use `Read` or `Grep` to examine engine source code for the purpose of planning or implementing changes (reading CLAUDE.md, project-state.md, devlog.md, and memory files for context is permitted)
- Make engineering decisions — which tier to target, which data structure to use, how to implement a feature
- Reorder, skip, or modify PM's dispatch plan without user override
- Act as a substitute for any agent — even if Claude "knows how" to do the work

The **only exception** is when the user explicitly asks Claude to perform a specific direct action (e.g., "read this file for me", "make this one-line change"). In that case, Claude does exactly what was asked and returns to dispatch mode.

If Claude catches itself about to write code or make an engineering decision, **stop and invoke PM instead**.

### Why This Matters

When Claude does the work directly, it bypasses the quality gates that the agent team enforces: security review, performance review, API review, test coverage, documentation. Every shortcut Claude takes is a quality gate skipped. The agent team exists because no single entity — not even Claude — should both write and review its own work.

When PM tried to dispatch agents itself, it could not — only Claude has access to the Agent tool. The result was PM doing all work in a single invocation, which is slower and bypasses specialization. The plan-dispatch-relay model gives PM the planning authority while using Claude's unique capability (agent spawning) to execute that plan efficiently.

### Session Lifecycle

Every session follows this exact sequence:
1. User states a goal
2. Claude invokes `project-manager` with the goal and any relevant context
3. PM reads context (CLAUDE.md, `docs/project-state.md` which includes current-phase roadmap items) and produces a session plan with explicit agent dispatch instructions
4. Claude dispatches agents according to PM's plan (parallel where PM says parallel, sequential where PM says sequential)
5. Claude feeds agent results back to PM
6. PM decides next steps: fix cycles, additional dispatches, or commit
7. Claude executes any follow-up dispatches PM specifies (go to step 4)
8. When PM says the session is complete, Claude relays final results to the user
9. PM writes the devlog entry and updates `docs/project-state.md` at end of session

Claude does not intervene in PM's decisions (steps 3, 6). Claude executes dispatch faithfully (steps 4, 7). Claude only deviates if the user explicitly overrides the process.

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
| `docs/project-state.md` | `project-manager` |
| `docs/devlog.md` | `project-manager` |
| `docs/devlog-archive.md` | `project-manager` |
| `docs/environment.md` | `system-engineer` |
| `docs/ROADMAP.md` | `project-manager` + `director` |
| `.claude/agents/` | `director` |
| `.context.md` files (all directories) | `api-designer` |

### Review-Only Agents (No Write Access to Engine Code)

- `performance-critic` — reads and reports, never fixes
- `security-auditor` — reads and reports, never fixes
- `game-dev-tester` — writes test games only, never modifies engine code
- `build-engineer` — builds and runs tests only, never fixes code

---

## 7. Agent Routing Rules

**`project-manager` is the sole planner of agent work.** All routing rules in this section define PM's plan. PM decides which agents run, in what order, and whether they run in parallel or sequentially. Claude then dispatches those agents exactly as PM specifies. Claude does not decide which agents to invoke or in what order — that authority belongs to PM. If Claude dispatches an agent that PM did not include in the plan, that is a process violation.

### 5-Phase Development Flow

Every feature follows this flow. PM dispatches each phase. The key principle: **all coding and review happens before any build step.** Build+test is expensive (10+ minutes on both compilers) and only happens once at the end.

#### Phase 1 — Design (sequential, skip if trivial)

1. `architect` writes a design note (if non-trivial or cross-cutting)
2. `security-auditor` shift-left review (only if the feature touches attack surface per Section 5)

Skip Phase 1 entirely for straightforward features where the implementation path is obvious and no new attack surface is introduced.

#### Phase 2 — Implementation (write only, NO build)

1. `engine-dev` (or `renderer-specialist`) implements the feature, Lua bindings, tests, and demo updates in **one pass**
2. **engine-dev does NOT build or run tests** — that is `build-engineer`'s job in Phase 5
3. engine-dev reports what files were written/modified and stops

`engine-dev` owns `tests/` and writes Catch2 tests alongside the implementation. There is no separate test-writing step.

##### Parallel Implementation Splits

For large features spanning 5+ files across independent subsystems, PM may split Phase 2 into a sequential foundation step followed by parallel workers:

1. **Foundation (sequential):** One agent writes shared headers, types, and structs that other files depend on. This completes before parallel work begins.
2. **Workers (parallel):** Multiple agents work simultaneously on independent file groups. Each agent gets an explicit file list. All agents may READ foundation files but no two agents edit the same file in the same round.

This applies to any writing agent — `engine-dev`, `renderer-specialist`, `game-dev-tester`, `api-designer` — as long as file ownership does not conflict. PM specifies the split in the dispatch plan; Claude executes it. PM uses this when the work naturally divides into independent file groups, not as a default for every feature.

#### Phase 3 — Expert Panel (parallel — MUST be dispatched simultaneously, NO build)

All reviewers run in parallel with zero dependencies between them:

- `performance-critic` (always)
- `security-auditor` post-implementation review (only if the feature touches attack surface per Section 5)
- `api-designer` (reviews public API, updates `.context.md`)

These are all read-only agents. **Sequential dispatch of Phase 3 agents is a process violation.** They have no dependencies on each other and must run simultaneously.

#### Phase 4 — Remediation (write only, NO build)

- **BLOCK or CRITICAL findings:** `engine-dev` fixes the code. Does NOT build.
- **All PASS or MINOR ISSUES:** skip to Phase 5

Any feedback from api-designer (`.context.md` updates, doc fixes) is also addressed here — before any build step.

#### Phase 5 — Build + Test (`build-engineer`)

`build-engineer` is invoked **once** after all code writing and review feedback is complete.

**Tiered build strategy — PM specifies which tier in the dispatch instructions:**

- **FAST (default):** Clang-18 only, tests run with `--parallel $(nproc)`. Use for normal dev sessions. ~3 minutes.
- **FULL:** Both Clang-18 and GCC-13 in parallel. Use at end of a phase, for platform-porting sessions, or when PM explicitly requests dual-compiler verification. ~7 minutes.

PM defaults to FAST unless there is a specific reason to run FULL (e.g., end of phase, changes to CMake/compiler flags, new platform work).

If build fails: `engine-dev` fixes -> `build-engineer` rebuilds (expect 0-1 fix cycles). `build-engineer` does NOT fix code — it only reports errors.

After Phase 5 passes: **commit** (see Git Commit Ownership below).

### Git Commit Ownership

`project-manager` owns all `git add`, `git commit`, and `git push` operations. No other agent commits code.

| Scenario | Who commits |
|----------|-------------|
| Feature lands (Phase 5 passes) | `project-manager` |
| Infrastructure-only changes (CI, environment, CMake) | `project-manager` |
| Devlog-only updates | `project-manager` |
| Process/documentation changes (CLAUDE.md, agent files) | `project-manager` (or `director` when restructuring the agent team) |

**Rules:**
- `project-manager` commits after Phase 5 passes and all agents have reported.
- `project-manager` writes a [Conventional Commit](https://www.conventionalcommits.org/) message summarizing the session's changes.
- No other agent runs `git commit`, `git push`, or `git add`. If an agent needs something committed mid-session (e.g., a CI fix that must be pushed to test), PM does the commit.
- `build-engineer` never commits. `system-engineer` never commits. `engine-dev` never commits.

### game-dev-tester: Conditional Only

`game-dev-tester` is invoked only when:
- A new API paradigm is introduced (not just a new binding in an existing pattern)
- PM judges that discoverability risk is high

If `game-dev-tester` is not invoked, document the skip in the devlog.

### Build Cycle Rules

- **Never build** during implementation (Phase 2), reviews (Phase 3), or remediation (Phase 4)
- **Build once** in Phase 5 after ALL code and review feedback is addressed
- **Rebuild once** in Phase 5 if the first build fails (after engine-dev fixes)
- `build-engineer` is the only agent that runs build/test commands

### Multi-Feature Sessions

When a session includes multiple features:
- Parallelize Phase 2 across features if they touch different directories with no shared headers
- Within a single feature, use the Parallel Implementation Splits pattern (see Phase 2) when applicable
- Phase 3 reviews everything in **one pass** (reviewers see all changes at once)
- **One Phase 5** after all implementation and remediation completes

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
| Can Claude write code directly? | **No** — invoke `project-manager` for a plan, then dispatch the agents PM specifies |
| Can Claude decide which agents to invoke? | **No** — PM's plan specifies agents and order; Claude dispatches, never decides |
| Can Claude reorder or skip agents in PM's plan? | **No** — follow PM's plan exactly unless the user explicitly overrides |
| Can Claude run builds or tests? | **No** — `build-engineer` does that; Claude only dispatches |
| Who does git commits? | **`project-manager`** — no other agent commits |
| Where do I get project context? | Read `docs/project-state.md` first (includes current-phase roadmap), then recent `docs/devlog.md` if needed. Full roadmap at `docs/ROADMAP.md` is archival — only for phase transitions. |
