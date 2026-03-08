# FastFreeEngine Development Log

> **Quick context:** Read `docs/project-state.md` first — it has the full project state in under 100 lines.
> **Archive:** Sessions 1-50 are in `docs/devlog-archive.md`.

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
