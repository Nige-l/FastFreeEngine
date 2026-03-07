# Architecture Map

> Quick-reference for planning agents. WHAT exists and WHERE — not HOW it works.
> Under 200 lines. No prose. Updated by `director` when subsystems are added.

## 1. Directory Structure

```
engine/
  core/             — App loop, ECS (EnTT), input, timers, platform, logging, arena allocator  [engine-dev]
  renderer/         — 2D sprite batching, 3D mesh rendering, shadows, text, textures, camera   [renderer-specialist]
  renderer/opengl/  — OpenGL 3.3 RHI backend, GL debug utilities                               [renderer-specialist]
  audio/            — miniaudio backend, WAV/OGG, sound/music playback, headless mode           [engine-dev]
  physics/          — 2D collision: spatial hash, AABB/Circle, layer/mask, callbacks            [engine-dev]
  scripting/        — LuaJIT sandbox, ffe.* Lua API (~128 bindings), timer system               [engine-dev + api-designer]
  networking/       — ENet transport, replication, server/client, network system module          [engine-dev]
  editor/           — Debug overlay (HUD text, FPS) — future standalone editor                  [engine-dev]
tests/
  core/             — ECS, input, timer, platform tests                                        [engine-dev]
  renderer/         — Sprite batch, render queue, text, mesh, shadow tests                     [engine-dev]
  audio/            — Audio playback tests (headless)                                          [engine-dev]
  physics/          — Collision system, spatial hash tests                                     [engine-dev]
  scripting/        — Lua binding tests, sandbox security tests                                [engine-dev]
  networking/       — Replication, server/client tests                                         [engine-dev]
examples/
  lua_demo/         — Collect Stars (2D top-down)
  pong/             — Pong (2D)
  breakout/         — Breakout (2D brick-breaker)
  3d_demo/          — 3D mesh + Blinn-Phong lighting
  hello_sprites/    — Minimal C++ sprite example
  headless_test/    — Headless mode validation
  interactive_demo/ — C++ interactive test
docs/
  architecture/     — ADRs (architect-owned)
  agents/           — Agent role descriptions (director-owned)
cmake/
  toolchains/       — MinGW cross-compile toolchain
.claude/agents/     — Agent prompt files (director-owned)
.github/workflows/  — CI: Linux Clang-18, Linux GCC-13, macOS arm64
third_party/        — Vendored: cgltf.h (mesh loading)
shaders/legacy/     — External GLSL files (if any; most shaders are inline in shader_library.cpp)
```

## 2. Subsystem Dependency Graph

```
scripting -----> core, renderer, audio, physics, networking  (binds all subsystems to Lua)
networking ---> core        (uses ECS World for replication, Application for lifecycle)
renderer  -----> core        (uses ECS World, Application, input)
audio     -----> core        (uses ECS context for config)
physics   -----> core        (uses ECS World for collision components)
editor    -----> core, renderer  (reads HudTextBuffer from ECS context)
mesh_renderer --> renderer   (3D render pass, uses RHI + shader_library)
shadow_map ----> renderer    (depth FBO, uses mesh_renderer pipeline)
sprite_batch --> renderer    (2D batched draw, uses RHI)
text_renderer -> renderer    (TTF + bitmap font, uses RHI + sprite_batch)
```

Cross-cutting: `core/platform.h` (path canonicalization) used by scripting + renderer.

## 3. Key Files Per Subsystem

| Subsystem | Key files (for dispatch instructions) |
|-----------|---------------------------------------|
| ECS | `engine/core/ecs.h`, `engine/core/system.h`, `engine/core/types.h` |
| Application | `engine/core/application.h`, `engine/core/application.cpp` |
| Input | `engine/core/input.h`, `engine/core/input.cpp` |
| Platform | `engine/core/platform.h` (canonicalizePath, cross-platform guards) |
| RHI | `engine/renderer/rhi.h`, `engine/renderer/rhi_types.h`, `engine/renderer/opengl/rhi_opengl.h` |
| Sprite/2D | `engine/renderer/render_system.h`, `engine/renderer/sprite_batch.h`, `engine/renderer/render_queue.h` |
| 3D Mesh | `engine/renderer/mesh_loader.h`, `engine/renderer/mesh_renderer.h` |
| Shadows | `engine/renderer/shadow_map.h`, `engine/renderer/shadow_map.cpp` |
| Shaders | `engine/renderer/shader_library.h`, `engine/renderer/shader_library.cpp` |
| Text | `engine/renderer/text_renderer.h`, `engine/renderer/font.cpp` |
| Textures | `engine/renderer/texture_loader.h`, `engine/renderer/texture_loader.cpp` |
| Camera | `engine/renderer/camera.h`, `engine/renderer/camera.cpp` |
| Screenshot | `engine/renderer/screenshot.h`, `engine/renderer/screenshot.cpp` |
| Audio | `engine/audio/audio.h`, `engine/audio/audio.cpp` |
| Collision | `engine/physics/collider2d.h`, `engine/physics/collision_system.h` |
| Scripting | `engine/scripting/script_engine.h`, `engine/scripting/script_engine.cpp` |
| Networking | `engine/networking/transport.h`, `engine/networking/packet.h`, `engine/networking/replication.h`, `engine/networking/server.h`, `engine/networking/client.h`, `engine/networking/network_system.h`, `engine/networking/connection.h` |
| Editor | `engine/editor/editor.h`, `engine/editor/editor.cpp` |

## 4. Lua Binding Registry (`ffe.*`)

**Core** (13): `log`, `requestShutdown`, `createEntity`, `destroyEntity`, `getEntityCount`, `destroyAllEntities`, `cameraShake`, `setBackgroundColor`, `getScreenWidth`, `getScreenHeight`, `setHudText`, `after`, `every`

**Transform** (5): `addTransform`, `getTransform`, `setTransform`, `fillTransform`, `addPreviousTransform`

**Sprites** (4): `addSprite`, `setSpriteColor`, `setSpriteSize`, `setSpriteFlip`

**Animation** (5): `addSpriteAnimation`, `playAnimation`, `stopAnimation`, `setAnimationFrame`, `isAnimationPlaying`

**Tilemap** (3): `addTilemap`, `setTile`, `getTile`

**Particles** (6): `addEmitter`, `setEmitterConfig`, `startEmitter`, `stopEmitter`, `emitBurst`, `removeEmitter`

**Collision** (3): `addCollider`, `removeCollider`, `setCollisionCallback`

**Textures** (2): `loadTexture`, `unloadTexture`

**Audio** (8): `playSound`, `loadSound`, `unloadSound`, `playMusic`, `stopMusic`, `setMusicVolume`, `getMusicVolume`, `isMusicPlaying`, `loadMusic`, `setMasterVolume`

**Input** (3 functions + constants): `isKeyHeld`, `isKeyPressed`, `isKeyReleased`, `getMouseX`, `getMouseY`, `isMousePressed`, `isMouseHeld`, `isMouseReleased` + KEY_* + MOUSE_*

**Gamepad** (7 + constants): `isGamepadConnected`, `isGamepadButtonPressed`, `isGamepadButtonHeld`, `isGamepadButtonReleased`, `getGamepadAxis`, `getGamepadName`, `setGamepadDeadzone`, `getGamepadDeadzone` + GAMEPAD_*

**Scene** (2): `loadScene`, `cancelAllTimers`, `cancelTimer`

**Save/Load** (2): `saveData`, `loadData`

**TTF Text** (3): `loadFont`, `unloadFont`, `drawFontText`, `measureText`

**Drawing** (2): `drawText`, `drawRect`

**3D Mesh** (13): `loadMesh`, `unloadMesh`, `createEntity3D`, `setTransform3D`, `fillTransform3D`, `set3DCamera`, `set3DCameraFPS`, `set3DCameraOrbit`, `setMeshColor`, `setMeshTexture`, `setLightDirection`, `setLightColor`, `setAmbientColor`

**Shadows** (4): `enableShadows`, `disableShadows`, `setShadowBias`, `setShadowArea`

**Screenshot** (1): `screenshot`

**Networking** (12): `startServer`, `stopServer`, `isServer`, `connectToServer`, `disconnect`, `isConnected`, `getClientId`, `sendMessage`, `onNetworkMessage`, `onClientConnected`, `onClientDisconnected`, `onConnected`, `onDisconnected`, `setNetworkTickRate`

## 5. ECS Components Registry

| Component | Size | Owner | Header |
|-----------|------|-------|--------|
| Transform | 28B | renderer | `render_system.h` |
| PreviousTransform | 28B | renderer | `render_system.h` |
| Sprite | ~40B | renderer | `render_system.h` |
| SpriteAnimation | 16B | renderer | `render_system.h` |
| Tilemap | ~32B | renderer | `render_system.h` |
| ParticleEmitter | ~10KB (128 inline particles) | renderer | `render_system.h` |
| Transform3D | 44B | renderer | `render_system.h` |
| Mesh | 8B | renderer | `render_system.h` |
| Material3D | 24B | renderer | `render_system.h` |
| Collider2D | ~24B | physics | `collider2d.h` |
| CollisionEventList | ~varies | physics | `collider2d.h` |
| CollisionCallbackRef | ~16B | physics | `collider2d.h` |

**ECS Context singletons** (stored in registry context, not per-entity):

| Singleton | Header |
|-----------|--------|
| ShutdownSignal | `ecs.h` |
| HudTextBuffer (256B) | `ecs.h` |
| CameraShake | `ecs.h` |
| ClearColor | `ecs.h` |
| Camera | `camera.h` |
| SceneLighting3D | `mesh_renderer.h` |
| ShadowConfig | `shadow_map.h` |

## 6. Shader Registry

| Enum | Name | Purpose |
|------|------|---------|
| `SOLID` (0) | Solid color | Untextured geometry (debug, rects) |
| `TEXTURED` (1) | Textured | Basic textured quads |
| `SPRITE` (2) | Sprite | Batched 2D sprites with color tint |
| `MESH_BLINN_PHONG` (3) | 3D Blinn-Phong | 3D mesh rendering with directional light, shadows |
| `SHADOW_DEPTH` (4) | Shadow depth | Depth-only pass for directional shadow mapping |

All shaders are GLSL 330 core. Source is inline in `engine/renderer/shader_library.cpp`.
