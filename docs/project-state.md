# FastFreeEngine — Project State

> **Updated each session by `project-manager`.** This is the quick-context document agents read instead of the full devlog. Under 100 lines. Authoritative for current state.

## Current Status

| Metric | Value |
|--------|-------|
| Last session | 41 |
| Total tests | 530 (Clang-18 passing) |
| Total Lua bindings | ~80 |
| Phase 1 (2D Foundation) | COMPLETE (Linux) |
| Phase 2 (3D Foundation) | COMPLETE |
| Windows build | DONE (MinGW-w64 cross-compilation) |
| macOS build | DONE (arm64 + x86_64) |
| GitHub Actions CI | DONE (Linux Clang-18, Linux GCC-13, macOS arm64) |
| GCC-13 verification | Pending — run FULL build at start of Session 42 |

## Engine Subsystems

| Subsystem | Status | Key APIs |
|-----------|--------|----------|
| ECS | Stable | World, createEntity/destroyEntity, function-pointer systems |
| Renderer (2D) | Stable | OpenGL 3.3, sprite batching (2048/batch), render queue, rotation+flip |
| Renderer (3D) | Stable | cgltf .glb, Blinn-Phong, Transform3D/Mesh/Material3D, 10 Lua bindings |
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
| Camera | Stable | CameraShake, ClearColor (ECS context) |
| Screenshot | Stable | glReadPixels, PNG via stb_image_write, ffe.screenshot |

## Demo Games

1. **Collect Stars** — top-down 2D, Lua-only
2. **Pong** — classic 2D, Lua-only
3. **Breakout** — brick-breaker 2D, Lua-only
4. **3D Demo** — mesh loading + Blinn-Phong lighting

## Known Issues / Deferred Items

- GCC-13 FULL build verification pending (Session 42)
- UNC path (`\\server\share`) blocking on Windows — comment-only, no test env
- MSVC native build support — deferred (MinGW cross-compile works)

## Recent Sessions (last 5)

| Session | Summary |
|---------|---------|
| 41 | CI fix (vcpkg toolchain, X11 deps), GLAD glReadPixels fix, screenshot tests pass, 530 tests |
| 40 | macOS arm64+x86_64 build, CI workflow, screenshot tool (blocked by GLAD gap) |
| 39 | Windows MinGW-w64 cross-compilation, platform.h canonicalizePath, ADS path fix |
| 38 | 3D Foundation implementation — cgltf, Blinn-Phong, 10 Lua bindings, 519 tests |
| 37 | Phase 2 architecture — ADR-007 3D Foundation design + security shift-left |

## Next Session (42) Should Start With

- FULL build (Clang-18 + GCC-13) to verify GCC-13 still passes after Sessions 40-41 changes
- Phase 2 ROADMAP items: `ffe.set3DCameraFPS`, `ffe.set3DCameraOrbit`, `ffe.setMeshTexture`, UV loading, Blinn-Phong texture uniform

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
| Roadmap | `docs/ROADMAP.md` |
| Devlog (recent) | `docs/devlog.md` (Sessions 36+) |
| Devlog (archive) | `docs/devlog-archive.md` (Sessions 1-35) |
| Project state | `docs/project-state.md` (this file) |
| Environment log | `docs/environment.md` |
| Agent definitions | `.claude/agents/*.md` |
