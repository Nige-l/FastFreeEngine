# FastFreeEngine Development Log

> **Quick context:** Read `docs/project-state.md` first — it has the full project state in under 100 lines.
> **Archive:** Sessions 1-50 are in `docs/devlog-archive.md`.

## 2026-03-07 — Session 80: Phase 7 M4 — Anti-Aliasing (MSAA + FXAA)

### Summary

Session 80 implemented anti-aliasing for the rendering pipeline: MSAA via multisample HDR FBO with `glRenderbufferStorageMultisample` and `glBlitFramebuffer` resolve (configurable 2x/4x/8x samples), plus FXAA 3.11 (Timothy Lottes algorithm) as a post-process pass after tone mapping with edge detection and sub-pixel AA in GLSL 330. AntiAliasingConfig fields added to PostProcessConfig (aaMode, msaaSamples). Pass order: MSAA resolve -> bloom -> tone map -> FXAA -> gamma. 2 Lua bindings: `ffe.setAntiAliasing(mode)`, `ffe.setMSAASamples(count)`. GLAD additions: `glRenderbufferStorageMultisample`, `GL_MAX_SAMPLES`. Performance-critic: PASS. api-designer: .context.md updated. game-dev-tester: SKIPPED (no examples/ changes, AA is transparent). FAST build: 1137 tests, zero warnings.

### Planned

- Phase 7 M4: Anti-Aliasing — MSAA + FXAA

### Delivered

- **MSAA** -- Multisample HDR FBO, `glRenderbufferStorageMultisample`, `glBlitFramebuffer` resolve, configurable 2x/4x/8x samples
- **FXAA 3.11** -- Timothy Lottes algorithm, post-process pass after tone mapping, edge detection + sub-pixel AA, GLSL 330
- **AntiAliasingConfig** -- New fields in PostProcessConfig: aaMode (NONE/MSAA/FXAA), msaaSamples (2/4/8)
- **Pass Order** -- MSAA resolve -> bloom -> tone map -> FXAA -> gamma
- **2 Lua Bindings** -- `ffe.setAntiAliasing(mode)`, `ffe.setMSAASamples(count)`
- **GLAD Extensions** -- `glRenderbufferStorageMultisample`, `GL_MAX_SAMPLES`
- **22 New Tests** -- 17 anti-aliasing tests + 5 misc

### Files Changed

- `engine/renderer/post_process.h` (MODIFIED -- AntiAliasingConfig, MSAA FBO handles, FXAA pass)
- `engine/renderer/post_process.cpp` (MODIFIED -- MSAA FBO creation/resolve, FXAA post-process pass)
- `engine/renderer/shader_library.h` (MODIFIED -- FXAA shader enum)
- `engine/renderer/shader_library.cpp` (MODIFIED -- FXAA 3.11 shader source)
- `engine/renderer/.context.md` (MODIFIED -- anti-aliasing API documentation)
- `engine/scripting/script_engine.cpp` (MODIFIED -- ffe.setAntiAliasing, ffe.setMSAASamples bindings)
- `engine/scripting/.context.md` (MODIFIED -- AA binding docs)
- `third_party/glad/include/glad/glad.h` (MODIFIED -- glRenderbufferStorageMultisample, GL_MAX_SAMPLES)
- `third_party/glad/src/glad.c` (MODIFIED -- GL multisample function loader)
- `tests/renderer/test_anti_aliasing.cpp` (NEW -- 17 AA tests)
- `tests/renderer/test_gpu_instancing.cpp` (MODIFIED -- 5 additional tests)
- `tests/CMakeLists.txt` (MODIFIED -- new test file)

### Reviews

- performance-critic: PASS
- api-designer: PASS (.context.md updated)
- game-dev-tester: SKIPPED (AA is transparent, no new API paradigm)

### Next Session (81)

Phase 7 M5: Skeletal Animation Completion — crossfade blending, interpolation modes, root motion.

---

## 2026-03-07 — Session 79: Phase 7 M3 — GPU Instancing

### Summary

Session 79 implemented GPU instancing: entities sharing a MeshHandle (2+ non-skinned) are automatically batched via `glDrawElementsInstanced`. InstanceData struct (64 bytes = mat4 model matrix), MAX_INSTANCES_PER_BATCH=1024, shared instance VBO (64KB GL_STREAM_DRAW). 3 instanced shader variants (Blinn-Phong, PBR, shadow depth) using mat4 from vertex attributes 8-11. Shadow pass also instanced. Lua binding: `ffe.getInstanceCount(meshHandle)`. Added GL instancing functions to GLAD (`glDrawElementsInstanced`, `glVertexAttribDivisor`). Performance-critic: PASS. api-designer: PASS. game-dev-tester: SKIPPED (instancing is transparent/automatic, no new API paradigm). FAST build: 1115 tests, zero warnings.

### Planned

- Phase 7 M3: GPU Instancing — instance buffers, automatic batching

### Delivered

- **Automatic Mesh Batching** -- Entities sharing a MeshHandle (2+ non-skinned) grouped automatically by the mesh renderer, one `glDrawElementsInstanced` call per group
- **InstanceData Struct** -- 64 bytes (mat4 model matrix), MAX_INSTANCES_PER_BATCH=1024
- **Shared Instance VBO** -- 64KB GL_STREAM_DRAW buffer, updated per frame via `glBufferSubData`
- **3 Instanced Shader Variants** -- MESH_BLINN_PHONG_INSTANCED, MESH_PBR_INSTANCED, SHADOW_DEPTH_INSTANCED; mat4 from vertex attributes 8-11 via `glVertexAttribDivisor`
- **Instanced Shadow Pass** -- Shadow depth rendering also batches instanced meshes
- **GLAD Extensions** -- Added `glDrawElementsInstanced`, `glVertexAttribDivisor` typedefs and loader
- **1 Lua Binding** -- `ffe.getInstanceCount(meshHandle)` returns entity count sharing a mesh
- **21 New Tests** -- GPU instancing unit tests + scripting binding test

### Files Changed

- `engine/renderer/gpu_instancing.h` (NEW -- InstanceData, MAX_INSTANCES_PER_BATCH, instance buffer management)
- `engine/renderer/mesh_renderer.cpp` (MODIFIED -- instanced draw path, mesh grouping, shadow instancing)
- `engine/renderer/mesh_renderer.h` (MODIFIED -- instance buffer handles, grouping state)
- `engine/renderer/shader_library.h` (MODIFIED -- 3 new instanced shader enum values)
- `engine/renderer/shader_library.cpp` (MODIFIED -- instanced shader source code)
- `engine/renderer/.context.md` (MODIFIED -- GPU instancing API documentation)
- `engine/core/application.cpp` (MODIFIED -- instance buffer init/cleanup)
- `engine/scripting/script_engine.cpp` (MODIFIED -- ffe.getInstanceCount binding)
- `engine/scripting/.context.md` (MODIFIED -- instancing binding docs)
- `third_party/glad/include/glad/glad.h` (MODIFIED -- glDrawElementsInstanced, glVertexAttribDivisor)
- `third_party/glad/src/glad.c` (MODIFIED -- GL instancing function loader)
- `tests/renderer/test_gpu_instancing.cpp` (NEW -- 21 tests)
- `tests/CMakeLists.txt` (MODIFIED -- new test file)

### Reviews

- performance-critic: PASS
- api-designer: PASS
- game-dev-tester: SKIPPED (instancing is transparent/automatic, no new API paradigm)

### Next Session (80)

Phase 7 M4: Anti-Aliasing — MSAA (multisample FBOs, glBlitFramebuffer resolve) + FXAA post-process pass.

---

## 2026-03-07 — Session 78: Phase 7 M2 — Post-Processing Pipeline (HDR, Bloom, Tone Mapping, Gamma Correction)

### Summary

Session 78 implemented a full post-processing pipeline: HDR scene FBO (GL_RGBA16F), bloom (threshold extract + 13-tap separable Gaussian blur at half-resolution ping-pong), tone mapping (Reinhard + ACES filmic), and gamma correction. PostProcessConfig is an ECS singleton — opt-in only (pipeline only active when component is present). 3 new shaders (POST_THRESHOLD, POST_BLUR, POST_FINAL), fullscreen triangle via gl_VertexID (no VBO), shadow pass saves/restores active FBO, framebuffer resize updates post-process FBOs. 6 new Lua bindings, 42 new tests (26 renderer + 15 scripting + 1 misc). Added GL_TEXTURE1-7 to GLAD header. Performance-critic: PASS. api-designer: PASS. FAST build: 1094 tests, zero warnings.

### Planned

- Phase 7 M2: Post-processing pipeline — HDR, bloom, tone mapping, gamma correction

### Delivered

- **HDR Scene FBO** -- GL_RGBA16F color attachment, depth renderbuffer, scene renders to HDR buffer when post-processing enabled
- **Bloom** -- Threshold extract (brightness > 1.0), half-resolution ping-pong FBOs, 13-tap separable Gaussian blur (horizontal + vertical passes)
- **Tone Mapping** -- Reinhard and ACES filmic operators, selectable via Lua
- **Gamma Correction** -- Configurable gamma value (default 2.2), applied in final composite pass
- **PostProcessConfig ECS Singleton** -- Opt-in design: post-processing only activates when component is present, zero overhead otherwise
- **Fullscreen Triangle** -- Rendered via gl_VertexID (no VBO allocation), covers clip space
- **3 New Shaders** -- POST_THRESHOLD, POST_BLUR, POST_FINAL registered in shader library
- **FBO Integration** -- Shadow pass now saves/restores active FBO; framebuffer resize callback updates post-process FBOs
- **GLAD Extensions** -- Added GL_TEXTURE1-7 constants to glad.h/glad.c
- **6 Lua Bindings** -- enableBloom, disableBloom, setToneMapping, setGammaCorrection, enablePostProcessing, disablePostProcessing
- **42 New Tests** -- 26 renderer unit tests, 15 scripting binding tests, 1 misc

### Files Changed

- `engine/renderer/post_process.h` (NEW -- PostProcessConfig component, PostProcessPipeline class)
- `engine/renderer/post_process.cpp` (NEW -- pipeline implementation: FBO setup, bloom, tone mapping, composite)
- `engine/renderer/mesh_renderer.cpp` (MODIFIED -- post-process integration in render loop)
- `engine/renderer/shader_library.h` (MODIFIED -- POST_THRESHOLD, POST_BLUR, POST_FINAL shader IDs)
- `engine/renderer/shader_library.cpp` (MODIFIED -- shader source for 3 new shaders)
- `engine/renderer/CMakeLists.txt` (MODIFIED -- added post_process.cpp)
- `engine/renderer/.context.md` (MODIFIED -- post-processing API documentation)
- `engine/core/application.cpp` (MODIFIED -- framebuffer resize updates post-process FBOs)
- `engine/scripting/script_engine.cpp` (MODIFIED -- 6 new Lua bindings)
- `engine/scripting/.context.md` (MODIFIED -- post-processing binding docs)
- `third_party/glad/include/glad/glad.h` (MODIFIED -- GL_TEXTURE1-7)
- `third_party/glad/src/glad.c` (MODIFIED -- GL_TEXTURE1-7)
- `tests/renderer/test_post_process.cpp` (NEW -- 26 renderer tests)
- `tests/scripting/test_postprocess_bindings.cpp` (NEW -- 15 scripting tests)
- `tests/CMakeLists.txt` (MODIFIED -- new test files)

### Reviews

- performance-critic: PASS
- api-designer: PASS

### Next Session (79)

Phase 7 M3: GPU Instancing -- instance buffers, automatic batching, 1000-instance benchmark.

---

## 2026-03-07 — Session 77: Showcase Debug — Inverted Controls, Ground Visibility, Player Scale, HUD Overflow

### Summary

Session 77 ran the showcase executable to capture runtime logs and diagnosed 4 visual/control bugs. Fixed inverted WASD controls (forward vector was `+sin/+cos` instead of `-sin/-cos` since camera at +Z looks toward -Z), doubled ground Y-scale and brightened ground colors for visibility, increased player model scale from 0.5 to 1.8, and shortened HUD controls text from 81 to 70 chars to prevent overflow at 1280px with scale 2. game-dev-tester validated: SHIP 9/10. FAST build: 1052 tests, zero warnings.

### Planned

- Run showcase executable and capture logs for first time
- Debug and fix any issues found during live playtest

### Delivered

- **Inverted forward vector** -- Player forward vector was `+sin(yaw)/+cos(yaw)`, causing W to move backward and S to move forward. Fixed to `-sin(yaw)/-cos(yaw)` to match OpenGL convention (camera at +Z looks toward -Z).
- **Ground visibility** -- Ground plane Y-scale was 0.5 (paper-thin), doubled to 1.0. Ground colors brightened from muted dark tones to more visible values across all 3 levels.
- **Player scale** -- Player model at scale 0.5 was too small to see clearly. Increased to 1.8 for proper visibility.
- **HUD text overflow** -- Controls HUD text was 81 characters, overflowing 1280px viewport at scale 2. Shortened to 70 characters to fit within viewport.

### Files Changed

- `examples/showcase/` (MODIFIED -- forward vector, ground scale/colors, player scale, HUD text)

### Reviews

- game-dev-tester: SHIP (9/10)

### Next Session (78)

Phase 7 M2: Post-Processing Pipeline -- HDR FBO chain, bloom, tone mapping, gamma correction.

---

## 2026-03-07 — Session 76: Engine Framebuffer Resize Fix + Showcase Physics/Visual Scale Match

### Summary

Session 76 fixed an engine-level bug (missing framebuffer resize callback causing stale screen dimensions) and corrected physics/visual scale mismatches in all 3 showcase levels. Music updated to BattleMusic.mp3 for all levels. Director process reform files committed. Performance-critic: PASS. game-dev-tester: SHIP 8/10. FAST build: 1052 tests, zero warnings.

### Planned

- Fix framebuffer resize callback (engine bug found by renderer-specialist)
- Fix physics halfExtents 2x mismatch in showcase levels
- Update music to BattleMusic.mp3
- Commit director process reform files from Session 75

### Delivered

- **Engine framebuffer resize callback** -- renderer-specialist discovered missing `glfwSetFramebufferSizeCallback`, causing `getScreenWidth`/`getScreenHeight` to return stale values after window resize. Added `glfwFramebufferSizeCallback`, `rhi::setViewportSize`, and `Application::onFramebufferResize`. Fixes text clipping and layout bugs at non-default resolutions.
- **Physics halfExtents fix** -- All 3 showcase levels had physics bodies with halfExtents = 2x visual size (e.g., visual cube size N but physics halfExtent 2N). Corrected to match visual scale in all levels.
- **BattleMusic** -- All showcase levels now use BattleMusic.mp3 as placeholder music.
- **Director process reform** -- Committed updated agent files from Session 75: game-dev-tester is now mandatory for all demo/showcase changes (CLAUDE.md, PM, game-dev-tester, director agent files).

### Files Changed

- `engine/core/application.cpp` (MODIFIED -- framebuffer resize callback)
- `engine/renderer/rhi.h` (MODIFIED -- setViewportSize declaration)
- `engine/renderer/opengl/rhi_opengl.cpp` (MODIFIED -- setViewportSize implementation)
- `examples/showcase/` (MODIFIED -- halfExtents fix, BattleMusic in all levels)
- `.claude/agents/` (MODIFIED -- process reform files)

### Reviews

- performance-critic: PASS
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: SHIP (8/10)

### Next Session (77)

Phase 7 M2: Post-Processing Pipeline -- HDR FBO chain, bloom, tone mapping, gamma correction.

---

## 2026-03-07 — Session 75: Showcase Bug Fixes + game-dev-tester Process Reform

### Summary

Session 75 fixed 7 user-reported bugs in the "Echoes of the Ancients" showcase demo and reformed the game-dev-tester agent process. Added 3 new Lua bindings (getMouseDeltaX, getMouseDeltaY, setCursorCaptured) for FPS mouse controls. Director review completed: game-dev-tester upgraded from "conditional only" to "mandatory for demo changes." game-dev-tester validated the showcase (8/10, found 2 additional issues, both fixed). FAST build: 1052 tests, zero warnings.

### Planned

- Fix 7 user-reported showcase demo bugs
- Director review of game-dev-tester process

### Delivered

- **Model rotation fix** -- Models now rotate to face movement direction correctly.
- **Ground visibility fix** -- Ground plane visible in all levels.
- **Health drain grace period** -- AI enemies no longer damage player instantly; added grace period after spawn.
- **Music loading fix** -- Corrected music file paths for all levels.
- **UI scaling fix** -- HUD elements scale correctly at different resolutions.
- **FPS mouse controls** -- Added `ffe.getMouseDeltaX()`, `ffe.getMouseDeltaY()`, `ffe.setCursorCaptured(bool)` Lua bindings for proper FPS camera control.
- **Attack input fix** -- Melee attack input now registers reliably.
- **Process reform** -- Director review upgraded game-dev-tester from conditional to mandatory for all demo/showcase changes. Updated CLAUDE.md, PM agent file, game-dev-tester agent file, director agent file.
- **game-dev-tester validation** -- Scored showcase 8/10, found 2 additional issues (both fixed in-session).
- **6 new tests** -- Mouse delta and cursor capture binding tests.

### Files Changed

- `engine/scripting/script_engine.cpp` (MODIFIED -- 3 new Lua bindings)
- `engine/core/application.h/cpp` (MODIFIED -- mouse delta + cursor capture support)
- `examples/showcase/` (MODIFIED -- 7 bug fixes across game, player, camera, HUD, levels)
- `tests/scripting/test_mouse_bindings.cpp` (CREATED -- 6 tests)
- `.claude/CLAUDE.md` (MODIFIED -- game-dev-tester process reform)
- `.claude/agents/project-manager.md` (MODIFIED)
- `.claude/agents/game-dev-tester.md` (MODIFIED)
- `.claude/agents/director.md` (MODIFIED)

### Reviews

- performance-critic: PASS
- api-designer: PASS
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: PASS (8/10, 2 issues found and fixed)
- director: COMPLETED (process reform approved)

### Next Session (76)

Phase 7 M2: Post-Processing Pipeline -- HDR FBO chain, bloom, tone mapping, gamma correction.

---

## 2026-03-07 — Session 74: Phase 7 M1 — PBR Materials + Fog System

### Summary

Session 74 delivered Phase 7 Milestone 1: PBR materials with Cook-Torrance BRDF, metallic-roughness workflow, and Image-Based Lighting (IBL) via skybox cubemap. Also delivered the fog system (linear fog with Lua bindings). 41 new tests (17 PBR material + 24 PBR bindings). FAST build: 1046 tests, zero warnings.

### Planned

- PBR Materials (Cook-Torrance BRDF, metallic-roughness workflow)
- IBL pipeline (irradiance cubemap from existing skybox)
- Lua bindings for PBR material properties
- Fog system with Lua bindings

### Delivered

- **PBR Materials** -- PBRMaterial component (POD struct), Cook-Torrance BRDF shader (GGX normal distribution, Smith geometry, Fresnel-Schlick), metallic-roughness workflow with albedo, metallic, roughness, AO, and emissive maps.
- **Image-Based Lighting** -- IBL pipeline using existing skybox cubemap for ambient lighting. Irradiance convolution and prefiltered specular maps.
- **Fog system** -- Linear depth fog integrated into both Blinn-Phong and PBR shaders. `ffe.setFog(r, g, b, near, far)` and `ffe.disableFog()` Lua bindings.
- **Lua bindings** -- `ffe.setPBRMaterial`, `ffe.setPBRTexture` and related PBR property bindings.
- **41 new tests** -- 17 PBR material component tests, 24 PBR Lua binding tests.

### Files Changed

- `engine/renderer/` (MODIFIED -- PBR shader, material component, mesh renderer updates)
- `engine/scripting/script_engine.cpp` (MODIFIED -- PBR Lua bindings)
- `engine/renderer/.context.md` (MODIFIED -- PBR documentation)
- `engine/scripting/.context.md` (MODIFIED -- PBR binding documentation)
- `tests/renderer/test_pbr_material.cpp` (CREATED -- 17 tests)
- `tests/scripting/test_pbr_bindings.cpp` (CREATED -- 24 tests)

### Reviews

- performance-critic: PASS
- api-designer: PASS
- security-auditor: SKIPPED (no new attack surface -- IBL reuses reviewed skybox path)
- game-dev-tester: SKIPPED (no new API paradigm)

### Next Session (75)

Showcase bug fixes (user-reported issues) and process improvements.

---

## 2026-03-07 — Session 73: Phase 6 COMPLETE + Phase 7 Planning

### Summary

Session 73 closed Phase 6 and planned Phase 7. No C++ changes this session — documentation and planning only. The Phase 7 ADR (rendering pipeline modernisation) was approved by architect, covering 8 milestones: PBR materials, post-processing (bloom/tone mapping/gamma), GPU instancing, skeletal animation completion, anti-aliasing (MSAA+FXAA), SSAO, and sprite batching 2.0. README updated with showcase game details. ROADMAP updated with Phases 7-12. No build needed (no engine changes).

### Planned

- Phase 6 assessment and close-out
- Phase 7 ADR: rendering pipeline modernisation
- README update with showcase game section
- ROADMAP update with future phases

### Delivered

- **Phase 6 COMPLETE** — "Echoes of the Ancients" 3-level 3D showcase game shipped (Sessions 66-73).
- **Phase 7 ADR** — `docs/architecture/adr-phase7-rendering-pipeline.md` — comprehensive design for PBR, post-processing, instancing, skeletal animation, AA, SSAO, sprite batching 2.0. All features target LEGACY tier (GL 3.3) except SSAO (STANDARD+).
- **README.md** — Updated with showcase game details, Phase 6 COMPLETE status, Phase 7 preview.
- **docs/ROADMAP.md** — Phase 6 marked COMPLETE, Phases 7-12 roadmap added.

### Files Changed

- `docs/architecture/adr-phase7-rendering-pipeline.md` (CREATED — 596 lines)
- `README.md` (MODIFIED — showcase game details, phase updates)
- `docs/ROADMAP.md` (MODIFIED — Phase 6 COMPLETE, Phases 7-12 added)

### Reviews

- No expert panel this session (documentation/planning only, no engine C++ changes)
- game-dev-tester: SKIPPED (no new API)
- security-auditor: SKIPPED (no new attack surface — noted in ADR that IBL reuses existing reviewed skybox path)

### Next Session (74)

Phase 7 M1: PBR Materials — first engine C++ session since Session 67 (fog). Per the ADR:
1. PBRMaterial component (POD struct, 68 bytes)
2. MESH_PBR shader (Cook-Torrance BRDF, GLSL 330 core)
3. IBL pipeline (irradiance cubemap, prefiltered specular, BRDF LUT generation)
4. Mesh renderer updated to check PBRMaterial first, fall back to Material3D
5. Lua bindings: ffe.setPBRMaterial, ffe.setPBRTexture
6. Catch2 tests for PBRMaterial component and shader registration
7. Full expert panel (first engine code in several sessions)

---

## 2026-03-07 — Session 72: Phase 6 M4b — Main Menu, Pause Menu, Victory Polish, Gamepad

### Summary

Session 72 delivered the Phase 6 M4b polish pass for "Echoes of the Ancients." Added a full main menu with animated title, fog bands, and control help. Implemented a navigable pause menu (Resume/Restart/Quit) with keyboard and gamepad support. Polished the victory sequence with 20 animated gold sparkle particles, staggered stat reveal, a rank system, and return-to-menu flow. Applied dead-zone filtering on both gamepad sticks and added dynamic HUD control labels that switch between keyboard/gamepad prompts. All 3 levels verified working with real models, puzzles, and portals. FAST build: 1005 tests, zero warnings.

### Planned

- Main menu screen with title and controls help
- Pause menu with Resume/Restart/Quit
- Victory sequence polish (particles, rank)
- Gamepad dead-zones and dynamic HUD labels
- Level verification across all 3 levels

### Delivered

- **Main menu** -- Title screen with "ECHOES OF THE ANCIENTS" title, animated fog bands, control help overlay, gamepad START support.
- **Pause menu** -- ESC/gamepad START toggles pause. Navigable menu with Resume, Restart, Quit options. Keyboard and gamepad D-pad navigation.
- **Victory polish** -- 20 animated gold sparkle particles with staggered timing, stat reveal animation, rank system (S/A/B/C based on time+artifacts+enemies), return to main menu.
- **Gamepad polish** -- Dead-zone filtering on both analog sticks, dynamic HUD labels that detect controller vs keyboard input.
- **Level verification** -- All 3 levels confirmed working end-to-end with real .glb models, puzzles, portals, and progression.

### Files Changed

- `examples/showcase/lib/menus.lua` (CREATED -- menu system module)
- `examples/showcase/game.lua` (MODIFIED -- menu integration, pause, victory flow)
- `examples/showcase/lib/player.lua` (MODIFIED -- dead-zone filtering)
- `examples/showcase/lib/camera.lua` (MODIFIED -- dead-zone filtering)
- `examples/showcase/lib/hud.lua` (MODIFIED -- dynamic control labels)
- `examples/showcase/levels/level1.lua` (MODIFIED -- minor fixes)
- `examples/showcase/levels/level2.lua` (MODIFIED -- minor fixes)
- `examples/showcase/levels/level3.lua` (MODIFIED -- minor fixes)

### Reviews

- No expert panel this session (Lua-only showcase polish, no engine C++ changes)
- game-dev-tester: SKIPPED (no new API paradigm)
- security-auditor: SKIPPED (no new attack surface)

### Next Session (73)

Phase 6 M5 close-out and Phase 7 planning:
1. Phase 6 assessment -- is the showcase game complete?
2. README update with showcase game section
3. Phase 6 close and transition planning
4. Begin Phase 7 -- high-impact engine features to compete with mainstream engines

---

## 2026-03-07 — Session 71: Phase 6 M4 — Level 3 "The Summit" + Victory Sequence

### Summary

Session 71 delivered Phase 6 Milestone 4: Level 3 "The Summit" — the final level of "Echoes of the Ancients." Features floating platforms above the clouds, sine-oscillation moving platforms, sunset lighting, 4 guardians (including a boss), artifact collection, and a complete victory sequence with a stats screen showing completion time, enemies defeated, and artifacts collected. The game now has full 3-level progression through to a victory state. FAST build: 1005 tests, zero warnings. Reviews: performance PASS, api PASS.

### Planned

- Level 3 "The Summit" — floating platforms, moving platforms, sunset skybox, guardians, victory

### Delivered

- **Level 3 "The Summit"** — 700+ line final level with floating stone platforms, sine-driven moving platforms, sunset lighting (warm amber ambient, orange/gold point lights), 4 guardians across separate platforms (including gold boss with double HP), artifact collection on each platform, and final combined puzzle.
- **Victory sequence** — Game transitions to victory state after Level 3 completion, displaying stats screen with total time, enemies defeated, artifacts collected, and "Press ENTER to play again" prompt.
- **Game progression complete** — Full 3-level arc: The Courtyard (outdoor) -> The Temple (underground) -> The Summit (sky platforms) -> Victory.

### Files Changed

- `examples/showcase/levels/level3.lua` (CREATED — 700+ lines)
- `examples/showcase/game.lua` (MODIFIED — Level 3 integration + victory state)

### Reviews

- performance-critic: PASS
- api-designer: PASS
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: SKIPPED (no new API paradigm)

### Deferred

- Level 3 gameplay polish (moved to Session 72)
- Main menu screen (moved to Session 72)
- Gamepad verification pass (moved to Session 72)

### Next Session (72)

Session 72 completes M4b polish and begins M5 work:
1. Level 3 gameplay polish — balance guardian difficulty, moving platform timing, ensure all interactions feel good
2. Main menu screen — title, "Press ENTER to start," controls help overlay
3. Gamepad verification pass across all 3 levels — ensure all actions work with gamepad
4. Quick polish items — any rough edges across levels

---

## 2026-03-07 — Session 70: Phase 6 M3 Complete — Real Models, Music, Level 2 Gameplay, README Update

### Summary

Session 70 completed Phase 6 Milestone 3 (Level 2 "The Temple"). Downloaded 7 CC0 .glb models (4.35 MB) replacing placeholder cubes across all levels. Integrated Suno music tracks (BattleMusic.mp3 for Level 2, music_pixelcrown.ogg available). Added Level 2 gameplay: crystal puzzle (4-crystal activation sequence), timed disappearing bridges, boss guardian (double HP, gold coloring), and portal victory condition. Fixed GitHub Pages deployment by deleting the Jekyll workflow that was overriding MkDocs. Disabled macOS CI (user approved -- upstream LuaJIT arm64-osx is a persistent issue). Added E key / gamepad Y for crystal interaction. Comprehensive README refresh reflecting Phase 6, future phases, and the showcase game. FAST build: 1005 tests, zero warnings.

### Planned

- Real CC0 3D models (replace cubes)
- Music integration (Suno tracks)
- Level 2 gameplay (crystal puzzle, timed bridges, boss)
- GitHub Pages fix
- README update

### Delivered

- **Real 3D models** -- 7 CC0 .glb models downloaded (damaged_helmet, cesium_man, fox, duck, rigged_figure, rigged_simple, box_vertex_colors). Updated level1.lua, test_level.lua, player.lua, ai.lua to use real models instead of cubes. Total 4.35 MB.
- **Music integration** -- Wired BattleMusic.mp3 for Level 2, music_pixelcrown.ogg available for future levels.
- **Level 2 gameplay** -- Crystal puzzle (4-crystal sequence with colored lights), timed disappearing bridges (fall after stepping), boss guardian (double HP, gold coloring, larger size), portal victory condition linking to Level 3.
- **Player interaction** -- E key / gamepad Y button for crystal activation and other interactions.
- **GitHub Pages fix** -- Deleted `.github/workflows/jekyll-gh-pages.yml` that was overriding MkDocs deployment.
- **macOS CI disabled** -- Removed macOS job from CI workflow. Upstream LuaJIT vcpkg arm64-osx build is persistently broken. User approved.
- **README overhaul** -- Comprehensive refresh: showcase game prominently featured, all 6 demos listed, 1005 tests, ~169 bindings, future phases roadmap (Vulkan, terrain, advanced editor, cross-platform, asset pipeline, plugin system, advanced rendering, AI tooling), macOS status updated.

### Reviews

- No expert panel this session (documentation/asset session, no engine C++ changes)

### Deferred

- game-dev-tester: SKIPPED (no new API paradigm)
- security-auditor: SKIPPED (no new attack surface)

### Next Session (71)

Phase 6 M4: Level 3 "The Summit" -- floating platforms above the clouds, dramatic sunset skybox, moving platforms (sine/cosine driven), wind particles, 4 guardians on separate platforms, final combined puzzle, victory sequence. Per ADR Section 8, this is Sessions 71-72.

---

## 2026-03-07 — Session 69: Phase 6 M3 (part 1) — Level 2 "The Temple"

### Summary

Session 69 delivered the first half of Phase 6 Milestone 3: Level 2 "The Temple" — an underground temple environment with dark atmospheric lighting, lava pit, crystal pedestals, narrow bridges, 2 purple guardians, and an artifact on a central altar. Also fixed macOS CI by making vcpkg overlay always-on (upstream LuaJIT port broken on arm64-osx). FAST build: 1005 tests pass on Clang-18, zero warnings.

### Planned

- Level 2 "The Temple" — underground environment with dark lighting, hazards, guardians
- macOS CI fix (vcpkg overlay)

### Delivered

- **Level 2 "The Temple"** — 532-line underground temple level with dark atmospheric lighting (low ambient 0.05/0.05/0.08), lava pit with orange glow lights, crystal pedestals with blue/purple/green lights, narrow stone bridges, 2 purple guardians patrolling corridors, artifact on central altar. Short-range dark fog (dark blue/purple, 3-18 range). Files: `examples/showcase/levels/level2.lua`, `examples/showcase/game.lua` (Level 2 added to LEVELS table).
- **macOS CI fix (take 2)** — Made vcpkg overlay always-on instead of conditional on MinGW. Upstream LuaJIT port is broken on arm64-osx, so the overlay is needed everywhere. Files: `CMakeLists.txt`, `docs/environment.md`.

### Reviews

- performance-critic: PASS (57/80 entities, 4/4 lights, 31/40 bodies)
- api-designer: PASS (all ffe.* calls verified, 1 unused variable noted, 2 doc gaps noted)
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: SKIPPED (no new API paradigm)

### Critical User Feedback (HIGH PRIORITY for Session 70)

1. **"The showcase needs to be EPIC"** — User wants REAL 3D models from CC0 sources (Kenney, Quaternius, etc.). Cubes are NOT acceptable. This is the #1 priority.
2. **"We have music"** — User has Suno tracks in `assets/audio/` (BattleMusic.mp3, music_pixelcrown.ogg, Pixel Crown.wav, etc.). Use these.
3. **"Be conscious of disk space"** — Selective downloads, no massive packs.
4. **"Exclude models from build"** — Downloaded assets should be copied, not compiled.
5. **GitHub Pages showing just README** — `jekyll-gh-pages.yml` (commit e935f47) overrides MkDocs deploy. Must be deleted.

### Next Session (70)

Priority: Download real CC0 3D models (Kenney .glb packs, Quaternius characters), wire up existing Suno music tracks, fix GitHub Pages by removing Jekyll workflow, Level 2 gameplay (crystal puzzle, timed platforms, boss guardian). The user wants EPIC visuals — colored cubes must go.

---

## 2026-03-07 — Session 68: Phase 6 M2 — Level 1 "The Courtyard"

### Summary

Session 68 delivered Phase 6 Milestone 2: Level 1 "The Courtyard" is now a complete, playable level in the "Echoes of the Ancients" showcase game. Also fixed macOS CI by making the vcpkg overlay port conditional on MinGW targets. FAST build: 1005 tests pass on Clang-18, zero warnings.

### Planned

- Level 1 "The Courtyard" — full gameplay (puzzles, combat, artifact collection)
- macOS CI fix (overlay port shadowing upstream LuaJIT on non-MinGW platforms)

### Delivered

- **Level 1 "The Courtyard"** — Complete playable level with push-block puzzle (2 blocks, 2 pressure plates, 1 gate), 2 guardian enemies with patrol/chase AI, destructible wall hiding the artifact, fog, directional shadows, 4 point lights (torches). Files: `examples/showcase/levels/level1.lua`.
- **Game flow updates** — `game.lua` updated with level cleanup on transitions, level name display, completion delay, proper shutdown cleanup. `lib/hud.lua` updated with dt-based prompt fade (replacing ffe.after timer approach).
- **Assets** — cube.glb model, PressStart2P TTF font, placeholder audio files (courtyard music, collect/gate/hit SFX). `examples/showcase/ASSETS.md` documents asset sources and licenses.
- **CMakeLists** — `examples/showcase/CMakeLists.txt` updated to copy assets to build directory alongside Lua scripts.
- **macOS CI fix** — Made `VCPKG_OVERLAY_PORTS` conditional on `VCPKG_TARGET_TRIPLET` matching `*mingw*`. Previously the overlay unconditionally shadowed the upstream LuaJIT port, breaking macOS arm64 builds. Files: `CMakeLists.txt`, `docs/environment.md`.

### Reviews

- performance-critic: PASS (38/80 entities, 4/4 lights, 34/40 physics bodies — within LEGACY budget)
- api-designer: PASS (all ffe.* calls verified correct)
- security-auditor: SKIPPED (no new attack surface)
- game-dev-tester: SKIPPED (no new API paradigm — all calls use existing patterns)

### Deferred

- Net Arena 's' key issue — user-reported, investigate in a future session
- Unreal project porting tools — long-term roadmap item
- GitHub Pages deployment — user enabled Pages; existing MkDocs workflow should deploy on push

### Next Session (69)

Phase 6 M3 (first half): Level 2 "The Temple" — underground scene with dark lighting (minimal ambient, point lights from crystals and lava glow), fog (dark, short range), pillars/bridges/lava pit/crystal pedestals, particles (lava bubbles, crystal sparkle), dark ambient audio. Per ADR Section 3.2.

---

## 2026-03-07 — Session 67: Phase 6 M1 — Linear Fog + Showcase Scaffold

### Summary

Session 67 delivered Phase 6 Milestone 1: linear fog in the Blinn-Phong shader and the "Echoes of the Ancients" showcase game scaffold. FAST build: 1005 tests pass on Clang-18, zero warnings.

### Planned

- Linear fog shader (ffe.setFog / ffe.disableFog)
- Showcase game project scaffold with player controller, camera, combat, AI, HUD
- Asset acquisition plan

### Delivered

- **Linear fog** -- FogParams struct (color, near, far), Blinn-Phong fragment shader integration, `ffe.setFog(r, g, b, near, far)` and `ffe.disableFog()` Lua bindings, 14 Catch2 tests. Files: `engine/renderer/mesh_renderer.h/cpp`, `engine/renderer/shader_library.cpp`, `engine/core/application.h/cpp`, `engine/scripting/script_engine.cpp`, `tests/renderer/test_fog.cpp`.
- **Showcase scaffold** -- "Echoes of the Ancients" project structure under `examples/showcase/`. Includes `game.lua` (main menu + level sequencing), `lib/player.lua` (WASD + gamepad movement, jump, orbit camera), `lib/camera.lua` (orbit camera with collision avoidance), `lib/hud.lua` (health bar, artifact count, interaction prompts), `lib/combat.lua` (melee attack, damage, health system), `lib/ai.lua` (patrol + chase AI state machine), `levels/test_level.lua` (test environment).
- **Asset plan** -- `examples/showcase/ASSETS.md` documenting CC0 asset sources (Kenney, Quaternius, ambientCG, OpenGameArt).
- **Doc updates** -- `engine/renderer/.context.md` (fog API), `engine/scripting/.context.md` (fog bindings).

### Reviews

- performance-critic: PASS
- api-designer: PASS (remediation: fog docs added to scripting .context.md)
- security-auditor: SKIPPED (no attack surface)
- game-dev-tester: SKIPPED (no new API paradigm)

### Deferred

- Net Arena 's' key issue -- user-reported, investigate in a future session
- GitHub Pages 404 -- user must enable Pages in repo Settings > Pages (not a code fix)

### Next Session (68)

Phase 6 M2 (first half): Level 1 "The Courtyard" -- download CC0 assets, build the courtyard environment with walls/floor/archways/fountain/torches, set up lighting (golden-hour directional + shadows + torch point lights), skybox, particle effects (fire, dust), spatial audio, and enemy encounters. The player should be able to walk through, fight guardians, solve the push-block puzzle, and collect the artifact.

---

## 2026-03-07 — Session 66: Editor Crash Fix, macOS CI, README Overhaul, Phase 6 ADR

### Summary

Session 66 fixed the editor crash caused by ImGui key handling migration, repaired macOS CI for LuaJIT on arm64, overhauled the README to reflect the full engine (all 5 phases), and produced the Phase 6 "Echoes of the Ancients" ADR. FAST build: 991 tests pass on Clang-18, zero warnings.

### Planned

- Fix editor crash (ImGui key handling)
- Fix macOS CI (LuaJIT arm64)
- Overhaul README
- Design Phase 6 showcase game

### Delivered

- **Editor crash fix** -- Migrated ShortcutManager from legacy int key indices to ImGuiKey enums. Fixed GLFW callback overwrite in editor_app. Updated `engine/core/input.h/cpp`, `engine/editor/editor.cpp`, `editor/editor_app.cpp`, `editor/input/shortcut_manager.h/cpp`, `tests/editor/test_shortcuts.cpp`.
- **macOS CI fix** -- LuaJIT vcpkg overlay port corrected for arm64-osx cross-compilation. Updated `cmake/vcpkg-overlays/ports/luajit/portfile.cmake` and `configure`.
- **README overhaul** -- Comprehensive rewrite reflecting 991 tests, ~167 Lua bindings, editor, networking, website, all 5 phases, hardware tier system.
- **Phase 6 ADR** -- "Echoes of the Ancients" 3D showcase game: 3-level action-exploration, 8-10 session estimate, linear fog as only engine enhancement. Written to `docs/architecture/adr-phase6-showcase.md`.
- **Doc fixes** -- `editor/.context.md` (ShortcutManager ImGuiKey), `docs/architecture-map.md` (binding count), `docs/environment.md` (diagnostics).

### Reviews

- performance-critic: PASS
- api-designer: PASS (minor issues fixed in remediation)
- security-auditor: SKIPPED (no attack surface changes)
- game-dev-tester: SKIPPED (no new API paradigms)

### User Directives for Autonomous Development

The user has provided long-term directives for continuous autonomous development:
1. Don't stop after Phase 6 -- keep planning and improving
2. GitHub site should have polished tutorials with navigation
3. Cool demos after each phase
4. Sample for each hardware tier
5. Competitive analysis: what do competitors offer that FFE doesn't?
6. Xbox controller support in game demos
7. Utilize GitHub's free features for open source
8. After each phase, PM updates README with new screenshots

### Next Session

Session 67: Phase 6 M1 -- Linear fog shader + showcase project scaffold + player controller prototype. Per ADR Section 8, fog is the only engine C++ change; the rest is Lua game code scaffolding.

### Devlog Maintenance

Archived Sessions 35-50 to `docs/devlog-archive.md` (now covers Sessions 1-50).

---

## 2026-03-07 — Session 51: Phase 3 Kickoff — Standalone Editor Milestone 1

### Summary

Session 51 kicked off Phase 3 (Standalone Editor). Delivered Milestone 1: a fully functional editor scaffold with ImGui, scene serialisation, inspector panel, scene hierarchy, and an undo/redo command system. FULL build passed: 766 tests on both Clang-18 and GCC-13, zero warnings.

### New Subsystems

- **Editor application** (`editor/`) — separate binary from game runtime. ImGui dockspace layout with menu bar, panels for scene hierarchy, inspector, and viewport (placeholder). Links against engine and ImGui.
- **Scene serialisation** (`engine/scene/`) — `SceneSerialiser` with JSON save/load. Security hardening: entity count limits, NaN/Inf rejection, path traversal rejection, file size limits.
- **Editor-hosted mode** — `Application` gained `initSubsystems()`, `shutdownSubsystems()`, `tickOnce()`, `renderOnce()`, `setWindow()` for editor control of the engine lifecycle.

### Editor Features

- **Scene hierarchy panel** — lists all entities by Name component (or "Entity N" fallback), click-to-select, right-click context menu for create/delete
- **Inspector panel** — editable Transform/Transform3D/Name fields, display-only Sprite/Material3D. Wired to command system for undo.
- **Command system** — `CommandHistory` with 256-depth bounded deque, `ICommand` interface with execute/undo. Entity create/destroy commands snapshot all components for full undo fidelity.
- **Viewport panel** — placeholder ready for FBO rendering in Milestone 2

### ECS Additions

- `Name`, `Parent`, `Children` components added to `render_system.h` for scene graph support

### Architecture

- ADR: `docs/architecture/adr-editor-architecture.md` — documents editor-hosted mode, panel architecture, command pattern, serialisation security model

### Tests

- 28 new tests across `tests/editor/` and `tests/scene/`
- **766 tests** total, passing on both Clang-18 and GCC-13, zero warnings

### Documentation

- 3 `.context.md` files: `editor/.context.md`, `engine/scene/.context.md`, updated `engine/core/.context.md`

### Reviews

- performance-critic: MINOR ISSUES (approved) — no blocking concerns
- security-auditor: MINOR ISSUES (approved) — serialisation security model solid
- api-designer: clean
- game-dev-tester: SKIP — editor-hosted mode API is internal to editor, not a new game developer-facing paradigm. Will invoke when play-in-editor is implemented.

### Build

- FULL build: 766 tests on Clang-18 + GCC-13, zero warnings, zero failures

---

