# Architecture Map

> Quick-reference for planning agents. WHAT exists and WHERE — not HOW it works.
> Under 200 lines. No prose. Updated by `director` when subsystems are added.

## 1. Directory Structure

```
engine/
  core/             — App loop, ECS (EnTT), input, timers, platform, logging, arena allocator  [engine-dev]
  renderer/         — 2D sprite batching (+atlas), 3D mesh/PBR, post-process, GPU instancing, SSAO, MSAA/FXAA, shadows, text, textures, camera  [renderer-specialist]
  renderer/opengl/  — OpenGL 3.3 RHI backend, GL debug utilities                               [renderer-specialist]
  audio/            — miniaudio backend, WAV/OGG, sound/music playback, headless mode           [engine-dev]
  physics/          — 2D collision: spatial hash, AABB/Circle, layer/mask, callbacks            [engine-dev]
  scripting/        — LuaJIT sandbox, ffe.* Lua API (~189 bindings), timer system               [engine-dev + api-designer]
  networking/       — ENet transport, replication, server/client, prediction, lobby, lag compensation, network system  [engine-dev]
  editor/           — Standalone editor (ImGui dockspace, hierarchy, inspector, viewport, gizmos, build pipeline)  [engine-dev]
tests/
  core/             — ECS, input, timer, platform tests                                        [engine-dev]
  renderer/         — Sprite batch, render queue, text, mesh, shadow tests                     [engine-dev]
  audio/            — Audio playback tests (headless)                                          [engine-dev]
  physics/          — Collision system, spatial hash tests                                     [engine-dev]
  scripting/        — Lua binding tests, sandbox security tests                                [engine-dev]
  networking/       — Replication, server/client tests                                         [engine-dev]
examples/
  showcase/         — "Echoes of the Ancients" (3-level 3D action-exploration)
  lua_demo/         — Collect Stars (2D top-down)
  pong/             — Pong (2D)
  breakout/         — Breakout (2D brick-breaker)
  3d_demo/          — 3D mesh + Blinn-Phong lighting
  net_demo/         — 2D multiplayer arena (client-side prediction)
  hello_sprites/    — Minimal C++ sprite example
  headless_test/    — Headless mode validation
  interactive_demo/ — C++ interactive test
website/            — MkDocs documentation site (tutorials, deep dives, learning track)
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
anim_system --> renderer     (bone matrix computation, uses skeleton.h)
shadow_map ----> renderer    (depth FBO, uses mesh_renderer pipeline)
sprite_batch --> renderer    (2D batched draw, uses RHI + texture_atlas)
text_renderer -> renderer    (TTF + bitmap font, uses RHI + sprite_batch)
post_process --> renderer    (HDR FBO, bloom, tone mapping, gamma, FXAA integration)
gpu_instancing > renderer    (batched instanced draw, uses RHI)
ssao ---------> renderer     (screen-space AO, uses RHI + post_process)
terrain ------> renderer     (chunked heightmap, uses RHI + mesh_renderer pipeline)
terrain_renderer > renderer  (ECS terrain render system, uses MESH_BLINN_PHONG shader)
texture_atlas -> renderer    (runtime shelf-packed sprite atlas, uses RHI)
pbr_material --> renderer    (Cook-Torrance BRDF, metallic-roughness, IBL)
```

Cross-cutting: `core/platform.h` (path canonicalization) used by scripting + renderer.

## 3. Key Files Per Subsystem

| Subsystem | Key files (for dispatch instructions) |
|-----------|---------------------------------------|
| ECS | `engine/core/ecs.h`, `engine/core/system.h`, `engine/core/types.h` |
| Application | `engine/core/application.h`, `engine/core/application.cpp` |
| Input | `engine/core/input.h`, `engine/core/input.cpp` |
| Platform | `engine/core/platform.h` (canonicalizePath, cross-platform guards) |
| RHI | `engine/renderer/rhi.h`, `engine/renderer/rhi_types.h`, `engine/renderer/opengl/rhi_opengl.h` (Phase 7 additions: `setViewportSize`, `getTextureWidth`, `getTextureHeight`, `updateTextureSubImage`, `readTexturePixels`, `createMultisampleFBO`, `blitFBO`) |
| Sprite/2D | `engine/renderer/render_system.h`, `engine/renderer/sprite_batch.h`, `engine/renderer/render_queue.h` |
| 3D Mesh | `engine/renderer/mesh_loader.h`, `engine/renderer/mesh_renderer.h` |
| Skeletal Anim | `engine/renderer/skeleton.h`, `engine/renderer/animation_system.h`, `engine/renderer/animation_system.cpp` |
| PBR Materials | `engine/renderer/pbr_material.h` |
| Post-Process | `engine/renderer/post_process.h`, `engine/renderer/post_process.cpp` |
| GPU Instancing | `engine/renderer/gpu_instancing.h` |
| SSAO | `engine/renderer/ssao.h`, `engine/renderer/ssao.cpp` |
| Texture Atlas | `engine/renderer/texture_atlas.h`, `engine/renderer/texture_atlas.cpp` |
| Terrain | `engine/renderer/terrain.h`, `engine/renderer/terrain.cpp`, `engine/renderer/terrain_internal.h`, `engine/renderer/terrain_renderer.h`, `engine/renderer/terrain_renderer.cpp` |
| Shadows | `engine/renderer/shadow_map.h`, `engine/renderer/shadow_map.cpp` |
| Shaders | `engine/renderer/shader_library.h`, `engine/renderer/shader_library.cpp` |
| Text | `engine/renderer/text_renderer.h`, `engine/renderer/font.cpp` |
| Textures | `engine/renderer/texture_loader.h`, `engine/renderer/texture_loader.cpp` |
| Camera | `engine/renderer/camera.h`, `engine/renderer/camera.cpp` |
| Screenshot | `engine/renderer/screenshot.h`, `engine/renderer/screenshot.cpp` |
| Audio | `engine/audio/audio.h`, `engine/audio/audio.cpp` |
| Collision | `engine/physics/collider2d.h`, `engine/physics/collision_system.h` |
| Scripting | `engine/scripting/script_engine.h`, `engine/scripting/script_engine.cpp` |
| Networking | `engine/networking/transport.h`, `engine/networking/packet.h`, `engine/networking/replication.h`, `engine/networking/server.h`, `engine/networking/client.h`, `engine/networking/prediction.h`, `engine/networking/lobby.h`, `engine/networking/lag_compensation.h`, `engine/networking/network_system.h`, `engine/networking/connection.h` |
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

**3D Animation** (6): `playAnimation3D`, `stopAnimation3D`, `setAnimationSpeed3D`, `getAnimationProgress3D`, `isAnimation3DPlaying`, `getAnimationCount3D`

**PBR** (3): `setPBRMaterial`, `setPBRTexture`, `removePBRMaterial`

**Post-Processing** (6): `enablePostProcessing`, `disablePostProcessing`, `setBloomThreshold`, `setBloomIntensity`, `setToneMapping`, `setExposure`

**GPU Instancing** (1): `getInstanceCount`

**Anti-Aliasing** (2): `setAntiAliasing`, `setMSAASamples`

**SSAO** (2): `enableSSAO`, `disableSSAO`

**Terrain** (4): `loadTerrain`, `getTerrainHeight`, `unloadTerrain`, `setTerrainTexture`

**Fog** (2): `setFog`, `disableFog`

**Screenshot** (1): `screenshot`

**Networking** (30): `startServer`, `stopServer`, `isServer`, `connectToServer`, `disconnect`, `isConnected`, `getClientId`, `sendMessage`, `onNetworkMessage`, `onClientConnected`, `onClientDisconnected`, `onConnected`, `onDisconnected`, `setNetworkTickRate`, `setLocalPlayer`, `sendInput`, `onServerInput`, `getPredictionError`, `getNetworkTick`, `createLobby`, `destroyLobby`, `joinLobby`, `leaveLobby`, `setReady`, `isInLobby`, `getLobbyPlayers`, `startLobbyGame`, `onLobbyUpdate`, `onGameStart`, `performHitCheck`, `setLagCompensationWindow`, `onHitConfirm`

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
| PBRMaterial | ~40B | renderer | `pbr_material.h` |
| Terrain | 8B | renderer | `render_system.h` |
| Skeleton | varies | renderer | `render_system.h` |
| AnimationState | ~16B | renderer | `render_system.h` |
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
| `MESH_SKINNED` (5) | Skinned mesh | 3D mesh with bone matrix skinning (64 max bones) |
| `SHADOW_DEPTH_SKINNED` (6) | Skinned shadow | Depth-only pass for skinned meshes |
| `POST_THRESHOLD` (7) | Bloom threshold | Extracts bright fragments for bloom |
| `POST_BLUR` (8) | Gaussian blur | Two-pass separable blur for bloom |
| `POST_TONEMAP` (9) | Tone mapping | HDR to LDR (Reinhard/ACES) + gamma correction |
| `MESH_PBR` (10) | PBR mesh | Cook-Torrance BRDF, metallic-roughness, IBL |
| `SHADOW_DEPTH_INSTANCED` (11) | Instanced shadow | Depth-only pass for GPU-instanced meshes |
| `MESH_INSTANCED` (12) | Instanced mesh | Blinn-Phong with per-instance transforms |
| `FXAA` (13) | FXAA 3.11 | Fast approximate anti-aliasing post-process |
| `SSAO` (14) | SSAO | Screen-space ambient occlusion |
| `SSAO_BLUR` (15) | SSAO blur | Bilateral blur for SSAO smoothing |

All shaders are GLSL 330 core. Source is inline in `engine/renderer/shader_library.cpp`.
