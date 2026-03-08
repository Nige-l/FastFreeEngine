# Current Sprint: Post-Restructure Hardening

Sprint started after agent consolidation (10 → 5 agents). Focus: CI hardening, documentation accuracy, security verification, and repository polish.

---

## Priority 1 — CI/Build (Ops)

### 1. Add CMakePresets.json
- **Agent:** Ops
- **Status:** [x] DONE
- **Acceptance:**
  - Presets: `legacy-debug`, `legacy-release`, `standard-debug`, `modern-debug`, `mingw-release`
  - `cmake --preset legacy-debug && cmake --build --preset legacy-debug` works
  - Users no longer need to type compiler paths or tier flags manually

### 2. Add ASan/UBSan CI job
- **Agent:** Ops
- **Status:** [x] DONE (added to ci.yml, fixed ArenaAllocator aligned_alloc bug found by ASan)
- **Acceptance:**
  - New GitHub Actions workflow: build with `-fsanitize=address,undefined`, run full test suite
  - Workflow runs on every push to main, zero sanitizer errors on current codebase
  - If sanitizer errors are found, log them as issues and fix before moving on

### 3. Add clang-tidy CI job
- **Agent:** Ops
- **Status:** [x] DONE
- **Acceptance:**
  - Run clang-tidy-18 on all `engine/` source files in CI
  - Zero clang-tidy warnings (fix code or configure `.clang-tidy` to suppress intentional patterns)

### 4. Add code coverage reporting
- **Agent:** Ops
- **Status:** [x] DONE
- **Acceptance:**
  - Use llvm-cov or gcov, output summary in CI log
  - Coverage percentage visible in CI output per subsystem

### 5. Cut v0.1.0 release
- **Agent:** Ops
- **Status:** [ ] TODO
- **Acceptance:**
  - Tag `v0.1.0`, write changelog summarizing Phase 1-9 work, create GitHub Release with build instructions
  - GitHub Releases page has a proper entry

---

## Priority 2 — Documentation & Design (Architect)

### 6. Audit all .context.md files
- **Agent:** Architect
- **Status:** [x] DONE
- **Acceptance:**
  - Compare every `.context.md` against actual code API surface
  - Every documented function exists in code, every public function is documented
  - Discrepancies fixed

### 7. Create docs/dependency-policy.md
- **Agent:** Architect
- **Status:** [x] DONE
- **Acceptance:**
  - Document why `vcpkg` vs `third_party/` split exists, policy for new dependencies
  - A new contributor can read this and know where to put a new dependency

### 8. Write ADR: RETRO tier deprecation
- **Agent:** Architect
- **Status:** [x] DONE
- **Acceptance:**
  - ADR in `docs/architecture/` documenting maintenance cost vs audience benefit of OpenGL 2.1 support
  - Clear recommendation: deprecate or explicitly scope what RETRO means

### 9. Write honest subsystem status page
- **Agent:** Architect
- **Status:** [x] DONE
- **Acceptance:**
  - `docs/subsystem-status.md` rating each subsystem as demo-quality, tested, or production-ready
  - No subsystem rated higher than its actual test coverage and real-world usage justify

---

## Priority 3 — Technical Hardening (Implementer + Critic + Tester)

### 10. Add LuaJIT/Lua 5.4 compile-time switch
- **Agent:** Implementer (spec from Architect)
- **Status:** [ ] TODO
- **Acceptance:**
  - `FFE_LUA_BACKEND=LuaJIT|Lua54` cmake option
  - Lua 5.4 path: functional correctness, not perf parity
  - Full test suite passes with both backends
  - macOS arm64 can build with Lua 5.4

### 11. Verify zero-heap-allocation claims
- **Agent:** Critic
- **Status:** [x] DONE
- **Acceptance:**
  - Documented list of every hot path with confirmation of zero heap allocs
  - Corrected claims where violations found

### 12. Lua sandbox security audit
- **Agent:** Critic + Tester
- **Status:** [x] DONE
- **Acceptance:**
  - Adversarial Lua scripts attempting sandbox escape written and executed
  - No script can escape sandbox, exhaust memory beyond limits, or crash host

### 13. Networking edge case tests
- **Agent:** Tester
- **Status:** [x] DONE
- **Acceptance:**
  - Fuzz-style tests: malformed packets, oversized payloads, rapid connect/disconnect, NaN injection
  - All tests pass, no crashes or undefined behavior under adversarial input

---

## Priority 4 — Repository Polish (Ops)

### 14. Trim README.md
- **Agent:** Ops
- **Status:** [x] DONE
- **Acceptance:**
  - Keep: project identity (3 sentences), one screenshot, 15-line quick start, build instructions, link to docs
  - Move detailed content to MkDocs website
  - README fits on 2 screens

### 15. Add Windows CI (stretch goal)
- **Agent:** Ops
- **Status:** [ ] TODO
- **Acceptance:**
  - GitHub Actions job building on `windows-latest` with MSVC or MinGW
  - Windows build compiles and runs test suite
