# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 91 |
| Total tests | 1269 (Clang-18, zero warnings) |
| Total Lua bindings | ~197 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | MVP COMPLETE (6 milestones, Sessions 51-56) |
| Phase 4 (Networking) | COMPLETE (Sessions 57-60) |
| Phase 5 (Website/Learning) | COMPLETE (Sessions 62-65) |
| Phase 6 (Showcase Game) | COMPLETE (Sessions 66-73) |
| Phase 7 (Rendering Pipeline) | COMPLETE (Sessions 74-84) |
| Phase 8 (Vulkan Backend) | COMPLETE (Sessions 85-89) |
| Phase 9 (Terrain/Open World) | IN PROGRESS (Session 90+) |
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
| 91 | Phase 9 M2: Terrain Texturing — RGBA splat map blending (4 texture layers), triplanar projection for steep surfaces, TERRAIN shader (GLSL 330, Blinn-Phong+shadow+fog), TerrainMaterial struct, 3 Lua bindings, 16 new tests. 1269 tests (FAST). |
| 90 | Phase 9 M1: Heightmap Terrain Rendering — chunked heightmap terrain (raw float + PNG loading), configurable chunk resolution, bilinear height queries, terrain renderer (Blinn-Phong, shadows, fog), 4 Lua bindings, 18 new tests. 1252 tests (FAST). |
| 89 | Phase 8 M5 (PHASE CLOSE): Vulkan depth buffer + build-time SPIR-V — depth attachment (VMA, format selection), pipeline depth state, CMake glslc pipeline, embed_spirv.cmake, 6 GLSL 450 shaders, Blinn-Phong lighting. 10 new tests. FULL build: 1234 tests. Phase 8 COMPLETE. |
| 88 | Phase 8 M4: Vulkan Mesh Rendering — resource manager (handle pools), full RHI impl (buffers/textures/shaders/uniforms/draw), Blinn-Phong SPIR-V, SceneUBO+LightUBO. 17 new tests. 1234 tests (FAST). |
| 87 | Phase 8 M3: Vulkan Textures + Uniform Buffers — VkImage/VMA, descriptors, MVPUniform, textured quad rendering. 10 new tests. 1234 tests (FAST). |

## Phase 8 — Vulkan Backend (COMPLETE, Sessions 85-89)

**Goal:** Add a Vulkan rendering backend alongside OpenGL, targeting STANDARD and MODERN tiers. ADR: `docs/architecture/adr-phase8-vulkan-backend.md`

### Milestones

- [x] M1 (Session 85): Vulkan Backend Bootstrap -- compile-time FFE_BACKEND selection, volk function loader, Vulkan instance/device/swapchain init (FIFO, 2 frames in flight), RHI Vulkan impl (beginFrame/endFrame clear-color, stubs for rest), tier/backend validation (Vulkan requires STANDARD+), 6 CPU-only tests.
- [x] M2 (Session 86): Vulkan Shader Compilation + Vertex Buffers -- VMA integration, embedded SPIR-V shaders (constexpr u32 arrays), VkManagedBuffer (staging upload), VkManagedShader, PipelineConfig + createGraphicsPipeline (dynamic viewport/scissor), colored triangle rendering. 13 new CPU-only tests.
- [x] M3 (Session 87): Vulkan Textures + Uniform Buffers -- VkImage/VkImageView/VkSampler via VMA, descriptor sets/pools (UBO + sampler), MVPUniform (192B per-frame host-coherent), embedded SPIR-V textured quad shaders, orthographic MVP, indexed draw with checkerboard texture. 10 new CPU-only tests.
- [x] M4 (Session 88): Vulkan Mesh Rendering -- vk_resource_manager (256 buffer/texture, 32 shader handle pools, 1-based handles), full RHI impl (createBuffer/updateBuffer/destroyBuffer, createTexture/destroyTexture/bindTexture, createShader/destroyShader/useShader, setUniformMat4/Vec3/Float/Int via FNV-1a hash, drawArrays/drawIndexed), SceneUBO (256B) + LightUBO (64B), Blinn-Phong SPIR-V headers (placeholder, glslc TODO), render pass restructured for multi-draw-call frames. 17 new CPU-only tests.
- [x] M5 (Session 89): Vulkan Depth Buffer + Build-Time SPIR-V -- VkImage depth attachment (VMA, D32_SFLOAT/D32_SFLOAT_S8/D24_UNORM_S8 format selection), PipelineConfig depth state, CMake glslc/glslangValidator pipeline (embed_spirv.cmake, constexpr u32 arrays), 6 GLSL 450 shader source files (triangle/textured/blinn_phong vert+frag), full Blinn-Phong directional lighting. 10 new tests. FULL build: 1234 tests, zero warnings, Clang-18 + GCC-13. **PHASE 8 COMPLETE.**

## Phase 9 — Terrain and Open World (IN PROGRESS, Session 90+)

**Goal:** Large-scale outdoor environment support with terrain rendering, LOD, and streaming. ADR: `docs/architecture/adr-phase9-terrain.md`. See `docs/ROADMAP.md` for full deliverables.

### Milestones

- [x] M1 (Session 90): Heightmap Terrain Rendering -- TerrainHandle, TerrainConfig, raw float + PNG heightmap loading (stb_image), chunked mesh generation (configurable resolution up to 128x128 per chunk), normal computation via finite differences, bilinear height queries, path security, NaN rejection, terrain renderer (MESH_BLINN_PHONG, directional + point lights, shadows, fog), Terrain ECS component (8B), 4 Lua bindings, 18 new tests.
- [x] M2 (Session 91): Terrain Texturing -- RGBA splat map (4 texture layers), triplanar projection for steep surfaces (threshold-gated), TerrainMaterial struct (splatTexture + 4 TerrainLayers with uvScale), TERRAIN shader (GLSL 330 core, Blinn-Phong + shadow PCF + point lights + fog + splat blending + triplanar), 3 Lua bindings (setTerrainSplatMap, setTerrainLayer, setTerrainTriplanar), 16 new tests.
- [ ] M3: Terrain LOD -- distance-based chunk detail levels, seamless stitching between LOD levels.
- [ ] M4: World Streaming -- chunk loading/unloading based on camera position, async loading.

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
