# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 70 |
| Total tests | 1005 (FAST build: Clang-18, zero warnings) |
| Total Lua bindings | ~169 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | MVP COMPLETE (6 milestones, Sessions 51-56) |
| Phase 4 (Networking) | COMPLETE (Sessions 57-60) |
| Phase 5 (Website/Learning) | COMPLETE (Sessions 62-65) |
| Phase 6 (Showcase Game) | IN PROGRESS (Sessions 66-present) |
| Windows build | DONE (MinGW-w64 cross-compilation) |
| macOS build | DISABLED (upstream LuaJIT arm64-osx vcpkg issue) |
| GitHub Actions CI | DONE (Linux Clang-18, Linux GCC-13) |

## Demo Games

1. **Collect Stars** -- top-down 2D, Lua-only
2. **Pong** -- classic 2D, Lua-only
3. **Breakout** -- brick-breaker 2D, Lua-only
4. **3D Demo** -- mesh loading, Blinn-Phong, point lights, materials, shadows, skybox
5. **Net Arena** -- 2D multiplayer arena, client-side prediction, server reconciliation
6. **Echoes of the Ancients** -- 3D showcase (IN PROGRESS: Level 1+2 complete, Level 3 next)

## Known Issues / Deferred Items

- macOS CI disabled (LuaJIT arm64-osx upstream vcpkg issue, user approved)
- UNC path (`\\server\share`) blocking on Windows -- comment-only, no test env
- MSVC native build support -- deferred (MinGW cross-compile works)
- set3DCameraFPS lacks NaN/Inf guards (MINOR -- backlog)
- NAT traversal / relay server -- deferred to backlog

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 70 | Phase 6 M3 (part 2): Real CC0 3D models (7 .glb), Suno music integration, Level 2 gameplay (crystal puzzle, timed bridges, boss guardian, portal victory), GitHub Pages fix (deleted Jekyll workflow), macOS CI disabled. README update. 1005 tests (FAST). |
| 69 | Phase 6 M3 (part 1): Level 2 "The Temple" (underground, dark lighting, lava pit, crystal pedestals, 2 purple guardians, artifact), macOS CI fix (always-on vcpkg overlay). 1005 tests (FAST). |
| 68 | Phase 6 M2: Level 1 "The Courtyard" (push-block puzzle, 2 guardians, destructible wall, artifact, fog+shadows+4 lights), macOS CI fix (conditional overlay ports). 1005 tests (FAST). |
| 67 | Phase 6 M1: linear fog shader (ffe.setFog/disableFog, 14 tests), "Echoes of the Ancients" scaffold (player, camera, HUD, combat, AI, test level). 1005 tests (FAST). |
| 66 | Editor crash fix (ImGuiKey migration), macOS CI fix (LuaJIT arm64), README overhaul, Phase 6 ADR ("Echoes of the Ancients"). 991 tests (FAST). |

## Phase 6 — Showcase Game: "Echoes of the Ancients" (IN PROGRESS)

**Goal:** A multi-level 3D showcase game proving FFE can ship real, playable games. ADR: `docs/architecture/adr-phase6-showcase.md`

### Progress
- [x] M1 (Session 67): Linear fog shader + project scaffold + player controller
- [x] M2 (Session 68): Level 1 -- The Courtyard (outdoor, puzzles, guardians, artifacts)
- [x] M3 (Sessions 69-70): Level 2 -- The Temple (underground, dark lighting, crystal puzzle, timed bridges, boss guardian, real 3D models, music)
- [ ] M4 (Sessions 71-72): Level 3 -- The Summit (floating platforms, dramatic skybox, victory)
- [ ] M5 (Sessions 73-74): Polish, main menu, screenshots, gamepad pass, README screenshots
- [ ] Optional M6: Stretch goals (skeletal anim, minimap, time-of-day)

### Planned Future Phases (beyond Phase 6)
- Phase 7: Vulkan renderer (STANDARD/MODERN tiers)
- Phase 8: Terrain system + open-world support
- Phase 9: Advanced editor (visual scripting, prefabs, LLM panel)
- Phase 10: Cross-platform native builds (MSVC, Xcode, AppImage)
- Phase 11: Asset pipeline + plugin system
- Phase 12: Advanced rendering (PBR, post-processing, instancing)

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
