# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 81 |
| Total tests | 1168 (FAST build: Clang-18, zero warnings) |
| Total Lua bindings | ~184 |
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
| 81 | Phase 7 M5: Skeletal Animation Completion — crossfade blending, STEP/LINEAR/CUBIC_SPLINE interpolation modes, root motion extraction. 3 Lua bindings, 31 new tests. 1168 tests (FAST). |
| 80 | Phase 7 M4: Anti-Aliasing — MSAA (multisample FBO, configurable 2x/4x/8x, glBlitFramebuffer resolve) + FXAA 3.11 post-process. 2 Lua bindings, 22 new tests. 1137 tests (FAST). |
| 79 | Phase 7 M3: GPU Instancing — automatic batching by MeshHandle (2+ non-skinned), glDrawElementsInstanced, 1024/batch, instanced shadow pass, ffe.getInstanceCount. 21 new tests. 1115 tests (FAST). |
| 78 | Phase 7 M2: Post-processing pipeline — HDR FBO, bloom (half-res ping-pong), tone mapping (Reinhard/ACES), gamma correction. 6 Lua bindings, 42 new tests. 1094 tests (FAST). |
| 77 | Showcase debug: inverted WASD (forward vector +sin/+cos fixed to -sin/-cos), ground Y-scale doubled + brighter colors, player scale 0.5->1.8, HUD text shortened for overflow. game-dev-tester SHIP 9/10. 1052 tests (FAST). |

## Phase 7 — Rendering Pipeline Modernisation (IN PROGRESS)

**Goal:** Bring FFE's visual output to competitive parity with Godot 4's forward renderer. ADR: `docs/architecture/adr-phase7-rendering-pipeline.md`

### Milestones

- [x] M1 (Sessions 74-76): PBR Materials -- PBRMaterial component, Cook-Torrance BRDF shader, IBL, Lua bindings, tests. Sessions 75-76: showcase bug fixes, engine framebuffer resize fix, process reform.
- [x] M2 (Sessions 77-78): Post-Processing -- HDR FBO (GL_RGBA16F), bloom (half-res ping-pong Gaussian), tone mapping (Reinhard/ACES), gamma correction. 6 Lua bindings, 42 tests.
- [x] M3 (Session 79): GPU Instancing -- InstanceData (64B), MAX_INSTANCES_PER_BATCH=1024, shared instance VBO (64KB GL_STREAM_DRAW), 3 instanced shader variants, instanced shadow pass, ffe.getInstanceCount, 21 tests.
- [x] M4 (Session 80): Anti-Aliasing -- MSAA (multisample HDR FBO, configurable 2x/4x/8x, glBlitFramebuffer resolve) + FXAA 3.11 (Timothy Lottes, post-process edge detection + sub-pixel AA). 2 Lua bindings, 22 tests.
- [x] M5 (Session 81): Skeletal Animation Completion -- crossfade blending (per-bone lerp/slerp), STEP/LINEAR/CUBIC_SPLINE interpolation modes (glTF sampler parsing), root motion extraction (XZ delta, RootMotionDelta component). 3 Lua bindings, 31 tests.
- [ ] M6 (Session 82): SSAO (STANDARD+ tier only)
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
