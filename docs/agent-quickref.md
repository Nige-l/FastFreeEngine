# FFE Agent Quick Reference

> Derived from `.claude/CLAUDE.md`. CLAUDE.md wins on any conflict. Read this in 30 seconds.

---

## Hardware Tiers

| Tier | Era | GPU API | Min VRAM | Note |
|------|-----|---------|----------|------|
| RETRO | ~2005 | OpenGL 2.1 | 512 MB | Single-core safe |
| **LEGACY** | ~2012 | OpenGL 3.3 | 1 GB | **DEFAULT — target this if unsure** |
| STANDARD | ~2016 | OpenGL 4.5 / Vulkan | 2 GB | Multi-threaded |
| MODERN | ~2022 | Vulkan | 4 GB+ | Multi-threaded, ray tracing optional |

**Default rule:** If unsure which tier to target → **LEGACY**.

Every feature must declare its minimum tier in `.context.md` and `docs/architecture/`. A feature that cannot sustain 60 fps on its declared minimum tier does not ship.

---

## Performance Rules (non-negotiable)

- No heap alloc in hot paths — no `new`/`malloc`/`push_back` without `reserve` per frame
- No virtual function calls in per-frame code — use templates or function pointers
- No `std::function` in hot paths — it hides heap allocations
- Arena/scratch allocators for transient per-frame data
- Data-oriented design — memory layout before functionality; wrong access pattern = wrong feature
- Cache-friendly structures — arrays over linked lists; no pointer-chasing in tight loops
- No unnecessary copies — move or `const&`; pass large types by `const&`
- `const` everything that can be `const`

---

## Naming Conventions

| Kind | Convention | Example |
|------|-----------|---------|
| Types | `PascalCase` | `RenderPass`, `PhysicsWorld` |
| Functions / variables | `camelCase` | `createBuffer()`, `frameCount` |
| Constants / enum values | `UPPER_SNAKE_CASE` | `MAX_DRAW_CALLS` |
| Private member variables | `m_` prefix | `m_frameAllocator` |
| Namespaces | `lowercase` | `ffe::renderer` |
| File names | `snake_case` | `render_pass.h` |
| Header guards | `#pragma once` | — |

---

## 5-Phase Development Flow

1. **Design** — `architect` writes ADR (skip if trivial). `security-auditor` shift-left review if attack surface touched. **NO builds.**
2. **Implementation** — `engine-dev` / `renderer-specialist` writes code + Catch2 tests in one pass. `game-dev-tester` writes demo Lua (parallel if using existing bindings; sequential after if new bindings needed). **NO builds.**
3. **Expert Panel** — `performance-critic` + `security-auditor` (if attack surface) + `api-designer` — **ALL run in parallel simultaneously. Sequential dispatch is a process violation. NO builds.** PM pre-writes Phase 4+5 instructions in parallel with Phase 3 running.
4. **Remediation** — `engine-dev` fixes BLOCK/CRITICAL findings only. API structural fixes from `api-designer`. **NO builds. Skip if all PASS/MINOR ISSUES.**
5. **Build + Test** — `build-engineer` ONLY. FAST (Clang-18, default) or FULL (Clang-18 + GCC-13, end of phase). Rebuild once if first build fails.

**PARALLELISM RULE:** When a session has two or more independent change areas, spawn one reviewer instance per area — all running simultaneously. Two `performance-critic` instances reviewing independent code = mandatory, not optional.

**api-designer in Phase 3:** Reviews API AND updates `.context.md` in the SAME invocation. PM must not plan a separate doc-writing step.

---

## File Ownership

| Path | Owner |
|------|-------|
| `engine/core/` | `engine-dev` |
| `engine/renderer/` | `renderer-specialist` |
| `engine/scripting/` | `engine-dev` + `api-designer` |
| `engine/audio/`, `engine/physics/` | `engine-dev` |
| `examples/` | `game-dev-tester` |
| `tests/` | `engine-dev` |
| `docs/architecture/` | `architect` |
| `docs/project-state.md`, `docs/devlog.md`, `docs/ROADMAP.md` | `project-manager` |
| `docs/agents/`, `.claude/agents/` | `director` |
| `.context.md` files (all dirs) | `api-designer` |
| `README.md`, `website/` | `api-designer` |
| `docs/environment.md` | `system-engineer` |

---

## No-Build Rule

`engine-dev`, `renderer-specialist`, `api-designer`, `game-dev-tester`, `architect`, `security-auditor`, `performance-critic`: **NEVER run cmake / ninja / ctest.** `build-engineer` ONLY.

---

## Git Rule

**`project-manager` owns ALL `git add` / `git commit` / `git push`.** No other agent commits or pushes. PM pushes at session end by default — the user should not have to ask.

---

## Review-Only Agents

`performance-critic`, `security-auditor`: **read and report ONLY. Never fix code. Never commit.**

---

## api-designer Rule

In Phase 3: review API AND update `.context.md` in the **same invocation**. Never a separate step.

---

## Quick Q&A

| Question | Answer |
|----------|--------|
| Virtual in hot code? | No |
| Heap alloc per frame? | No — use arena allocators |
| Suppress warnings? | No — fix the code |
| RTTI in engine core? | No |
| Exceptions in engine core? | No |
| Who commits / pushes? | `project-manager` only |
| Who builds / tests? | `build-engineer` only |
| Default tier? | LEGACY |
| Env broken (missing header, linker)? | `system-engineer` before retrying anything |
| Unsure about architecture? | `architect` |
