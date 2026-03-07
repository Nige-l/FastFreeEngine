# Agent Team Changelog

## 2026-03-05 — Post-ADR-001 Team Review

**Reviewer:** director
**Trigger:** Completion of core engine skeleton (ADR-001), first session retrospective

### Current Roster (11 agents)

| Agent | Role |
|---|---|
| director | Team structure review, agent creation |
| project-manager | Session planning, devlog, task sequencing |
| architect | System design, ADR authorship |
| engine-dev | C++ implementation, owns engine/core, audio, physics, editor |
| renderer-specialist | Owns engine/renderer/ |
| test-engineer | Owns tests/, writes Catch2 tests and benchmarks |
| performance-critic | Read-only perf review, PASS/MINOR/BLOCK verdicts |
| security-auditor | Read-only security review, threat classification |
| api-designer | Lua bindings, .context.md files, public API review |
| game-dev-tester | Builds test games, end-user validation |
| system-engineer | Linux environment, toolchain, dependency management |

### Analysis

#### 1. Coverage vs CLAUDE.md Section 6 (File Ownership)

Every directory listed in the ownership table has a named agent:

- `engine/core/` -> engine-dev. Covered.
- `engine/renderer/` -> renderer-specialist. Covered.
- `engine/audio/` -> engine-dev. Covered.
- `engine/physics/` -> engine-dev. Covered.
- `engine/scripting/` -> engine-dev + api-designer. Covered.
- `engine/editor/` -> engine-dev. Covered.
- `tests/` -> test-engineer. Covered.
- `docs/architecture/` -> architect. Covered.
- `docs/agents/` -> director. Covered.
- `docs/devlog.md` -> project-manager. Covered.
- `docs/environment.md` -> system-engineer. Covered.
- `.claude/agents/` -> director. Covered.
- `.context.md` files -> api-designer. Covered.

Review-only agents (performance-critic, security-auditor, game-dev-tester) are all present.

**No gaps in file ownership coverage.**

#### 2. Readiness for Upcoming Work

Next phases in priority order:
1. Renderer RHI abstraction (OpenGL 3.3 for LEGACY tier)
2. Physics integration (Jolt)
3. Audio system
4. Scripting (Lua/sol2)
5. Networking (later)

Assessment per phase:

**Renderer RHI:** renderer-specialist is purpose-built for this. Their description covers OpenGL, Vulkan, the RHI abstraction layer, draw call batching, tier awareness, and shader code. Architect designs the interfaces, renderer-specialist implements. No gap.

**Physics (Jolt):** Falls to engine-dev, who owns engine/physics/. Jolt is a well-documented library with a clean C++ API. engine-dev is described as a pragmatic senior C++ engineer who has shipped engines. Integrating a third-party physics library is squarely within their skill set. Architect designs the wrapper/interface, engine-dev implements. No gap.

**Audio:** Same as physics -- engine-dev owns engine/audio/. Audio systems in games are typically simpler than rendering or physics. engine-dev can handle this. No gap.

**Scripting (Lua/sol2):** Owned jointly by engine-dev and api-designer. engine-dev writes the C++ binding layer, api-designer reviews the Lua-facing API. This is the system where api-designer and game-dev-tester add the most value. No gap.

**Networking:** This is the furthest out. When it arrives, security-auditor becomes critical (their description already covers network packet parsing, buffer overflows, replay attacks). engine-dev implements, security-auditor reviews. No gap in agent coverage, though networking is complex enough that we should evaluate again when it is closer.

**Verdict: The current team covers all upcoming phases.**

#### 3. Session 1 Workflow Retrospective

The flow was: architect (ADR-001) -> engine-dev (implementation) -> performance-critic + test-engineer (review).

What worked well:
- architect producing a complete, implementable ADR before engine-dev started
- performance-critic catching the strlen-per-tick issue
- test-engineer identifying future test gaps

What was NOT invoked but should have been considered:
- **api-designer** was not invoked to review the .context.md placeholder files or the public API surface of engine/core/. The Definition of Done (section 8) requires api-designer review and .context.md files. However, for the core skeleton these are legitimately placeholder -- there are no Lua bindings yet and the API is internal. Deferring api-designer was reasonable for session 1.
- **security-auditor** was not invoked. For the core skeleton (no file I/O, no networking, no external input), there is no meaningful attack surface to review. Correct to skip.
- **game-dev-tester** was not invoked. No Lua layer exists yet, so there is nothing for them to test from a game developer perspective. Correct to skip.
- **system-engineer** was not needed because the environment worked. Correct.

**Nothing fell through the cracks in session 1.** The agents that were skipped had no meaningful work to do yet. When the scripting layer arrives, api-designer and game-dev-tester become mandatory in the pipeline.

#### 4. New Agents Considered and Rejected

- **"shader-engineer"**: Could specialize in GLSL/SPIR-V. Rejected -- renderer-specialist already covers shader code explicitly ("You write shader code as carefully as you write C++"). No need to split this out.

- **"networking-specialist"**: Could own engine/networking/ the way renderer-specialist owns engine/renderer/. Rejected for now -- networking is several phases away. When we get there, engine-dev handles the implementation and security-auditor handles the review. If networking proves complex enough to warrant a specialist (netcode prediction, rollback, authoritative server architecture), we can revisit. Creating the agent now would be premature.

- **"build-engineer" / "ci-engineer"**: Could own CMake and CI pipelines. Rejected -- system-engineer covers the environment and build toolchain. engine-dev owns the CMakeLists.txt files. The current split works.

- **"documentation-writer"**: Could own README, guides, tutorials. Rejected -- api-designer owns .context.md files (the primary docs). A separate docs agent is not justified until there is a documentation website or tutorial system.

**Decision: No new agents created.**

#### 5. Role Refinements

No changes made. All agent descriptions are accurate, well-scoped, and aligned with their responsibilities.

**Watch item for the future:** engine-dev currently owns four directories (core, audio, physics, editor). As those systems grow, engine-dev's scope could become too broad. Monitor after physics and audio are implemented. If engine-dev sessions start exceeding reasonable scope, consider splitting out a specialist (most likely for editor, which has the most distinct concerns).

### Summary

| Question | Answer |
|---|---|
| Coverage gaps? | None |
| Ready for next phases? | Yes |
| Session 1 workflow issues? | None |
| New agents needed? | No |
| Role refinements needed? | No |
| Watch items | engine-dev scope breadth as systems grow; networking specialist decision when networking phase arrives |

---

## 2026-03-05 — Post-Sessions 2 & 3 Review: Renderer Implementation Retrospective

**Reviewer:** director
**Trigger:** Renderer went from ADR-002 design through full implementation, two review cycles (performance-critic BLOCK then PASS, security-auditor HIGH then cleared), plus example programs. This was the largest push so far — 35 new files, ~1900 lines of renderer code, 10 new tests, 2 example programs.

### 1. Agent Pipeline Performance

#### What went right

**The review-fix-re-review cycle worked exactly as designed.** Performance-critic issued a BLOCK with specific, actionable findings (use-after-free in render queue arena lifetime, sprite batch not connected to actual rendering). Engine-dev fixed them. Performance-critic re-reviewed and returned PASS. Same pattern with security-auditor: 2 HIGH findings (integer overflow in VRAM calc, missing texture dimension validation), fixes applied, re-review cleared the merge. This is the pipeline doing its job — catching real bugs before they ship.

**Architect produced an implementable spec.** ADR-002 was detailed enough that renderer-specialist could build from it without clarifying questions, same standard as ADR-001. The design-then-implement pattern continues to work well.

**Session 2 parallelism was well-structured.** Four agents (architect, api-designer, engine-dev, test-engineer) ran in parallel on independent tasks, then sequenced into performance-critic re-review. No wasted time, no conflicts.

#### What went wrong or could improve

**Performance-critic found a use-after-free.** This is a memory safety bug, not a performance bug. Performance-critic caught it because it was reading the code carefully, but it is really security-auditor's domain. The fact that it was caught is good — the fact that it was caught by the wrong agent is a process smell. It worked out this time, but relying on performance-critic to catch safety bugs is not a reliable pattern.

**Security-auditor ran after implementation was complete.** Both HIGH findings (integer overflow, missing validation) were in fundamental resource creation paths. If security-auditor had reviewed the ADR-002 design or been consulted during implementation, these patterns could have been prevented rather than found-and-fixed. The current flow is: implement -> review -> fix -> re-review. A shift-left to: design -> security review of design -> implement with constraints -> final security review would catch classes of bugs earlier and reduce rework.

**Engine-dev wrote the examples, not game-dev-tester.** The Definition of Done in CLAUDE.md Section 8 says "game-dev-tester has built something with it and reported no blockers." The hello_sprites and headless_test examples were written by engine-dev. These are technically competent demos, but they test from an engine developer's perspective, not a game developer's perspective. Game-dev-tester was never invoked in Session 3. This is a gap.

**Api-designer was not invoked in Session 3.** The renderer now has a public API surface (sprite components, camera setup, shader library). The Definition of Done requires api-designer review and .context.md files. The .context.md for the renderer exists but was not reviewed by api-designer. The devlog correctly notes this as a next-session item, but it means Session 3 did not fully satisfy the DoD.

### 2. Coverage and Ownership

#### Renderer maintenance vs new features

The renderer is ~1900 lines across 18 files. Renderer-specialist owns `engine/renderer/` — that is clear. But Session 3 showed that engine-dev did the bug fixes after performance-critic and security-auditor flagged issues. This is the correct pattern per CLAUDE.md (review-only agents report, they do not fix). However, it means renderer bug fixes are split: renderer-specialist implements, engine-dev fixes issues found in review. This works if the bugs are straightforward (integer overflow, missing validation). It could become a problem if fixes require deep graphics knowledge (e.g., GPU synchronization bugs, driver-specific workarounds).

**Recommendation:** Keep current ownership. Renderer-specialist implements, engine-dev handles fixes that are general C++ (safety, validation). If a fix requires graphics-specific knowledge, renderer-specialist should handle it. This is already implied by the ownership model but worth stating explicitly.

#### Known issues backlog

Session 3 left several tracked issues: updateBuffer integer overflow (M-7), uniform cache hash collisions (M-1), sort key 12-bit truncation (M-3), dead code in submitRenderQueue(), and the requestShutdown design gap from Session 1. These are all documented in the devlog but there is no formal tracking beyond the devlog. For now this is fine — the list is short. If it grows past 10 items, we need a dedicated issue tracker or a `docs/known_issues.md` file.

### 3. Team Composition

#### Current roster assessment

The 11-agent roster remains correct for the work completed. No agent was idle without reason, and no work fell outside an agent's defined scope (with the caveats about game-dev-tester and api-designer noted above).

#### Upcoming phases

The devlog Session 3 "next session" items are:
1. game-dev-tester builds something with the sprite system
2. api-designer reviews public API and writes .context.md
3. Lua scripting layer (sol2 integration)
4. Input handling (keyboard/mouse via GLFW)
5. Run hello_sprites on a real display

For Lua scripting: engine-dev + api-designer jointly own `engine/scripting/`. This is well-covered. Security-auditor must review the sandbox — their description explicitly covers Lua sandbox escapes. No new agent needed.

For input handling: This is GLFW event polling, which is straightforward. Engine-dev can handle it. No new agent needed.

For audio (future): Engine-dev owns `engine/audio/`. No new agent needed.

**Verdict: No new agents needed. No agents to remove.**

#### Agent description refinements

No changes needed to any agent descriptions. They accurately reflect what each agent did in Sessions 2-3 and what they will need to do going forward.

### 4. Process Improvement Recommendations

#### Recommendation A: Shift security review left (ADOPT)

**Current:** implement -> security-auditor reviews -> fix -> re-review.
**Proposed:** architect produces ADR -> security-auditor reviews ADR for attack surface patterns -> implement with security constraints baked in -> security-auditor final review of implementation.

The cost is one additional security-auditor invocation per feature (reviewing the ADR). The benefit is catching integer overflow patterns, missing validation patterns, and unsafe memory lifetime patterns at design time rather than finding them in 1900 lines of completed code. The Session 3 security findings (integer overflow in size calc, missing dimension validation) are exactly the kind of thing that a design-phase review would catch: "every resource creation function must validate inputs and use u64 for size calculations."

This does NOT mean security-auditor reviews every ADR. Only ADRs that touch the attack surfaces listed in CLAUDE.md Section 5: networking, file I/O, asset loading, scripting, or external input. The renderer qualifies because it loads textures (asset loading) and processes external data (texture dimensions, buffer sizes).

#### Recommendation B: game-dev-tester must verify before a feature is "done" (ENFORCE)

This is already in the Definition of Done (CLAUDE.md Section 8) but was skipped in Session 3. The devlog acknowledges this as a next-session item, which is honest, but it means the renderer shipped without end-user validation. Going forward, project-manager should include game-dev-tester in the session plan for any feature that adds user-facing API surface. The examples should be written by game-dev-tester, not engine-dev — or at minimum, game-dev-tester should attempt to use the feature independently and report.

#### Recommendation C: api-designer reviews before examples are written (SEQUENCE)

The natural flow should be: implement -> api-designer reviews API -> game-dev-tester tries to use it -> examples are the artifact of game-dev-tester's usage. This gives the best signal on API quality. In Session 3, examples were written by the implementer, which tests "does my code work" but not "is my API discoverable."

#### Recommendation D: No formal integration test gate yet (DEFER)

A formal "integration test" step where game-dev-tester verifies before commit was considered. This is premature — the engine does not yet have Lua scripting, which is game-dev-tester's primary interface. Once Lua bindings exist, game-dev-tester writing a Lua game becomes a meaningful integration test. Until then, the headless C++ examples serve as adequate smoke tests. Revisit when Lua scripting ships.

### 5. Summary

| Question | Answer |
|---|---|
| Did the right agents get invoked? | Mostly. Performance-critic and security-auditor were correctly invoked. Game-dev-tester and api-designer were incorrectly skipped. |
| Review-fix-re-review cycle? | Worked well. Both blockers were caught, fixed, and verified. |
| Coverage gaps? | game-dev-tester validation missing; api-designer review of renderer API deferred |
| New agents needed? | No |
| Agent description changes? | None |
| Process changes? | (A) Shift security review to include ADR phase for attack-surface features. (B) Enforce game-dev-tester verification in session plans. (C) Sequence api-designer review before examples. (D) Defer formal integration test gate until Lua ships. |
| Watch items carried forward | engine-dev scope breadth (from last review); known issues list growth; renderer fix ownership for graphics-specific bugs |

---

## 2026-03-07 — Session 42 Pre-Session: Process Fixes (Director Review)

**Reviewer:** director
**Trigger:** User raised process concerns after 41 sessions — devlog bloat, unclear git commit ownership, system-engineer scope creep

### Changes Made

#### 1. Devlog Restructuring

**Problem:** `docs/devlog.md` was 2877 lines across 41 sessions. Agents reading the entire file each session was wasteful and slow.

**Solution:** Three-part structure:
- `docs/project-state.md` (NEW) — concise living document (<100 lines) with current project status, subsystem table, recent session summaries, and next-session plan. PM updates this each session. This is the primary context document agents read.
- `docs/devlog.md` — trimmed to Sessions 35-41 (~694 lines). Header points to project-state.md and archive.
- `docs/devlog-archive.md` (NEW) — Sessions 1-34 (~2192 lines). Historical reference only.

PM is instructed to archive old sessions when devlog exceeds 1000 lines.

**Files changed:** `docs/project-state.md` (new), `docs/devlog.md` (rewritten), `docs/devlog-archive.md` (new)

#### 2. Git Commit Ownership Defined

**Problem:** CLAUDE.md Section 7 said "After Phase 5 passes: commit" but never specified which agent. system-engineer had been doing `git commit` and `git push` in recent sessions, which is outside its defined scope.

**Decision:** `project-manager` owns all git operations (`git add`, `git commit`, `git push`). Rationale:
- PM already declares when a session is complete
- PM writes the devlog entry (which is part of the commit)
- PM writes the commit message content (conventional commits)
- Concentrating commit authority in one agent prevents the same change being committed by different agents in different sessions

**Files changed:**
- `.claude/CLAUDE.md` Section 7: new "Git Commit Ownership" subsection with explicit table
- `.claude/CLAUDE.md` Quick Reference: added "Who does git commits?" entry
- `.claude/agents/project-manager.md`: added `Bash` tool, "Git Commit Ownership" section, updated session closeout to include git commit step
- `.claude/agents/engine-dev.md`: clarified that engine-dev does not run git commit
- `.claude/agents/system-engineer.md`: explicit "You do NOT: run git commit" in scope boundaries

#### 3. system-engineer Scope Clarified

**Problem:** system-engineer's scope was ambiguous about builds. While the agent file didn't instruct full builds, it also didn't prohibit them. system-engineer had been running full builds in some sessions.

**Decision:** system-engineer may run diagnostic commands (cmake configure, pkg-config, minimal compile tests) but NOT full project builds or test suites. Added explicit "Scope Boundaries" section with DO/DO NOT lists.

**Files changed:** `.claude/agents/system-engineer.md`: replaced generic "primary directive" with explicit scope boundaries section

#### 4. Stale References Fixed

- `.claude/agents/project-manager.md`: "3-phase flow" updated to "5-phase flow"
- `.claude/agents/project-manager.md`: context reading updated to reference `docs/project-state.md` first, devlog second
- `.claude/agents/engine-dev.md`: removed contradictory "After implementing anything you run the build" (conflicts with "Write Everything, Never Build" section below it)
- `.claude/CLAUDE.md` Section 0: PM context reading references updated to include `docs/project-state.md`
- `.claude/CLAUDE.md` Session Lifecycle step 9: now includes "updates `docs/project-state.md`"
- `.claude/CLAUDE.md` Section 6: `docs/project-state.md` and `docs/devlog-archive.md` added to file ownership table

### Summary

| Change | Rationale |
|--------|-----------|
| `docs/project-state.md` | Agents get full context in <100 lines instead of reading 2877-line devlog |
| Devlog split (current + archive) | Main devlog stays manageable; history preserved |
| PM owns git commits | Single agent with commit authority prevents confusion and scope creep |
| PM gets Bash tool | Needed to run git commands |
| system-engineer scope boundaries | Explicit DO/DO NOT prevents builds and commits outside its role |
| engine-dev contradiction removed | "run the build" line conflicted with "Write Everything, Never Build" |
| 3-phase -> 5-phase in PM file | Stale reference from before Session 34 process change |

### Watch Items

- Monitor whether 1000-line devlog threshold triggers archiving too often or not often enough
- PM now has Bash tool — monitor that PM only uses it for git commands, not for builds or code execution
- project-state.md must stay under 100 lines — PM must be disciplined about this
