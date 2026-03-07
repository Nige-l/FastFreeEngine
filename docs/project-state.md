# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 77 |
| Total tests | 1052 (FAST build: Clang-18, zero warnings) |
| Total Lua bindings | ~172 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | MVP COMPLETE (6 milestones, Sessions 51-56) |
| Phase 4 (Networking) | COMPLETE (Sessions 57-60) |
| Phase 5 (Website/Learning) | COMPLETE (Sessions 62-65) |
| Phase 6 (Showcase Game) | COMPLETE (Sessions 66-73) |
| Phase 7 (Rendering Pipeline) | IN PROGRESS (Session 74+) |
| Windows build | DONE (MinGW-w64 cross-compilation) |
| macOS build | DISABLED (upstream LuaJIT arm64-osx vcpkg issue) |
| GitHub Actions CI | DONE (Linux Clang-18, Linux GCC-13) |

## Demo Games

1. **Collect Stars** -- top-down 2D, Lua-only
2. **Pong** -- classic 2D, Lua-only
3. **Breakout** -- brick-breaker 2D, Lua-only
4. **3D Demo** -- mesh loading, Blinn-Phong, point lights, materials, shadows, skybox
5. **Net Arena** -- 2D multiplayer arena, client-side prediction, server reconciliation
6. **Echoes of the Ancients** -- 3-level 3D showcase game (COMPLETE: menus, puzzles, combat, victory)

## Known Issues / Deferred Items

- macOS CI disabled (LuaJIT arm64-osx upstream vcpkg issue, user approved)
- UNC path (`\\server\share`) blocking on Windows -- comment-only, no test env
- MSVC native build support -- deferred (MinGW cross-compile works)
- set3DCameraFPS lacks NaN/Inf guards (MINOR -- backlog)
- NAT traversal / relay server -- deferred to backlog
- Installer / easy setup wizard -- user should install, connect AI model, start making games without build complexity (user request, Session 75)

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 77 | Showcase debug: inverted WASD (forward vector +sin/+cos fixed to -sin/-cos), ground Y-scale doubled + brighter colors, player scale 0.5->1.8, HUD text shortened for overflow. game-dev-tester SHIP 9/10. 1052 tests (FAST). |
| 76 | Engine fix: framebuffer resize callback (stale screen dimensions), showcase halfExtents 2x mismatch fix, BattleMusic, director process reform. game-dev-tester SHIP 8/10. 1052 tests (FAST). |
| 75 | Showcase demo 7 bugs fixed, 3 new Lua bindings (getMouseDeltaX/Y, setCursorCaptured), game-dev-tester process reformed (now mandatory for demo changes), director review. 1052 tests (FAST). |
| 74 | Phase 7 M1: PBR Materials (Cook-Torrance BRDF, metallic-roughness, IBL via skybox cubemap), fog system, 41 new tests. 1046 tests (FAST). |
| 73 | Phase 6 COMPLETE. Phase 7 ADR approved (PBR, post-processing, instancing, skeletal anim, AA, SSAO). README + ROADMAP updated. No C++ changes. |

## Phase 7 — Rendering Pipeline Modernisation (IN PROGRESS)

**Goal:** Bring FFE's visual output to competitive parity with Godot 4's forward renderer. ADR: `docs/architecture/adr-phase7-rendering-pipeline.md`

### Milestones

- [x] M1 (Sessions 74-76): PBR Materials -- PBRMaterial component, Cook-Torrance BRDF shader, IBL, Lua bindings, tests. Sessions 75-76: showcase bug fixes, engine framebuffer resize fix, process reform.
- [ ] M2 (Sessions 77-78): Post-Processing -- HDR FBO chain, bloom, tone mapping, gamma correction
- [ ] M3 (Sessions 79-80): GPU Instancing -- instance buffers, automatic batching, 1000-instance benchmark
- [ ] M4 (Sessions 81-82): Skeletal Animation Completion -- crossfade blending, interpolation modes, root motion
- [ ] M5 (Session 83): Anti-Aliasing -- MSAA + FXAA
- [ ] M6 (Session 84): SSAO (STANDARD+ tier only)
- [ ] M7 (Session 85): Sprite Batching 2.0 -- texture array batching
- [ ] M8 (Session 86): Phase Close -- FULL build, profiling, documentation sweep

## Build Commands

```bash
# Clang-18 (FAST -- default)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build && ctest --test-dir build --output-on-failure --parallel $(nproc)

# GCC-13 (FULL builds only)
cmake -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build-gcc && ctest --test-dir build-gcc --output-on-failure --parallel $(nproc)

# Windows (MinGW cross-compile from Linux)
cmake -B build-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build-mingw
```

## Key File Locations

| What | Where |
|------|-------|
| Constitution | `.claude/CLAUDE.md` |
| Roadmap (archival) | `docs/ROADMAP.md` (full phases -- read only for phase transitions) |
| Devlog (recent) | `docs/devlog.md` (Sessions 51+) |
| Devlog (archive) | `docs/devlog-archive.md` (Sessions 1-50) |
| Project state | `docs/project-state.md` (this file) |
| Architecture map | `docs/architecture-map.md` (subsystems, files, Lua bindings, ECS components) |
| Environment log | `docs/environment.md` |
| Agent definitions | `.claude/agents/*.md` |
