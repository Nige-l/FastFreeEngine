# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 73 |
| Total tests | 1005 (FAST build: Clang-18, zero warnings) |
| Total Lua bindings | ~169 |
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

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 73 | Phase 6 COMPLETE. Phase 7 ADR approved (PBR, post-processing, instancing, skeletal anim, AA, SSAO). README + ROADMAP updated. No C++ changes. |
| 72 | Phase 6 M4b: Polish pass -- main menu, pause menu, victory particles/rank, gamepad dead-zones, dynamic HUD labels. 1005 tests (FAST). |
| 71 | Phase 6 M4: Level 3 "The Summit" (floating platforms, moving platforms, sunset lighting, 4 guardians + boss, victory sequence with stats). 1005 tests (FAST). |
| 70 | Phase 6 M3 (part 2): Real CC0 3D models (7 .glb), Suno music integration, Level 2 gameplay, GitHub Pages fix, macOS CI disabled. 1005 tests (FAST). |
| 69 | Phase 6 M3 (part 1): Level 2 "The Temple" (underground, dark lighting, crystal puzzle, boss guardian, artifact). 1005 tests (FAST). |

## Phase 7 — Rendering Pipeline Modernisation (IN PROGRESS)

**Goal:** Bring FFE's visual output to competitive parity with Godot 4's forward renderer. ADR: `docs/architecture/adr-phase7-rendering-pipeline.md`

### Milestones

- [ ] M1 (Sessions 74-75): PBR Materials -- PBRMaterial component, Cook-Torrance BRDF shader, IBL, Lua bindings, tests
- [ ] M2 (Sessions 76-77): Post-Processing -- HDR FBO chain, bloom, tone mapping, gamma correction
- [ ] M3 (Sessions 78-79): GPU Instancing -- instance buffers, automatic batching, 1000-instance benchmark
- [ ] M4 (Sessions 80-81): Skeletal Animation Completion -- crossfade blending, interpolation modes, root motion
- [ ] M5 (Session 82): Anti-Aliasing -- MSAA + FXAA
- [ ] M6 (Session 83): SSAO (STANDARD+ tier only)
- [ ] M7 (Session 84): Sprite Batching 2.0 -- texture array batching
- [ ] M8 (Session 85): Phase Close -- FULL build, profiling, documentation sweep

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
