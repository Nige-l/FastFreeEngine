# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 51 |
| Total tests | 766 (Clang-18 + GCC-13 passing, zero warnings) |
| Total Lua bindings | ~115 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Phase 3 (Standalone Editor) | IN PROGRESS — Milestone 1 delivered |
| Windows build | DONE (MinGW-w64 cross-compilation) |
| macOS build | DONE (arm64 + x86_64) |
| GitHub Actions CI | DONE (Linux Clang-18, Linux GCC-13, macOS arm64) |

## Engine Subsystems

| Subsystem | Status | Key APIs |
|-----------|--------|----------|
| ECS | Stable | World, createEntity/destroyEntity, function-pointer systems |
| Renderer (2D) | Stable | OpenGL 3.3, sprite batching (2048/batch), render queue, rotation+flip |
| Renderer (3D) | Stable | cgltf .glb, Blinn-Phong, Transform3D/Mesh/Material3D, point lights (4 max), specular/normal maps, skeletal animation |
| Shadow Mapping | Stable | Depth FBO, PCF 3x3, enableShadows/disableShadows/setShadowBias/setShadowArea |
| Skybox | Stable | Cubemap loading (6-face), SKYBOX shader, loadSkybox/unloadSkybox/setSkyboxEnabled |
| Text (bitmap) | Stable | 8x8 bitmap font atlas, drawText, drawRect |
| Text (TTF) | Stable | stb_truetype, loadFont/drawFontText/measureText, 8 font slots |
| Sprite Animation | Stable | SpriteAnimation component, grid atlas |
| Tilemap | Stable | Tilemap component, renderTilemaps, 1024x1024 max |
| Particles | Stable | ParticleEmitter, 128 inline pool, gravity/color/size interp |
| Scene Serialisation | Stable | SceneSerialiser — saveScene/loadScene (JSON), entity count limits, NaN rejection, path traversal rejection |
| Editor | Milestone 1 | Standalone binary, ImGui dockspace, scene hierarchy, inspector, undo/redo commands, viewport placeholder |
| Scene Mgmt | Stable | destroyAllEntities, cancelAllTimers, loadScene |
| Input | Stable | keyboard+mouse+gamepad, pressed/held/released, action bindings |
| Audio | Stable | miniaudio, WAV/OGG, playSound/playMusic, streaming, headless, 3D positional (playSound3D, listener sync) |
| Physics (3D) | Stable | Jolt Physics, rigid bodies, collision callbacks, raycasting, entity-body mapping, 13 Lua bindings |
| Collision | Stable | Spatial hash, AABB/Circle, layer/mask, CollisionEvent |
| Scripting | Stable | LuaJIT sandbox, 1M instruction budget, ffe.* API |
| Save/Load | Stable | JSON on disk, path security, atomic writes |
| Timers | Stable | ffe.after/every/cancelTimer, 256 max, fixed-size array |
| Camera | Stable | CameraShake, ClearColor, set3DCameraFPS/Orbit |
| Screenshot | Stable | glReadPixels, PNG via stb_image_write, ffe.screenshot |

## Demo Games

1. **Collect Stars** — top-down 2D, Lua-only
2. **Pong** — classic 2D, Lua-only
3. **Breakout** — brick-breaker 2D, Lua-only
4. **3D Demo** — mesh loading, Blinn-Phong, point lights, materials, shadows, skybox

## Known Issues / Deferred Items

- UNC path (`\\server\share`) blocking on Windows — comment-only, no test env
- MSVC native build support — deferred (MinGW cross-compile works)
- set3DCameraFPS lacks NaN/Inf guards (MINOR — set3DCameraOrbit has them; backlog)

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 51 | Phase 3 kickoff — standalone editor scaffold, scene serialisation, inspector, undo/redo commands. 766 tests. Milestone 1 delivered. |
| 50 | Text flicker fix (fixed-timestep gating), macOS CI fix (LINK_GROUP + vcpkg pin), 3 new TTF tests, README update. 738 tests. |
| 49 | 3D physics gameplay layer — collision callbacks, raycasting, entity mapping, 5 Lua bindings, 735 tests. Phase 2 COMPLETE. |
| 48 | 3D physics foundation — Jolt integration, rigid bodies, ECS sync, 8 Lua bindings, 711 tests |
| 47 | 3D positional audio (spatial voices, listener sync, 4 Lua bindings), 686 tests |

## Phase 2 — 3D Foundation: COMPLETE

**Goal:** Extend the renderer to support 3D games while keeping full 2D capability.

All deliverables met:
- [x] 3D mesh loading (cgltf, .glb format)
- [x] 3D mesh rendering (Blinn-Phong, vertex/index buffers)
- [x] Basic lighting (directional + point lights, Blinn-Phong)
- [x] 3D camera (FPS and orbit modes)
- [x] Materials system (diffuse, specular, normal maps, shininess)
- [x] Skeletal animation (bone hierarchy, GPU skinning)
- [x] 3D physics integration (Jolt — rigid bodies, collision callbacks, raycasting)
- [x] Skybox / cubemap environment rendering
- [x] Shadow mapping (depth FBO, PCF 3x3)
- [x] 3D positional audio (spatial voices, listener sync)
- [x] Lua bindings for all 3D features (~45 bindings)
- [x] 3D demo game
- [x] .context.md files for all new subsystems

## Current Phase: Phase 3 — Standalone Editor (IN PROGRESS)

**Goal:** A graphical application for building games with FFE, like Unity or Unreal Editor.

### Deliverables
- [x] Standalone editor application (separate binary from the game runtime) — Milestone 1
- [x] Scene serialisation (save/load scene files) — Milestone 1
- [x] Entity inspector (create, modify, delete entities and components) — Milestone 1 (basic)
- [x] Undo/redo system — Milestone 1 (entity create/destroy; component mods next)
- [ ] Scene view with 2D and 3D viewport (FBO rendering)
- [ ] Play-in-editor (run the game inside the editor viewport)
- [ ] File dialogs for Open/Save Scene
- [ ] Asset browser (textures, audio, scripts, meshes)
- [ ] Build pipeline (export game as standalone executable)
- [ ] Project creation wizard
- [ ] LLM integration panel (connect AI assistant, generate code, explain systems)

### Next Session (52) — Milestone 2: FBO viewport rendering, component-modify commands, file dialogs

## Build Commands

```bash
# Clang-18 (FAST — default)
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
| Roadmap (archival) | `docs/ROADMAP.md` (full phases — read only for phase transitions) |
| Devlog (recent) | `docs/devlog.md` (Sessions 35+) |
| Devlog (archive) | `docs/devlog-archive.md` (Sessions 1-34) |
| Project state | `docs/project-state.md` (this file) |
| Architecture map | `docs/architecture-map.md` (subsystems, files, Lua bindings, ECS components) |
| Environment log | `docs/environment.md` |
| Agent definitions | `.claude/agents/*.md` |
