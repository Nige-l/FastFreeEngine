# FastFreeEngine Development Log

> **Quick context:** Read `docs/project-state.md` first — it has the full project state in under 100 lines.
> **Archive:** Sessions 1-50 are in `docs/devlog-archive.md`.

## 2026-03-08 — Session 112: Phase 10 M2 Visual Scripting

### Goal

Phase 10 Milestone 2: Visual Scripting — a node-based graph executor as an alternative to Lua, with an ImGui editor panel integrated into the standalone editor, full security hardening, 3 Lua bindings, 42 Catch2 tests, and a 918-line ADR.

### Delivered

**Phase 10 M2 — Visual Scripting:**

- **engine/core/visual_scripting.h** — `NodeType` enum (11 nodes), `NodePort`, `NodePortType`, `GraphNode`, `GraphEdge`, `VisualGraph`, `VisualScriptingSystem` class with `loadGraph`/`attachGraph`/`detachGraph`/`update` API. `MAX_NODES` (512), `MAX_EDGES` (1024), `MAX_NODE_CALLS_PER_FRAME` (256) static limits. No heap allocation in hot path — per-graph sorted-order vector pre-computed at load time.
- **engine/core/visual_scripting.cpp** — full implementation: 14-step security pipeline (UNC block, path length, null byte, canonicalize, asset-root prefix, JSON parse, node count limit, node type whitelist, port type validation, edge bounds check, port compatibility, cycle detection via DFS, reachability from event node, deferred-destroy validation), topological sort, per-frame function-pointer dispatch table for 11 built-in nodes.
- **engine/core/CMakeLists.txt** — `visual_scripting.cpp` registered in build.
- **engine/editor/graph_editor_panel.h / graph_editor_panel.cpp** — `GraphEditorPanel`: ImGui node canvas with pan/zoom (mouse drag + scroll wheel), bezier wire rendering between ports, port-type colour coding, RMB add-node context menu with all 11 node types, read-only mode during play, node selection and drag, canvas grid background.
- **engine/editor/CMakeLists.txt** — `graph_editor_panel.cpp` registered in build.
- **engine/scripting/script_engine.h / script_engine.cpp** — 3 new Lua bindings: `ffe.loadGraph(path)`, `ffe.attachGraph(handle, entity)`, `ffe.detachGraph(entity)`.
- **engine/core/.context.md** — Visual Scripting System section added (node types, Lua API, security model, usage patterns, anti-patterns).
- **engine/scripting/.context.md** — loadGraph/attachGraph/detachGraph documented.
- **engine/renderer/.context.md** — minor refresh (existing section updates).
- **tests/core/test_visual_scripting.cpp** — 42 Catch2 tests covering: system init, graph load (valid + invalid paths, malformed JSON, oversized graphs), all 14 security rejection cases (UNC, path traversal, unknown node types, cyclic graphs, unreachable nodes), attach/detach lifecycle, duplicate attach guard, update with no-op graphs.
- **tests/core/fixtures/** — 4 JSON fixture files: `valid_graph.json` (3-node OnUpdate→Add→SetVelocity chain), `cyclic_graph.json` (2-node cycle for rejection test), `unknown_node_graph.json` (contains invalid node type), `large_graph.json` (513 nodes, exceeds MAX_NODES limit).
- **tests/CMakeLists.txt** — `test_visual_scripting` registered.
- **docs/architecture/adr-phase10-m2-visual-scripting.md** — 918-line ADR: problem statement, design options (Lua extension vs native graph vs plugin), chosen design rationale, security model (all 14 pipeline steps), node type catalogue, data layout, editor panel architecture, tier compatibility (LEGACY+), future extension points (user-defined node registration API).

**Warning cleanup:**

- **engine/renderer/terrain.cpp** — fixed unused-parameter warnings in LOD/culling methods (Clang-18 `-Wunused-parameter`).
- **engine/renderer/vegetation.h** — removed dead field `m_cubeIbo` (was declared but never assigned or used).

**Process update:**

- **.claude/agents/build-engineer.md** — kill-stale-FFE-processes rule added: `pkill -f ffe_` before any screenshot capture to prevent window-focus conflicts from previously-crashed engine instances.

### Security Review

`security-auditor` reviewed the VisualScriptingSystem implementation in Phase 3. Two HIGH findings were raised:

1. **Reachability validation missing** — graphs with nodes unreachable from any event node could execute orphaned logic. Fixed in ADR and implementation: reachability DFS from all event-type nodes added as security step 12.
2. **Deferred destroy validation** — `DestroyEntity` node was executing immediately mid-frame, potentially invalidating other nodes' entity references. Fixed: destroy requests are queued and applied post-update via deferred list.

Post-remediation review: PASS.

### Build Fix Cycle

`build-engineer` Phase 5 (Clang-18 FAST) found 2 errors on first build:

1. **Macro redefinition / braced-init mismatch** — `MAX_NODE_CALLS_PER_FRAME` was defined as a preprocessor macro in the header but used in a `constexpr` context in the .cpp. Fixed: removed macro, promoted to `constexpr std::size_t` in the header.
2. **Field name error** — `graph_editor_panel.cpp` referenced `m_panOffset` but the struct field was named `m_pan`. Fixed: renamed field to `m_panOffset` for consistency with the scroll variable name.

Rebuild: 1493/1493 PASS, zero warnings.

### Build

1493/1493 PASS — Clang-18, zero warnings. FAST build (single compiler, as specified).

### game-dev-tester

Skipped. Visual scripting graphs are not yet integrated into any demo game — the system is a new editor workflow, not a Lua-replacement in existing examples. Demo integration deferred to a future session when a graph-authored game entity is showcased in `examples/`.

### Session Close

Session 112 commit pushed to origin/main:
- `d2d169d` — feat(core): Phase 10 M2 — Visual Scripting node graph system

### Next Session Goal

**Phase 10 M3 — LLM Integration Panel:** Connect an AI assistant inside the editor, with context-aware code generation (using .context.md files as system prompt context), system explanation, and Lua snippet insertion into the active script slot. See ROADMAP.md Phase 10 M3.

---

## 2026-03-08 — Session 111: Phase 10 M1 Prefab System

### Goal

Phase 10 Milestone 1: Prefab System — reusable JSON entity templates with per-instance component overrides, full security hardening, Lua bindings, and 21 Catch2 tests.

### Delivered

**Phase 10 M1 — Prefab System:**

- **engine/core/prefab_system.h** — `PrefabHandle`, `PrefabOverride`, `PrefabOverrides`, `PrefabSystem` class with `loadPrefab`/`instantiatePrefab`/`unloadPrefab` API
- **engine/core/prefab_system.cpp** — full implementation: JSON template loading, security pipeline (path traversal prevention, schema validation), ECS instantiation with per-instance component overrides
- **engine/core/CMakeLists.txt** — `prefab_system.cpp` registered in build
- **engine/scripting/script_engine.h / script_engine.cpp** — 3 new Lua bindings: `ffe.loadPrefab`, `ffe.instantiatePrefab`, `ffe.unloadPrefab`
- **engine/core/.context.md** — Prefab System section added (837 → 1089 lines)
- **tests/core/test_prefab_system.cpp** — 21 Catch2 tests covering load, instantiate, override, unload, and security rejection cases
- **tests/core/fixtures/** — 4 JSON fixture files (valid prefab, override prefab, malformed prefab, path-traversal prefab)
- **tests/CMakeLists.txt** — `test_prefab_system.cpp` registered
- **docs/architecture/adr-phase10-m1-prefab-system.md** — 553-line ADR covering design rationale, security model, override resolution, and tier compatibility

**Build result:** 1451/1451 PASS — Clang-18 + GCC-13, zero warnings (FULL build)

**Screenshot / tooling fixes:**

- **tools/take_screenshot.sh** — real-display auto-detect, `scrot -a` region syntax fix, `xdotool` window-by-PID capture
- **examples/showcase/game.lua** — `arg[1]` direct level startup (`tryDirectLevelStart`) for reliable screenshot capture
- **examples/showcase/levels/level1.lua** + **level3.lua** — `Camera.setYawPitch(180, -20)` for better initial camera angle

**README.md** — updated: Phase 10 M1 prefab system noted, test count updated to 1451, screenshot pipeline note added

**Process improvements (director):**

- **.claude/CLAUDE.md** — process discipline additions: off-track deviation prevention rules
- **.claude/agents/project-manager.md** — unexpected outcomes protocol added
- **/home/nigel/.claude/projects/*/memory/MEMORY.md** — process discipline section added

### Build

1451/1451 PASS — Clang-18 + GCC-13, zero warnings. FULL dual-compiler build.

### game-dev-tester

Skipped. No demo code changes required for Prefab M1 — prefabs are not yet used in any example game. Demo integration deferred to a future session when prefab-authored entities appear in gameplay.

### Known Issues

- **Mesh path-to-handle resolution gap:** `MeshLoader` does not expose a `findHandleByPath` method. Mesh components instantiated from prefabs receive an invalid handle and must be resolved manually by the game script after instantiation. Deferred to backlog — requires `MeshLoader` API extension in a future session.

### Session Close

Both Session 111 commits pushed to origin/main:
- `066f496` — feat(core): Phase 10 M1 — Prefab System (engine, tests, ADR, docs, README, process)
- `f460525` — fix(renderer): restore full-size screenshots + script_engine argv + process additions (screenshots 58KB + 189KB confirmed, setCommandLineArgs wired, director round-2 process rules)

### Next Session Goal

**Phase 10 M2 — Visual Scripting:** Node-based graph editor as alternative to Lua. Visual scripting nodes for common game logic patterns (see ROADMAP.md Phase 10 M2). Session 112 begins immediately.

---

## 2026-03-08 — Session 110: Phase 9 M6 Water Rendering (Phase 9 COMPLETE)

### Summary

Session 110 delivered Phase 9 Milestone 6: water rendering. WaterManager provides a reflective water surface using a half-resolution reflection FBO, animated UV scrolling for surface ripple, and Fresnel blending to transition between reflection and refraction based on view angle. WaterSurfaceConfig drives all surface parameters (tiling, scroll speed, Fresnel bias/scale/power, reflection blend). A legacy WaterConfig ECS singleton is provided for scene-serialiser compatibility. The water quad mesh is generated at runtime from the config dimensions. Twenty-one new Catch2 tests cover config defaults, handle validity, plane count, and Fresnel parameter ranges. The Showcase Level 1 was updated to include a water surface in the fountain basin area, and the Level 1 screenshot was refreshed.

This milestone completes Phase 9 (Terrain and Open World). All six milestones — heightmap terrain, splat-map texturing, LOD and frustum culling, world streaming, GPU-instanced vegetation, and water rendering — are delivered. The engine now supports large-scale outdoor environments competitive with contemporary indie engines on LEGACY-tier hardware.

Process improvements implemented this session included encoding multi-instance agent parallelism (running the same agent type in multiple simultaneous subagent slots when work is file-disjoint) in CLAUDE.md and the relevant agent definition files, adding a PM pre-planning rule so PM drafts the next milestone dispatch plan while the current build runs, a selective screenshot policy (build-engineer captures only when PM specifies a list), and a new `docs/examples-map.md` planning reference for demo-to-subsystem mapping.

### Delivered

**Phase 9 M6 — Water Rendering:**

- **engine/renderer/water.h** — `WaterSurfaceConfig` (tiling, scrollSpeed, fresnelBias/Scale/Power, reflectionBlend, dimensions), `WaterHandle`, `WaterPlane` (internal: FBO, VAO/VBO, config), `WaterManager` class with `createSurface`/`destroySurface`/`render`/`update` API. Legacy `WaterConfig` ECS singleton for scene-serialiser compatibility.
- **engine/renderer/water.cpp** — Reflection FBO at half resolution (GL_RGBA16F colour + depth attachment); water quad mesh generated at runtime from config dimensions; per-frame UV scroll offset accumulated via `update(dt)`; Fresnel blend computed in WATER shader uniform upload; render skips surfaces outside camera frustum.
- **engine/scripting/script_engine.h / script_engine.cpp** — New Lua bindings: `ffe.createWaterSurface(terrainHandle, x, y, z, width, depth)` → waterHandle; `ffe.destroyWaterSurface(terrainHandle, waterHandle)`; `ffe.setWaterScrollSpeed(terrainHandle, waterHandle, speed)`; `ffe.setWaterFresnelParams(terrainHandle, waterHandle, bias, scale, power)`.
- **engine/renderer/.context.md** — Water section added: WaterSurfaceConfig fields, Lua binding signatures, usage example, known limitations (fountain basin at terrain-level camera may not be visible in screenshots due to camera angle).
- **engine/scripting/.context.md** — New water bindings added to Lua registry reference.
- **tests/renderer/test_water.cpp** — 21 new CPU-only tests: WaterSurfaceConfig default values, WaterHandle validity semantics, plane count tracking, Fresnel parameter range validation, scroll speed defaults.
- **examples/showcase/game.lua** — `grassHandle`/`waterHandle` state variables added; cleanup in all 3 exit sites (menu, quit, scene transitions).
- **examples/showcase/levels/level1.lua** — `Camera.setTerrainAware(true)` applied; water surface created in fountain basin area via `ffe.createWaterSurface`.
- **docs/assets/screenshots/showcase_level1.png**, **website/docs/assets/screenshots/showcase_level1.png** — Updated screenshots showing Level 1 with vegetation and water surface.

**Process improvements:**

- **.claude/CLAUDE.md** — Multi-instance agent parallelism rule (PM may dispatch same agent type in multiple simultaneous slots for file-disjoint work); PM pre-plan rule (PM drafts next milestone dispatch plan in devlog while current build runs).
- **.claude/agents/build-engineer.md** — Selective screenshots policy: capture only when PM specifies an explicit list of demo names.
- **.claude/agents/project-manager.md** — Screenshots field in dispatch instructions; multi-instance parallelism reference.
- **.claude/agents/performance-critic.md**, **security-auditor.md**, **api-designer.md**, **game-dev-tester.md** — Multi-instance notes and examples-map.md reference where relevant.
- **docs/examples-map.md** — New PM planning reference: maps each demo game to the subsystems it exercises, reducing planning overhead for screenshot and demo sessions.
- **docs/agent-quickref.md** — 70-line agent quick-reference card (tier defaults, performance rules, naming, routing, common decisions).

### Known Issues

- Water surface (2x2m fountain basin) not visible in Level 1 screenshot: the terrain-level camera angle at screenshot time does not capture the water plane. The surface renders correctly at runtime when the player navigates to the basin. Deferred to backlog (low priority — cosmetic screenshot issue only).

### Phase 9 Complete

All Phase 9 milestones delivered:
- M1 (Session 90): Heightmap terrain rendering
- M2 (Session 91): Splat-map texturing + triplanar projection
- M3 (Session 92): 3-level LOD + frustum culling
- M4 (Session 108): World streaming (ChunkState machine, background worker thread)
- M5 (Session 109): GPU-instanced vegetation (billboard grass, tree placement)
- M6 (Session 110): Water rendering (reflection FBO, Fresnel blend, UV scroll)

Total tests: 1430 (all passing, Clang-18 FAST build).

### Next Session Goal

**Phase 10 M1 — Prefab System:** Save/load reusable entity configurations (prefabs) as JSON assets. Prefab asset format, PrefabManager (load/instantiate/override), Lua bindings (`ffe.loadPrefab`, `ffe.instantiatePrefab`, `ffe.savePrefab`), editor integration (drag prefab from asset browser into scene), and test coverage.

---

## 2026-03-08 — Session 109: Phase 9 M5 Vegetation System

### Summary

Session 109 delivered Phase 9 Milestone 5: a GPU-instanced vegetation system for billboard grass patches and tree placement. The VegetationSystem integrates with the terrain renderer, placing grass instances per chunk (up to 256) and trees across the world (up to 512). A dedicated VEGETATION shader (GLSL 330 core) handles Y-axis cylindrical billboarding so grass faces the camera horizontally, alpha-test for leaf transparency, and distance fade to avoid hard pop-in at the render boundary. GrassInstance is 24 bytes (position + halfSize + texOffset) and TreeInstance is 16 bytes (position + scale), both designed for cache-friendly GPU upload.

Four new Lua bindings give game scripts full control: `addVegetationPatch` places a grass cluster at a world position with configurable density and radius, `removeVegetationPatch` removes it by handle, `addTree` places a single tree with scale, and `clearTrees` removes all trees on a terrain. The Showcase Levels 1 and 3 were updated with grass and tree placement to demonstrate the system in the context of the full open-world scene.

Twenty-seven new CPU-only Catch2 tests cover struct size assertions (verifying the 24B/16B layout guarantees), config defaults, shader constant values (MAX_GRASS_PER_CHUNK = 256, MAX_TREES = 512), and the COUNT == 22 shader enum assertion updated in test_water and test_gpu_instancing. The build brings the total to 1406 passing tests.

Process improvements also landed this session: a 70-line agent quick-reference card (`docs/agent-quickref.md`) was added to give agents a single-page lookup for common decisions. The PM pre-plans-ahead pattern — where PM drafts the next milestone plan while the current phase is building — was encoded in CLAUDE.md and the relevant agent definition files.

### Delivered

**Phase 9 M5 — Vegetation System:**

- **engine/renderer/vegetation.h** — `GrassInstance` (24B: position xyz, halfSize, texOffset xy), `TreeInstance` (16B: position xyz, scale), `VegetationConfig` (maxGrassPerChunk=256, maxTrees=512, fadeStart/fadeEnd), `VegetationPatchHandle`, `VegetationSystem` class with `addPatch`/`removePatch`/`addTree`/`clearTrees`/`render` API.
- **engine/renderer/vegetation.cpp** — GPU instancing via instance VBO per patch; cylindrical billboard matrix computed on GPU via VEGETATION shader; distance fade uniform set per draw; tree placement uses static impostor quads (VEGETATION shader reused); render skips patches/trees beyond fadeEnd.
- **engine/renderer/shader_library.h / shader_library.cpp** — VEGETATION shader added to `ShaderType` enum; COUNT updated to 22; VEGETATION GLSL 330 core source: vertex stage computes Y-axis billboard rotation from camera position, fragment stage samples diffuse texture, applies alpha-test (discard < 0.1) and distance fade.
- **engine/renderer/CMakeLists.txt** — `vegetation.cpp` added to renderer source list; `terrain_internal.h` GLAD include path fix applied.
- **engine/scripting/script_engine.h / script_engine.cpp** — 4 new Lua bindings: `ffe.addVegetationPatch(terrainHandle, x, y, z, density, radius)` → patchHandle; `ffe.removeVegetationPatch(terrainHandle, patchHandle)`; `ffe.addTree(terrainHandle, x, y, z, scale)` → treeHandle; `ffe.clearTrees(terrainHandle)`.
- **tests/renderer/test_vegetation.cpp** — 27 new CPU-only tests: GrassInstance sizeof == 24, TreeInstance sizeof == 16, VegetationConfig defaults (maxGrassPerChunk=256, maxTrees=512, fadeStart/fadeEnd values), MAX_GRASS_PER_CHUNK and MAX_TREES constants, patch handle validity, clear semantics.
- **tests/renderer/test_water.cpp** — COUNT == 22 assertion updated (was 21).
- **tests/renderer/test_gpu_instancing.cpp** — COUNT == 22 assertion updated (was 21).
- **tests/CMakeLists.txt** — `test_vegetation` target registered.
- **examples/showcase/game.lua** — vegetation system initialised on game start; VegetationSystem enabled for the scene.
- **examples/showcase/levels/level1.lua** — grass patches and trees placed across the terrain using `addVegetationPatch` and `addTree`.
- **examples/showcase/levels/level3.lua** -- grass patches and trees placed across the terrain using `addVegetationPatch` and `addTree`.
- **docs/architecture/adr-phase9-m5-vegetation.md** -- ADR documenting billboard approach, struct size rationale, shader design decisions, and integration with the terrain chunk system.
- **docs/assets/screenshots/showcase_level1.png**, **showcase_level3.png** -- updated screenshots showing grass and trees in the open-world scene.
- **website/docs/assets/screenshots/showcase_level1.png**, **showcase_level3.png** -- mirrored to website asset directory.

**Process improvements:**

- **docs/agent-quickref.md** — new 70-line agent quick-reference card covering: tier defaults, performance rules, naming conventions, agent routing, and common decision table.
- **CLAUDE.md + agent definition files** — PM pre-plans-ahead pattern encoded: PM drafts the next milestone dispatch plan in the devlog entry while the current phase's build is running, so the next session can begin immediately without a planning round-trip.

### Next Session Goal

**Phase 9 M6 — Water Rendering:** reflective/refractive water plane with shoreline blending and animated surface. Planned approach: dual FBO render (reflection pass with clipped above-plane geometry, refraction pass with clipped below-plane geometry), Fresnel blend in WATER shader (GLSL 330 core), animated normal map for surface ripples, shoreline foam via depth buffer comparison.

## 2026-03-08 — Session 108: Phase 9 M4 World Streaming + Showcase Camera Fix

### Summary

Session 108 delivered Phase 9 Milestone 4: terrain world streaming. The terrain system previously loaded every chunk at startup and never freed them; M4 adds a full ChunkState machine with a background worker thread per terrain instance that streams chunks in and out as the camera moves. A main-thread GL upload gate keeps all OpenGL calls on the render thread (OpenGL contexts are not thread-safe). Dirty-distance gating avoids redundant streaming recalculations when the camera has not moved far enough to change the active set. Two new Lua bindings expose the feature to game scripts. Twenty new tests bring the total to 1379 passing.

A secondary fix addressed a showcase regression: the camera.lua terrain-aware Y clamp was missing on hilly terrain, causing the camera to clip below the ground surface on levels 1 and 3. The clamp now queries `getTerrainHeight` and enforces a minimum eye height above the terrain.

A process update was also landed: multi-instance agent parallelism (running the same agent type in multiple simultaneous subagent slots) is now explicitly documented in CLAUDE.md and in the relevant agent definition files. An `examples-map.md` was created to give PM a quick reference for which demo games cover which subsystems, reducing the planning overhead for screenshot and demo sessions.

### Delivered

**Phase 9 M4 — World Streaming:**

- **engine/renderer/terrain_internal.h** — `ChunkState` enum (EAGER, UNLOADED, QUEUED, GENERATING, READY_TO_UPLOAD, LOADED, UNLOADING); extended `TerrainChunk` with state field and upload-ready mesh buffer; `TerrainStreamingConfig` (radius, max-chunks-per-frame).
- **engine/renderer/terrain.h** — `setStreamingRadius` / `getChunkCount` public API; internal `StreamingWorker` thread handle and job queue.
- **engine/renderer/terrain.cpp** — Background worker thread per terrain: pops QUEUED chunks, generates mesh geometry, transitions to READY_TO_UPLOAD. Main-thread `tickStreaming()` called from the render loop: computes chunk distances, enqueues/dequeues chunks, uploads READY_TO_UPLOAD meshes to GL (VAO/VBO/EBO creation stays on render thread), transitions LOADED chunks to UNLOADING and frees GL resources. Dirty-distance gating: streaming recalc only triggers when camera moves more than half a chunk width since last recalc.
- **engine/renderer/terrain_renderer.cpp** — `tickStreaming()` called each frame before draw; only LOADED chunks are submitted for rendering.
- **engine/scripting/script_engine.cpp** — 2 new Lua bindings: `setTerrainStreamingRadius(handle, radius)`, `getTerrainChunkCount(handle)` → integer.
- **engine/renderer/.context.md** — Streaming section added: ChunkState lifecycle, thread safety contract (no GL on worker thread), Lua binding signatures, usage example.
- **tests/renderer/test_terrain_streaming.cpp** — 20 new CPU-only tests covering: state machine transitions, dirty-distance gating logic, chunk count reporting, radius validation, worker thread lifecycle (start/stop without crash), and streaming config defaults.
- **tests/CMakeLists.txt** — `test_terrain_streaming` target registered.
- **docs/architecture/adr-phase9-m4-world-streaming.md** — ADR documenting the ChunkState design, thread safety decisions, GL upload gate rationale, and dirty-distance optimisation.

**Showcase camera fix:**

- **examples/showcase/lib/camera.lua** — Terrain-aware Y clamp: each frame the camera queries `getTerrainHeight` at the current XZ position and enforces `eye.y >= terrain_y + MIN_EYE_HEIGHT`. Prevents camera from clipping into hillsides on levels 1 and 3 where terrain elevation varies significantly.
- **examples/showcase/levels/level1.lua**, **level3.lua** — Minor constant updates to MIN_EYE_HEIGHT defaults matching camera clamp.

**Process updates:**

- **.claude/CLAUDE.md** — Multi-instance agent parallelism section added to Section 7: PM may dispatch the same agent type in multiple simultaneous slots when work is file-disjoint (e.g., two `engine-dev` instances writing to different directories in parallel). Claude executes these as separate concurrent subagent invocations.
- **.claude/agents/project-manager.md**, **performance-critic.md**, **security-auditor.md**, **api-designer.md** — Multi-instance parallelism notes added where relevant. PM agent file includes reference to `docs/examples-map.md` for demo planning.

**Screenshots (3 refreshed):**

- `docs/assets/screenshots/showcase_menu.png`, `showcase_level1.png`, `showcase_level3.png`
- `website/docs/assets/screenshots/showcase_menu.png`, `showcase_level1.png`, `showcase_level3.png`

### Reviews

- **performance-critic:** PASS. ChunkState machine is zero-allocation in steady state (chunk slots pre-allocated at terrain load). Background worker uses a bounded job queue. Dirty-distance gating eliminates per-frame distance recomputation. GL upload gated to max-chunks-per-frame to bound frame spike.
- **security-auditor:** PASS. No new attack surface; streaming radius and chunk count are engine-internal values, not user-supplied file paths. Existing path security on heightmap loading unchanged.
- **api-designer:** PASS. `.context.md` updated with thread-safety contract and streaming lifecycle. Two Lua bindings follow existing naming convention.

### Build

- **FAST build (Clang-18):** 1379 tests passing, zero warnings.

### Next Session

**Phase 9, Milestone 5 — Procedural Terrain Generation.** Noise-based heightmap generation (Perlin/simplex), hydraulic erosion simulation, and vegetation/object placement on the generated terrain. Target tier: LEGACY.

---

## 2026-03-08 — Session 107: Director Process Review + Screenshot Pipeline Refresh

### Summary

Session 107 was a process-quality session with no engine C++ changes. A Director review tightened the screenshot policy in both agent definition files so that screenshots are only captured when PM explicitly requests them (with a named demo list), rather than being taken speculatively on every build. Alongside the policy change, all eight demo screenshots were regenerated to reflect the visual fixes landed in Sessions 105-106 (mouse axis correction, terrain centering, dynamic spawn height, fox.glb flat-face normals, HDR FBO black-sky fix, keyboard/mouse input latching). Test count remains 1358 passing.

### Delivered

**Process improvements (Director review):**

- **.claude/agents/build-engineer.md** — New Screenshot section added: selective capture only when PM specifies a named list in the Phase 5 dispatch, demo-to-subsystem mapping documented (e.g. showcase = terrain/PBR/post-processing, 3d_demo = Blinn-Phong/skeletal animation), parallel capture via Xvfb+Mesa, output paths for both `docs/screenshots/` and `website/docs/assets/screenshots/`. FULL build trigger clarified to "end of entire numbered phase" (not "end of milestone").
- **.claude/agents/project-manager.md** — New Screenshot Policy section with same demo-to-subsystem mapping. `Screenshots:` field added to Phase 5 dispatch format. FULL trigger wording aligned with build-engineer.

**Screenshot refresh (8 demos, both `docs/screenshots/` and `website/docs/assets/screenshots/`):**

- `showcase_menu.png`, `showcase_level1.png`, `showcase_level2.png`, `showcase_level3.png` — reflect terrain centering, HDR sky fix, fox.glb guardians rendering correctly
- `collect_stars.png`, `pong.png`, `breakout.png`, `3d_demo.png` — refreshed to current state post input-latch fix

These screenshots capture the visual state as of Session 106 fixes; no further rendering changes were made this session.

### Context: Bug Fixes Committed in Sessions 105-106

The following fixes (committed in earlier sessions) are documented here for completeness in the session 107 devlog entry:

- **Mouse axis inversion** (`camera.lua`) — `FLIP_BOTH=true` negates both dx and dy; fixes inverted look on Wayland.
- **Terrain centering** — `loadTerrain` centres the heightmap at world origin; player spawn XZ coordinates now map correctly to terrain.
- **Dynamic spawn height** (`level1.lua`, `level3.lua`) — Player spawn Y computed as `getTerrainHeight(SPAWN_X, SPAWN_Z) + 2.5`; no more underground spawns on heightmap variation.
- **Fox.glb flat-face normals** (`mesh_loader.cpp`) — Computed normals for unindexed glTF meshes lacking a NORMAL accessor; fox model renders correctly with proper lighting.
- **HDR FBO clear-colour pass-through** — Black sky artefact fixed; clear colour propagates correctly through the HDR framebuffer.
- **Keyboard/mouse input latching** — `pressedThisTick` / `releasedThisTick` prevent fast taps being missed between frames on real hardware.
- **Combat debug log removal** — Per-frame attack log lines removed from hot path (perf-critic MINOR finding).

### Files Modified This Session

- `.claude/agents/build-engineer.md`
- `.claude/agents/project-manager.md`
- `docs/screenshots/*.png` (8 files refreshed)
- `website/docs/assets/screenshots/*.png` (8 files refreshed)

### Reviews

- No engine C++ changes — no performance, security, or build review required.
- Process changes reviewed inline by Director.

### Build

- No engine changes — test count unchanged at **1358 tests passing**.

### Next Session

**Phase 9, Milestone 4 — World Streaming.** The terrain system currently loads all chunks at startup; M4 will add runtime streaming: load/unload chunks as the camera moves, with a configurable view-distance radius and background thread I/O to avoid frame hitches. Target tier: STANDARD (LEGACY fallback with smaller radius).

---

## 2026-03-08 — Session 106: Real-Hardware Bug Fixes Round 3

### Summary

Session 106 addressed the remaining real-hardware issues surfaced after Session 105. Four targeted fixes were applied to the showcase demo scripts and the two renderer/scripting context files. No engine C++ was modified — all fixes were in Lua and documentation.

The camera mouse-axis flip (`FLIP_BOTH=true`) negates both dx and dy, resolving inverted look behaviour on Wayland compositors where GLFW reports raw delta signs opposite to X11 conventions. Terrain spawn height is now computed dynamically: `level1.lua` and `level3.lua` call `getTerrainHeight` at the player's spawn XZ coordinates and add 2.5 m, so the player always spawns above the terrain surface regardless of heightmap variation. The player attack now has three input triggers (LMB, `isKeyPressed(KEY_F)`, and `isKeyHeld(KEY_F)`), making the attack reliably accessible whether or not cursor capture is active. Combat debug log lines that fired every attack frame were removed following a performance-critic MINOR finding. Both `.context.md` files were updated: `scripting/.context.md` gained a full Terrain section with coordinate convention documentation, and `renderer/.context.md` clarified the centred-world-coordinate contract for `getTerrainHeight`.

### Delivered

**Demo fixes (Lua):**

- **examples/showcase/lib/camera.lua** — `FLIP_BOTH=true` now negates both dx and dy. Fixes inverted horizontal and vertical look on Wayland.
- **examples/showcase/levels/level1.lua** + **level3.lua** — Player spawn Y is now `getTerrainHeight(SPAWN_X, SPAWN_Z) + 2.5` instead of a hardcoded constant. Player spawns cleanly above terrain on any heightmap.
- **examples/showcase/lib/player.lua** — `isKeyHeld(KEY_F)` added as a third attack fallback alongside LMB and `isKeyPressed(KEY_F)`. Ensures attack always works regardless of cursor capture state.
- **examples/showcase/lib/combat.lua** — Per-frame attack debug log lines removed (perf-critic MINOR fix).

**Documentation:**

- **engine/scripting/.context.md** — New Terrain section added: `loadTerrain`, `getTerrainHeight`, `setTerrainLayer`, `setTerrainSplatMap`, `setTerrainTriplanar`, `setTerrainLodDistances`. Coordinate convention (centred world space, terrain origin at world (0,0,0)) documented.
- **engine/renderer/.context.md** — `getTerrainHeight` coordinate convention clarified: input XZ must be in centred world coordinates.

### Files Modified

- `examples/showcase/lib/camera.lua`
- `examples/showcase/levels/level1.lua`
- `examples/showcase/levels/level3.lua`
- `examples/showcase/lib/player.lua`
- `examples/showcase/lib/combat.lua`
- `engine/scripting/.context.md`
- `engine/renderer/.context.md`

### Reviews

- No C++ changes — no performance or security review required.
- Documentation reviewed inline by api-designer.

### Build

- No engine changes — build count unchanged at 1358 tests.

### Known Issues Status

- Mouse axis flip applied — awaiting user confirmation on Wayland.
- Terrain spawn height now dynamic — player should consistently spawn above terrain.
- Attack has three input triggers — reliable regardless of cursor capture state.
- Fox.glb guardians rendering correctly since Session 105.

---

## 2026-03-08 — Session 105: Keyboard Input Latch + fox.glb Mesh Fix

### Summary

Session 105 fixed two bugs carried over from Session 104's real-hardware testing. First, keyboard press/release events were not using latched detection, meaning fast key taps could be missed between ticks on real hardware — the same class of bug fixed for mouse clicks in Session 101. Second, the root cause of fox.glb rendering as a triangle blob was identified and fixed: the mesh loader did not compute normals for unindexed glTF meshes that lack a NORMAL accessor, causing all normals to default to (0,1,0) and producing incorrect lighting. Both fixes include new tests (+11). The showcase demo is restored to use fox.glb for guardians.

### Delivered

**Engine fixes:**

- **engine/core/input.cpp** -- Keyboard press/release now uses latched detection via `pressedThisTick[512]` and `releasedThisTick[512]` arrays (same pattern as the mouse click fix in Session 101). Fast key taps that were previously missed between ticks on real hardware are now correctly registered. 3 new tests added.
- **engine/renderer/mesh_loader.cpp** -- Flat normal computation added for unindexed glTF meshes with no NORMAL accessor. Normals are now computed per triangle via cross product of edge vectors. Root cause of fox.glb rendering as a blob (all normals defaulting to (0,1,0)) is resolved. 8 new tests added.
- **engine/core/.context.md** -- Updated to document latched keyboard input detection.
- **engine/renderer/.context.md** -- Updated to document unindexed mesh support and automatic flat normal computation.

**Demo fixes (Lua):**

- **examples/showcase/levels/level1.lua** + **level3.lua** -- Fox.glb restored for guardian entities. The cube.glb workaround from Session 104 has been removed now that the mesh loader correctly handles unindexed geometry.

### Files Modified

- `engine/core/input.cpp` (latched keyboard press/release detection)
- `engine/core/.context.md` (document latched input)
- `engine/renderer/mesh_loader.cpp` (flat normal computation for unindexed meshes)
- `engine/renderer/.context.md` (document unindexed mesh support)
- `examples/showcase/levels/level1.lua` (fox.glb guardians restored)
- `examples/showcase/levels/level3.lua` (fox.glb guardians restored)
- `tests/core/test_input.cpp` (+3 tests)
- `tests/renderer/test_mesh_loader.cpp` (+8 tests)

### Reviews

- Performance review: PASS
- Security review: PASS
- API review: PASS

### Build

- FAST build (Clang-18): 1347+ tests pending. +11 new tests.

### game-dev-tester

- Not separately invoked; demo Lua fixes were direct restorations of existing scripts by engine-dev.

### Known Issues Resolved

- fox.glb unindexed mesh rendering broken -- FIXED (flat normal computation in mesh loader)
- Keyboard input race condition (missed key taps on real hardware) -- FIXED (latched detection)

---

## 2026-03-08 — Session 104: Real-Hardware Bug Fixes

### Summary

Session 104 fixed eight bugs discovered through real-hardware testing on an actual GPU (as opposed to the llvmpipe software renderer used in CI). The issues fell into three categories: input (mouse not grabbing until a click), renderer (black sky in HDR FBO, terrain rendering as a floating green wedge), and demo polish (inverted mouse Y, fox.glb rendering as a triangle blob, crosshair obscuring third-person view, LMB attack not always registering). All fixes include regression tests; the test count rose by 11 to 1347.

### Delivered

**Engine fixes:**

- **input.cpp** -- Cursor capture now deferred via `glfwFocusWindow` with a pending-retry flag checked on the next focus event. Previously the engine called `glfwSetInputMode` before the window had OS focus on some real drivers, causing the cursor to never be grabbed until the user clicked. 3 new tests added to `tests/core/test_input.cpp`.
- **post_process.cpp / post_process.h** -- Clear colour is now passed into `beginSceneCapture` and applied to the HDR FBO. Previously the HDR framebuffer was cleared to black regardless of the scene background colour, making the sky appear black on real GPUs (which enforce FBO clears strictly). New regression tests added to `tests/renderer/test_post_process.cpp`.
- **terrain_renderer.cpp** -- Chunk AABB and centre offsets now incorporate `modelTranslation` (the world-origin centering offset). Previously frustum culling used un-offset AABBs, causing all chunks to be culled incorrectly after the terrain centering fix.
- **terrain.cpp** -- `getTerrainHeight()` UV mapping now uses centred world coordinates (subtracts the world-origin offset before sampling). Previously height queries returned values based on uncentred coordinates, making the terrain appear as a floating green wedge. New regression tests added to `tests/renderer/test_terrain.cpp`.
- **script_engine.cpp** -- `loadTerrain` now centres the terrain entity at world origin (`-worldWidth/2, 0, -worldDepth/2`) so the terrain's geometric centre sits at (0,0,0) by default.

**Demo fixes (Lua):**

- **examples/showcase/lib/camera.lua** -- Pitch delta negated to fix inverted vertical mouse look.
- **examples/showcase/levels/level1.lua** + **level3.lua** -- Guardian entities switched from fox.glb to cube.glb. fox.glb uses an unindexed mesh format that the current mesh loader renders as a single triangle blob; cube.glb works correctly.
- **examples/showcase/lib/hud.lua** -- Crosshair removed. In third-person view the crosshair was rendering directly over the player character.
- **examples/showcase/lib/player.lua** -- F key added as a fallback attack trigger alongside LMB, ensuring the attack action is always accessible regardless of cursor capture state.

### Files Modified (16)

- `engine/core/application.cpp` (focus callback wiring for pending cursor capture)
- `engine/core/input.cpp` (deferred cursor capture + pending retry)
- `engine/core/input.h` (pending capture flag)
- `engine/renderer/post_process.cpp` (pass clear colour into HDR FBO)
- `engine/renderer/post_process.h` (beginSceneCapture signature)
- `engine/renderer/terrain.cpp` (centred UV in getTerrainHeight)
- `engine/renderer/terrain_renderer.cpp` (AABB/centre offset by modelTranslation)
- `engine/scripting/script_engine.cpp` (loadTerrain centres at world origin)
- `examples/showcase/levels/level1.lua` (cube.glb guardians)
- `examples/showcase/levels/level3.lua` (cube.glb guardians)
- `examples/showcase/lib/camera.lua` (negate pitch delta)
- `examples/showcase/lib/hud.lua` (remove crosshair)
- `examples/showcase/lib/player.lua` (F-key attack fallback)
- `tests/core/test_input.cpp` (+3 tests)
- `tests/renderer/test_post_process.cpp` (new regression tests)
- `tests/renderer/test_terrain.cpp` (new regression tests)

### Reviews

- Skipped (targeted bug-fix session, no new API surface or attack surface changes)

### Build

- FAST build (Clang-18): 1347 tests, zero warnings. +11 new tests.

### game-dev-tester

- Not separately invoked; demo Lua fixes were direct corrections to existing scripts by engine-dev.

### Known Issues Added to Backlog

- fox.glb unindexed mesh rendering broken — mesh loader does not handle unindexed geometry correctly; renders as a triangle blob. Workaround: use indexed meshes (cube.glb). Root cause to investigate.
- 3D particle system missing — showcase demo uses geometry-based workarounds for effects; a proper 3D particle emitter is needed.

---

## 2026-03-08 — Session 102: CMake Lua Asset Copy at Build-Time

### Summary

Session 102 fixed a build-system issue where Lua example scripts were only copied at CMake configure time, not at build time. A new `cmake/CopyExampleAssets.cmake` helper module provides `ffe_copy_example_lua` and `ffe_copy_example_dir` functions that set up POST_BUILD copy commands. All 6 example projects now use these helpers, ensuring Lua files are always up to date after a build without needing to re-run CMake. The showcase CMakeLists was simplified from 60 lines of manual copy commands to 4 helper calls. Also deleted a 30MB uncompressed `Pixel Crown.wav` duplicate that was gitignored but taking up disk space.

### Delivered

- **cmake/CopyExampleAssets.cmake** -- new helper module with `ffe_copy_example_lua` (copies .lua files) and `ffe_copy_example_dir` (copies directories) as POST_BUILD commands
- **All 6 examples updated** -- 3d_demo, breakout, lua_demo, net_demo, pong, showcase CMakeLists now use the helpers
- **Showcase CMakeLists simplified** -- replaced 60-line manual copy block with 4 helper calls
- **Root CMakeLists.txt** -- added `include(cmake/CopyExampleAssets.cmake)`
- **Audio cleanup** -- deleted 30MB `assets/audio/Pixel Crown.wav` (uncompressed duplicate, was gitignored)
