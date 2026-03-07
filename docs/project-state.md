# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 46 |
| Total tests | 664 (Clang-18 passing, zero warnings) |
| Total Lua bindings | ~106 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | IN PROGRESS (see current phase section below) |
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
| Scene Mgmt | Stable | destroyAllEntities, cancelAllTimers, loadScene |
| Input | Stable | keyboard+mouse+gamepad, pressed/held/released, action bindings |
| Audio | Stable | miniaudio, WAV/OGG, playSound/playMusic, streaming, headless |
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
| 46 | Skeletal animation (bone hierarchy, GPU skinning, 8 Lua bindings), 1.37 GB static array fix, 664 tests |
| 45 | Skybox / cubemap environment rendering, 3 new Lua bindings, security hardening, 627 tests |
| 44 | Point lights (4 max) + materials system (specular, normal maps), 8 new Lua bindings, 618 tests |
| 43 | Shadow mapping (depth FBO, PCF 3x3, 4 Lua bindings), CI improvements (paths-ignore, concurrency), 568 tests |
| 42 | 3D camera modes (FPS/orbit), mesh texture binding, process restructuring, 559 tests |

## Current Phase: Phase 2 — 3D Foundation (IN PROGRESS)

**Goal:** Extend the renderer to support 3D games while keeping full 2D capability.

### Delivered
- [x] 3D mesh loading (cgltf, .glb format)
- [x] 3D mesh rendering (Blinn-Phong, vertex/index buffers)
- [x] Basic lighting (directional, Blinn-Phong)
- [x] 3D camera (FPS and orbit modes)
- [x] Mesh texture binding
- [x] Shadow mapping (depth FBO, PCF 3x3, 4 Lua bindings)
- [x] Point lights (up to 4, with Lua bindings)
- [x] Materials system (specular maps, normal maps, shininess)
- [x] Lua bindings for 3D (28 bindings)
- [x] 3D demo game
- [x] .context.md for 3D renderer
- [x] Skybox / cubemap environment rendering (3 Lua bindings)

### Remaining
- [x] Skeletal animation (bone hierarchy, GPU skinning, 8 Lua bindings)
- [ ] 3D physics integration (Bullet, Jolt, or similar)
- [ ] 3D audio (positional sound sources)

### Next Session (47) — PM to select from remaining items above

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
