# Current Sprint: Post-Restructure Hardening

Focus: CI hardening, documentation accuracy, security verification, and repository polish.

---

## Priority 1 -- CI/Build

### 1. Add CMakePresets.json
- **Status:** DONE
- **Result:** Presets `legacy-debug`, `legacy-release`, `standard-debug`, `modern-debug`, `mingw-release`. Users no longer need to type compiler paths or tier flags manually.

### 2. Add ASan/UBSan CI job
- **Status:** DONE
- **Result:** GitHub Actions workflow running with `-fsanitize=address,undefined`. Zero sanitizer errors on current codebase. Fixed one `ArenaAllocator` `aligned_alloc` bug discovered by ASan.

### 3. Add clang-tidy CI job
- **Status:** DONE
- **Result:** clang-tidy-18 runs on all `engine/` source files in CI. Zero warnings.

### 4. Add code coverage reporting
- **Status:** DONE
- **Result:** llvm-cov summary in CI log, coverage percentage visible per subsystem.

### 5. Cut v0.1.0 release
- **Status:** TODO
- **Acceptance:** Tag `v0.1.0`, write changelog summarizing Phase 1-9 work, create GitHub Release with build instructions.

---

## Priority 2 -- Documentation and Design

### 6. Audit all .context.md files
- **Status:** DONE
- **Result:** Every `.context.md` compared against actual code API surface. Discrepancies fixed.

### 7. Create dependency policy
- **Status:** DONE
- **Result:** [Dependency Policy](dependency-policy.md) documents the `vcpkg` vs `third_party/` split and policy for new dependencies.

### 8. Write ADR: RETRO tier deprecation
- **Status:** DONE
- **Result:** [ADR: RETRO Tier Deprecation](adr-retro-tier.md) documents the decision to formally deprecate OpenGL 2.1 support.

### 9. Write honest subsystem status page
- **Status:** DONE
- **Result:** [Subsystem Status](subsystem-status.md) rates each subsystem as demo-quality, tested, or production-ready based on actual coverage.

---

## Priority 3 -- Technical Hardening

### 10. Add LuaJIT/Lua 5.4 compile-time switch
- **Status:** TODO
- **Acceptance:** `FFE_LUA_BACKEND=LuaJIT|Lua54` cmake option. Full test suite passes with both backends. macOS arm64 can build with Lua 5.4.

### 11. Verify zero-heap-allocation claims
- **Status:** DONE
- **Result:** Documented list of every hot path with confirmation of zero heap allocs. Corrected claims where violations found.

### 12. Lua sandbox security audit
- **Status:** DONE
- **Result:** Adversarial Lua scripts attempting sandbox escape written and executed. No escape, no memory exhaustion, no crashes.

### 13. Networking edge case tests
- **Status:** DONE
- **Result:** Fuzz-style tests covering malformed packets, oversized payloads, rapid connect/disconnect, NaN injection. All pass.

---

## Priority 4 -- Repository Polish

### 14. Trim README.md
- **Status:** DONE
- **Result:** README trimmed to project identity, one screenshot, quick start, build instructions, and link to docs.

### 15. Add Windows CI
- **Status:** TODO
- **Acceptance:** GitHub Actions job building on `windows-latest` with MSVC or MinGW. Windows build compiles and runs test suite.
