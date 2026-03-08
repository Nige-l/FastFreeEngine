# FastFreeEngine — Subsystem Status

> Honest assessment of each subsystem as of 2026-03-08.
> 1511 total tests. Ratings reflect actual test coverage, demo usage, and known gaps.

## Rating Definitions

| Rating | Meaning |
|--------|---------|
| **Production-ready** | Thorough test coverage, battle-tested in demos, API stable |
| **Tested** | Has unit tests, works in demos, API may evolve |
| **Demo-quality** | Works in demos but limited test coverage or known gaps |
| **Experimental** | Partially implemented, not fully tested |
| **Planned** | Defined in roadmap but not yet started |

---

## Core

### ECS (EnTT wrapper)
- **Rating:** Production-ready
- **Tests:** ~10 (test_ecs.cpp) + used implicitly by nearly every other test
- **Deps:** EnTT (vcpkg)
- **Notes:** Thin wrapper over EnTT. Stable API since Phase 1. Every demo and subsystem depends on it. Low direct test count but exercised heavily through integration.

### Application Loop
- **Rating:** Tested
- **Tests:** ~5 (test_application.cpp) + ~44 (test_renderer_headless.cpp)
- **Deps:** GLFW, ECS
- **Notes:** Headless mode well-tested. Fixed timestep, command-line arg parsing. No known gaps.

### Input System
- **Rating:** Production-ready
- **Tests:** ~49 (test_input.cpp) + ~15 (test_gamepad.cpp)
- **Deps:** GLFW
- **Notes:** Keyboard, mouse, and gamepad all covered. Gamepad deadzone support. Used by all 6 demos.

### Arena Allocator
- **Rating:** Tested
- **Tests:** ~13 (test_arena_allocator.cpp)
- **Deps:** None
- **Notes:** Scratch allocator for per-frame transient data. API stable. Not yet widely adopted in hot paths (most subsystems still use standard allocators).

### Logging
- **Rating:** Tested
- **Tests:** ~9 (test_logging.cpp)
- **Deps:** None
- **Notes:** Simple logging system. Stable.

### Types / Platform
- **Rating:** Tested
- **Tests:** ~8 (test_types.cpp)
- **Deps:** None
- **Notes:** Type aliases, platform path canonicalization. Cross-platform guards for Windows/Linux/macOS.

### Particles (2D)
- **Rating:** Tested
- **Tests:** ~10 (test_particles.cpp)
- **Deps:** ECS, Renderer
- **Notes:** 128 inline particle pool per emitter. Gravity, color/size interpolation. Used in Breakout demo.
- **Known gap:** No 3D particle system. Showcase game works around this with billboard geometry.

---

## Renderer (2D)

### Sprite Batching 2.0
- **Rating:** Tested
- **Tests:** ~44 (test_renderer_headless.cpp covers sprite rendering paths)
- **Deps:** RHI, Texture Atlas
- **Notes:** Runtime texture atlas with shelf packing. Rotation, flip, color tint. Used by 4 of 6 demos. API stable since Phase 7 M7 rewrite.

### Texture Atlas
- **Rating:** Production-ready
- **Tests:** ~28 (test_texture_atlas.cpp)
- **Deps:** RHI
- **Notes:** 2048x2048 atlas, shelf packing, lazy packing, UV remapping. Well-tested.

### Texture Loading
- **Rating:** Tested
- **Tests:** ~24 (test_texture_loader.cpp)
- **Deps:** stb_image (vendored)
- **Notes:** PNG/JPG/BMP loading. Path traversal prevention. Headless-safe.

### Text Rendering
- **Rating:** Tested
- **Tests:** ~24 (test_ttf_font.cpp)
- **Deps:** stb_truetype (vendored)
- **Notes:** Bitmap 8x8 font + TTF (8 font slots). Used in all demos for HUD text.

---

## Renderer (3D)

### Mesh Loading
- **Rating:** Tested
- **Tests:** ~29 (test_mesh_loader.cpp)
- **Deps:** cgltf (vendored)
- **Notes:** glTF/GLB loading. Handle-based resource management (256 max meshes). Used in 3D Demo and Showcase.
- **Known gap:** `findHandleByPath` missing -- prefab Mesh components need manual resolve after instantiation.

### Mesh Renderer (Blinn-Phong)
- **Rating:** Tested
- **Tests:** ~17 (test_point_lights_materials.cpp) + covered by 3D demo tests
- **Deps:** RHI, Shader Library
- **Notes:** Directional + 4 point lights, specular/normal maps, Material3D component. Stable.

### PBR Materials
- **Rating:** Tested
- **Tests:** ~16 (test_pbr_material.cpp)
- **Deps:** RHI, Shader Library
- **Notes:** Cook-Torrance BRDF, metallic-roughness workflow, IBL. Used in Showcase game. API stable since Phase 7 M1.

### Shadow Mapping
- **Rating:** Tested
- **Tests:** ~9 (test_shadow_map.cpp)
- **Deps:** RHI, Mesh Renderer
- **Notes:** Directional shadow map, depth FBO, PCF 3x3 filtering. Instanced shadow pass. Works but limited: single directional light only, no cascaded shadows, fixed shadow area.

### Skybox
- **Rating:** Demo-quality
- **Tests:** ~4 (test_skybox.cpp)
- **Deps:** RHI, Shader Library
- **Notes:** Cubemap loading (6 faces), dedicated shader. Low test count. Works in 3D Demo. API simple and stable.

### Skeletal Animation
- **Rating:** Tested
- **Tests:** ~42 (test_skeleton.cpp)
- **Deps:** cgltf, Mesh Renderer
- **Notes:** Crossfade blending, STEP/LINEAR/CUBIC_SPLINE interpolation, root motion extraction, 64 max bones. Good test coverage. Used in Showcase game.

### Post-Processing
- **Rating:** Tested
- **Tests:** ~30 (test_post_process.cpp)
- **Deps:** RHI
- **Notes:** HDR FBO (GL_RGBA16F), bloom (half-res ping-pong Gaussian), tone mapping (Reinhard/ACES), gamma correction. Well-tested. Used in Showcase game.

### GPU Instancing
- **Rating:** Tested
- **Tests:** ~21 (test_gpu_instancing.cpp)
- **Deps:** RHI, Mesh Renderer
- **Notes:** Automatic batching by MeshHandle, 1024 instances/batch. Instanced shadow pass. Used by vegetation system. Solid.

### Anti-Aliasing (MSAA + FXAA)
- **Rating:** Tested
- **Tests:** ~22 (test_anti_aliasing.cpp)
- **Deps:** RHI, Post-Processing
- **Notes:** MSAA (2x/4x/8x) via multisample FBO + FXAA 3.11 post-process. Both can be combined.

### SSAO
- **Rating:** Tested
- **Tests:** ~26 (test_ssao.cpp)
- **Deps:** RHI, Post-Processing
- **Notes:** 32-sample hemisphere, half-res, 4x4 box blur. LEGACY compatible (GLSL 330). Used in Showcase.

### Fog
- **Rating:** Tested
- **Tests:** ~14 (test_fog.cpp)
- **Deps:** Shader Library
- **Notes:** Atmospheric distance fog. Used in Showcase game.

### Frustum Culling
- **Rating:** Tested
- **Tests:** ~8 (test_frustum.cpp)
- **Deps:** Camera
- **Notes:** Used by terrain LOD system.

### Camera
- **Rating:** Demo-quality
- **Tests:** No dedicated test file (tested through scripting bindings: ~29 in test_camera3d_bindings.cpp)
- **Deps:** Core
- **Notes:** FPS, orbit, and free camera modes. Works in all 3D demos.
- **Known gap:** `set3DCameraFPS` lacks NaN/Inf guards (minor, backlog item).

---

## Renderer (Terrain / Open World)

### Terrain System
- **Rating:** Tested
- **Tests:** ~26 (test_terrain.cpp) + ~21 (test_terrain_streaming.cpp)
- **Deps:** RHI, Mesh Renderer, stb_image
- **Notes:** Heightmap terrain (raw float + PNG), chunked mesh, bilinear height queries, RGBA splat map, triplanar projection, 3 LOD levels, frustum culling. Solid coverage.

### World Streaming
- **Rating:** Tested
- **Tests:** ~21 (test_terrain_streaming.cpp)
- **Deps:** Terrain, threading (std::thread)
- **Notes:** ChunkState machine, background worker thread, GL upload gate, dirty-distance gating.

### Vegetation
- **Rating:** Tested
- **Tests:** ~27 (test_vegetation.cpp)
- **Deps:** GPU Instancing, Terrain
- **Notes:** Billboard grass (256/chunk, 24B/instance), tree placement (512 trees, 16B/instance), VEGETATION shader with alpha test and distance fade.

### Water Rendering
- **Rating:** Tested
- **Tests:** ~56 (test_water.cpp)
- **Deps:** RHI, Camera
- **Notes:** Reflection FBO (half-res), Fresnel blend, animated UV scroll. Highest test count of any renderer subsystem.

---

## Renderer (Infrastructure)

### RHI (OpenGL 3.3)
- **Rating:** Tested
- **Tests:** ~44 (test_renderer_headless.cpp covers RHI through rendering paths)
- **Deps:** GLFW, glad
- **Notes:** Buffer/texture/shader/uniform/draw abstraction. Supports FBOs, multisample FBOs, instanced draw. Workhorse of the engine.

### RHI (Vulkan)
- **Rating:** Experimental
- **Tests:** ~59 (test_rhi_vulkan.cpp)
- **Deps:** volk, VMA, glslc (build-time SPIR-V)
- **Notes:** Compile-time `FFE_BACKEND` selection. Instance/device/swapchain, VMA memory, SPIR-V graphics pipeline, resource manager, depth buffer, Blinn-Phong shader. Does NOT support shadows, post-processing, instancing, PBR, skeletal animation, terrain, or features added after Phase 8.

### Shader Library
- **Rating:** Tested
- **Tests:** Tested indirectly through subsystem tests (no dedicated test file)
- **Deps:** RHI
- **Notes:** 16 GLSL 330 shaders inline in shader_library.cpp, plus 6 SPIR-V shaders for Vulkan.

---

## Physics

### 2D Collision System
- **Rating:** Production-ready
- **Tests:** ~37 (test_narrow_phase.cpp)
- **Deps:** ECS
- **Notes:** Spatial hash broadphase, AABB/Circle narrow phase, layer/mask filtering, collision callbacks. Used in Pong, Breakout, Collect Stars, Net Arena.

### 3D Physics (Jolt)
- **Rating:** Tested
- **Tests:** ~18 (test_physics3d.cpp) + ~7 (test_physics3d_collisions.cpp) + ~7 (test_physics3d_raycast.cpp)
- **Deps:** Jolt Physics (vcpkg)
- **Notes:** Rigid bodies, collision detection, raycasting. Used in Showcase game.

---

## Audio

### Audio System
- **Rating:** Tested
- **Tests:** ~29 (test_audio.cpp) + ~14 (test_audio_3d.cpp)
- **Deps:** miniaudio (vendored)
- **Notes:** WAV/OGG playback, sound/music channels, headless mode for CI. 3D positional audio. Stable API.

---

## Scripting

### Lua Scripting Engine
- **Rating:** Production-ready
- **Tests:** ~217 (test_lua_sandbox.cpp) + ~260 across binding-specific test files
- **Deps:** LuaJIT + sol2 (vcpkg)
- **Notes:** ~225 `ffe.*` bindings. Sandboxed: instruction budget, blocked globals (os, io, loadfile, dofile), path traversal prevention. Highest test concentration in the project. Every demo is a Lua script.

---

## Networking

### Transport / Server-Client
- **Rating:** Tested
- **Tests:** ~8 (test_transport.cpp) + ~14 (test_server_client.cpp)
- **Deps:** ENet (vcpkg)
- **Notes:** UDP transport, reliable/unreliable channels.

### Replication
- **Rating:** Tested
- **Tests:** ~24 (test_replication.cpp)
- **Deps:** ECS, Transport
- **Notes:** Snapshot-based entity replication.

### Prediction / Lag Compensation
- **Rating:** Tested
- **Tests:** ~15 (test_prediction.cpp) + ~15 (test_lag_compensation.cpp)
- **Deps:** ECS, Replication
- **Notes:** Client-side prediction with server reconciliation.

### Lobby System
- **Rating:** Tested
- **Tests:** ~20 (test_lobby.cpp)
- **Deps:** Transport
- **Notes:** Create/join/leave lobby, ready state, game start.

### Packet System
- **Rating:** Tested
- **Tests:** ~22 (test_packet.cpp)
- **Deps:** None
- **Notes:** Bounds-checked reads, validated length fields. Security-reviewed.
- **Known gap:** NAT traversal / relay server not implemented (deferred to backlog).

---

## Scene

### Scene Serialisation
- **Rating:** Tested
- **Tests:** ~10 (test_scene_serialiser.cpp)
- **Deps:** ECS, nlohmann/json
- **Notes:** JSON save/load, entity count limits, NaN rejection. Security-reviewed.

### Scene Graph
- **Rating:** Tested
- **Tests:** ~12 (test_scene_graph.cpp)
- **Deps:** ECS
- **Notes:** Parent-child entity hierarchy. Used by editor.

---

## Editor

### Editor Application
- **Rating:** Tested
- **Tests:** ~96 across 9 test files
- **Deps:** ImGui (vcpkg), Core, Renderer
- **Notes:** Standalone application with dockspace layout: hierarchy panel, inspector, viewport with gizmos, build pipeline/exporter, prefab system, visual scripting (11 node types), LLM integration panel.

### Prefab System
- **Rating:** Tested
- **Tests:** ~21 (test_prefab_system.cpp)
- **Deps:** ECS, JSON, Scene Serialisation
- **Notes:** JSON-based prefab loading, ECS instantiation, override support. Security hardened.

### Visual Scripting
- **Rating:** Tested
- **Tests:** ~42 (test_visual_scripting.cpp)
- **Deps:** ECS, Editor
- **Notes:** 11 built-in node types, topological sort execution, ImGui graph editor with bezier connections.

### LLM Integration Panel
- **Rating:** Experimental
- **Tests:** ~18 (test_llm_panel.cpp)
- **Deps:** cpp-httplib (vendored)
- **Notes:** Async HTTP to LLM API, context-aware queries using .context.md files, Lua snippet insertion. Requires external LLM API key. `#ifdef FFE_EDITOR` guarded.

---

## Summary Table

| Subsystem | Rating | Tests (approx) | Key Gap |
|-----------|--------|----------------|---------|
| ECS | Production-ready | 10+ | None |
| Application | Tested | 49 | None |
| Input | Production-ready | 64 | None |
| Arena Allocator | Tested | 13 | Not widely adopted yet |
| 2D Sprite Batching | Tested | 44 | None |
| Texture Atlas | Production-ready | 28 | None |
| Text Rendering | Tested | 24 | None |
| Mesh Loading | Tested | 29 | Missing findHandleByPath |
| PBR Materials | Tested | 16 | None |
| Shadow Mapping | Tested | 9 | No cascaded shadows |
| Skybox | Demo-quality | 4 | Low test count |
| Skeletal Animation | Tested | 42 | None |
| Post-Processing | Tested | 30 | None |
| GPU Instancing | Tested | 21 | None |
| Anti-Aliasing | Tested | 22 | None |
| SSAO | Tested | 26 | None |
| Terrain | Tested | 47 | None |
| Vegetation | Tested | 27 | None |
| Water | Tested | 56 | None |
| RHI (OpenGL) | Tested | 44 | None |
| RHI (Vulkan) | Experimental | 59 | Missing most features vs OpenGL |
| 2D Collision | Production-ready | 37 | None |
| 3D Physics | Tested | 32 | Less battle-tested |
| Audio | Tested | 43 | None |
| Lua Scripting | Production-ready | 477+ | None |
| Networking | Tested | 118 | No NAT traversal |
| Scene | Tested | 22 | None |
| Editor | Tested | 96 | Not production-used |
| Prefab System | Tested | 21 | None |
| Visual Scripting | Tested | 42 | New, may evolve |
| LLM Panel | Experimental | 18 | Requires external API |
| 3D Particles | Planned | 0 | Not implemented |
