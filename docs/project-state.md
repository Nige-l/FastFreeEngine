# FastFreeEngine — Project State

> Quick-context document for current project state. Under 100 lines. See `docs/current-sprint.md` for active tasks.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 113 |
| Total tests | 1579 |
| Total Lua bindings | ~225 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | MVP COMPLETE (6 milestones, Sessions 51-56) |
| Phase 4 (Networking) | COMPLETE (Sessions 57-60) |
| Phase 5 (Website/Learning) | COMPLETE (Sessions 62-65) |
| Phase 6 (Showcase Game) | COMPLETE (Sessions 66-73) |
| Phase 7 (Rendering Pipeline) | COMPLETE (Sessions 74-84) |
| Phase 8 (Vulkan Backend) | COMPLETE (Sessions 85-89) |
| Phase 9 (Terrain/Open World) | COMPLETE (Sessions 90-92, 108-110) |
| Phase 10 (Advanced Editor) | IN PROGRESS — M1-M3 COMPLETE, M4 next |
| Windows build | DONE (MinGW-w64 cross-compilation) |
| macOS build | DISABLED (upstream LuaJIT arm64-osx vcpkg issue) |
| GitHub Actions CI | DONE (Linux Clang-18, Linux GCC-13) |

## Demo Games

1. **Collect Stars** -- top-down 2D, Lua-only
2. **Pong** -- classic 2D, Lua-only
3. **Breakout** -- brick-breaker 2D, Lua-only
4. **3D Demo** -- mesh loading, Blinn-Phong, point lights, materials, shadows, skybox
5. **Net Arena** -- 2D multiplayer arena, client-side prediction, server reconciliation
6. **Echoes of the Ancients** -- 3-level 3D showcase game (terrain, post-processing, vegetation, water)

## Known Issues / Deferred Items

- macOS CI disabled (LuaJIT arm64-osx upstream vcpkg issue, user approved)
- UNC path (`\\server\share`) blocking on Windows -- comment-only, no test env
- MSVC native build support -- deferred (MinGW cross-compile works)
- set3DCameraFPS lacks NaN/Inf guards (MINOR -- backlog)
- NAT traversal / relay server -- deferred to backlog
- Installer / easy setup wizard -- user request (Session 75), backlog
- **Procedural Music Generation** -- algorithmic music system, no VST dependency (Phase 13)
- **3D particle system** -- showcase uses geometry workaround; proper system needed
- Water surface (2x2m fountain basin) not visible in Level 1 screenshot -- cosmetic, low priority (new screenshot captured Session 112)
- **Mesh path-to-handle gap** -- `findHandleByPath` missing from MeshLoader; prefab Mesh components need manual resolve after instantiation

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 114 | Docs audit + skeletal animation tests -- architecture-map.md updated (agent names → Architect/Implementer/Critic/Tester/Ops, InstanceData 80B, binding counts fixed), current-sprint.md tasks 3/4/6/8/9/11/12/13/14 marked DONE, ROADMAP.md Phase 10 M2/M3 marked COMPLETE, project-state.md updated. Skeletal animation: animation_system.h/cpp + skeleton.h (new), test_skeleton.cpp (42 tests), test_animation3d_bindings.cpp (26 tests), adr-skeletal-animation.md. 1579 tests total. |
| 113 | Phase 10 M3 LLM Integration Panel -- LlmPanel (async HTTP via vendored cpp-httplib, context-aware queries using .context.md files, Lua snippet insertion), 2 Lua bindings (ffe.llmQuery, ffe.isLLMConfigured, #ifdef FFE_EDITOR guarded), 15 tests (1511 total, Clang-18 FAST). Security: 2 HIGH findings in shift-left ADR review resolved (https:// enforcement, 32KB response cap, API key scrubbing, path traversal prevention). Build fix cycle: try/catch → error-code refactor + path traversal security gap in tests. ADR 645+ lines (Rev 2). 1 commit (66fed59). |
| 112 | Phase 10 M2 Visual Scripting -- VisualScriptingSystem (11 built-in nodes, topological sort, function-pointer dispatch, 14-step security pipeline), GraphEditorPanel (ImGui node canvas, pan/zoom, bezier connections, RMB add menu), 3 Lua bindings (loadGraph, attachGraph, detachGraph), 42 tests (1493 total, Clang-18 FAST). Build fix cycle: macro/braced-init + field name. Warning cleanup: terrain.cpp + vegetation.h. ADR 918 lines. 1 commit (d2d169d). |
| 111 | Phase 10 M1 Prefab System -- PrefabSystem (JSON load, ECS instantiate, override, security hardening), 3 Lua bindings (loadPrefab, instantiatePrefab, unloadPrefab), 21 tests (1451 total, Clang-18 + GCC-13 FULL). Screenshot pipeline fixes. 2 commits (066f496, f460525). |
| 110 | Phase 9 M6 Water Rendering -- WaterManager, reflection FBO (half-res), Fresnel blend, animated UV scroll, 4 Lua bindings (createWaterSurface, destroyWaterSurface, setWaterScrollSpeed, setWaterFresnelParams), 21 new tests (1430 total). Level 1 water surface. Phase 9 COMPLETE. |

## Phase 10 — Advanced Editor (IN PROGRESS)

- [x] M1 (Session 111): Prefab System -- PrefabSystem (JSON load, security hardening, ECS instantiate, PrefabOverride), 3 Lua bindings, 21 tests, ADR (553 lines), .context.md updated.
- [x] M2 (Session 112): Visual Scripting -- VisualScriptingSystem (11 nodes, topological sort, 14-step security pipeline), GraphEditorPanel (ImGui canvas, bezier wires, pan/zoom), 3 Lua bindings, 42 tests, ADR (918 lines), .context.md updated.
- [x] M3 (Session 113): LLM Integration Panel -- LlmPanel (async HTTP, context-aware queries, Lua snippet insertion), 2 Lua bindings (#ifdef FFE_EDITOR guarded), 15 tests, ADR (645+ lines Rev 2), .context.md updated.
- [ ] M4: Editor Preferences and Project Wizard / Installer (NEXT)
- [ ] M5: Animation Editor (planned)

## Phase 9 — Terrain and Open World (COMPLETE, Sessions 90-92, 108-110)

- [x] M1 (Session 90): Heightmap terrain rendering -- TerrainHandle, raw float + PNG heightmap, chunked mesh, bilinear height queries, 4 Lua bindings, 18 tests.
- [x] M2 (Session 91): Terrain texturing -- RGBA splat map, triplanar projection, TERRAIN shader (GLSL 330), 3 Lua bindings, 16 tests.
- [x] M3 (Session 92): LOD + frustum culling -- 3 LOD levels, distance-based selection, Griess-Hartmann frustum, 1 Lua binding, 13 tests.
- [x] M4 (Session 108): World streaming -- ChunkState machine, background worker thread, GL upload gate, dirty-distance gating, 2 Lua bindings, 20 tests.
- [x] M5 (Session 109): Vegetation -- GPU-instanced grass (24B/instance), tree placement (16B/instance), VEGETATION shader, 4 Lua bindings, 27 tests.
- [x] M6 (Session 110): Water rendering -- reflection FBO, Fresnel blend, UV scroll, WaterManager, 4 Lua bindings, 21 tests.

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
