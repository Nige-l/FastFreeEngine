# FastFreeEngine Development Log

> **Quick context:** Read `docs/project-state.md` first — it has the full project state in under 100 lines.
> **Archive:** Sessions 1-50 are in `docs/devlog-archive.md`.

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
