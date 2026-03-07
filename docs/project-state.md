# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 88 |
| Total tests | 1234 (Clang-18, zero warnings) |
| Total Lua bindings | ~190 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | MVP COMPLETE (6 milestones, Sessions 51-56) |
| Phase 4 (Networking) | COMPLETE (Sessions 57-60) |
| Phase 5 (Website/Learning) | COMPLETE (Sessions 62-65) |
| Phase 6 (Showcase Game) | COMPLETE (Sessions 66-73) |
| Phase 7 (Rendering Pipeline) | COMPLETE (Sessions 74-84) |
| Phase 8 (Vulkan Backend) | IN PROGRESS (Session 85+, M4 done) |
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
| 88 | Phase 8 M4: Vulkan Mesh Rendering — resource manager (handle pools), full RHI impl (buffers/textures/shaders/uniforms/draw), Blinn-Phong SPIR-V, SceneUBO+LightUBO. 17 new tests. 1234 tests (FAST). |
| 87 | Phase 8 M3: Vulkan Textures + Uniform Buffers — VkImage/VMA, descriptors, MVPUniform, textured quad rendering. 10 new tests. 1234 tests (FAST). |
| 86 | Phase 8 M2: Vulkan Shader Compilation + Vertex Buffers — VMA integration, SPIR-V shaders, graphics pipeline, staged buffer uploads, triangle rendering. 13 new tests. 1234 tests (FAST). |
| 85 | Phase 8 M1: Vulkan Backend Bootstrap — compile-time FFE_BACKEND selection, volk loader, instance/device/swapchain init, RHI Vulkan impl (clear-color), tier validation. 6 new tests. 1234 tests (FAST). |
| 84 | Phase 7 M8: Phase Close — README update, architecture-map update, GCC-13 strncpy warning fix. FULL build: 1228 tests. Phase 7 COMPLETE. |

## Phase 8 — Vulkan Backend (IN PROGRESS)

**Goal:** Add a Vulkan rendering backend alongside OpenGL, targeting STANDARD and MODERN tiers. ADR: `docs/architecture/adr-phase8-vulkan-backend.md`

### Milestones

- [x] M1 (Session 85): Vulkan Backend Bootstrap -- compile-time FFE_BACKEND selection, volk function loader, Vulkan instance/device/swapchain init (FIFO, 2 frames in flight), RHI Vulkan impl (beginFrame/endFrame clear-color, stubs for rest), tier/backend validation (Vulkan requires STANDARD+), 6 CPU-only tests.
- [x] M2 (Session 86): Vulkan Shader Compilation + Vertex Buffers -- VMA integration, embedded SPIR-V shaders (constexpr u32 arrays), VkManagedBuffer (staging upload), VkManagedShader, PipelineConfig + createGraphicsPipeline (dynamic viewport/scissor), colored triangle rendering. 13 new CPU-only tests.
- [x] M3 (Session 87): Vulkan Textures + Uniform Buffers -- VkImage/VkImageView/VkSampler via VMA, descriptor sets/pools (UBO + sampler), MVPUniform (192B per-frame host-coherent), embedded SPIR-V textured quad shaders, orthographic MVP, indexed draw with checkerboard texture. 10 new CPU-only tests.
- [x] M4 (Session 88): Vulkan Mesh Rendering -- vk_resource_manager (256 buffer/texture, 32 shader handle pools, 1-based handles), full RHI impl (createBuffer/updateBuffer/destroyBuffer, createTexture/destroyTexture/bindTexture, createShader/destroyShader/useShader, setUniformMat4/Vec3/Float/Int via FNV-1a hash, drawArrays/drawIndexed), SceneUBO (256B) + LightUBO (64B), Blinn-Phong SPIR-V headers (placeholder, glslc TODO), render pass restructured for multi-draw-call frames. 17 new CPU-only tests.
- [ ] M5: Vulkan Depth Buffer + Build-Time SPIR-V -- depth buffer attachment, build-time glslc compilation, full Blinn-Phong lighting, phase close (FULL build).

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
