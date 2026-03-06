# FastFreeEngine

**A performance-first, open-source C++ game engine that runs on old hardware.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## What is FFE?

FastFreeEngine exists to unlock creativity. A student with a ten-year-old laptop should be able to build a game that runs beautifully and share it with the world. A kid who discovers FFE because they want to make games should come out the other side understanding systems programming, graphics, and architecture -- because the engine was designed to be learned from, not just used.

FFE targets indie developers, students, hobbyists, and anyone working with older or lower-end hardware. The engine guarantees 60 fps on its declared minimum tier, or the feature does not ship. Every performance decision is an accessibility decision.

The engine ships with **AI-native documentation**. Every subsystem includes a `.context.md` file written for LLMs to consume, so developers can use AI assistants to write correct game code against FFE immediately. Point your AI coding assistant at the project directory and it should generate idiomatic FFE code out of the box.

FFE is free and open source forever. MIT licensed. That is not up for debate.

---

## Quick Start: Your First Game in 15 Lines

Write game logic in Lua. The engine handles rendering, input, and audio:

```lua
-- my_game.lua
local player = ffe.createEntity()
ffe.addTransform(player, 0, 0, 0, 1, 1)
ffe.addSprite(player, ffe.loadTexture("textures/white.png"), 32, 32, 0.2, 0.8, 1.0, 1, 1)
ffe.addPreviousTransform(player)

function update(entityId, dt)
    local t = {}
    ffe.fillTransform(player, t)
    local speed = 200
    if ffe.isKeyHeld(ffe.KEY_W) then t.y = t.y + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then t.y = t.y - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then t.x = t.x - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_D) then t.x = t.x + speed * dt end
    ffe.setTransform(player, t.x, t.y, 0, 1, 1)
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then ffe.requestShutdown() end
end
```

See the [full tutorial](docs/tutorial.md) for audio, collisions, sprites, and more.

---

## Features

All subsystems below are implemented and working together in three demo games: "Collect the Stars", "Pong", and "Breakout".

- **ECS** -- Entity Component System built on EnTT with a thin `World` wrapper and function-pointer system dispatch. No virtual calls in hot paths.
- **2D Sprite Rendering** -- OpenGL 3.3 backend with batched draw calls (2048 sprites per batch), packed 64-bit sort keys, and render queue.
- **Sprite Animation** -- Grid-based atlas animation with configurable frame count, columns, frame duration, and looping.
- **Lua Scripting** -- Sandboxed LuaJIT with instruction budget (1M ops), blocked globals, and a rich `ffe.*` API for input, transforms, entities, audio, textures, collisions, and shutdown.
- **Audio** -- WAV and OGG support via miniaudio. One-shot SFX playback and streaming background music with volume control. Lock-free ring buffer for decoded audio.
- **2D Collision Detection** -- Spatial hash broadphase with AABB and circle colliders. Lua collision callbacks for game logic.
- **Input System** -- State-based keyboard and mouse input (pressed/held/released/up) with action mapping (64 actions, 4 bindings each).
- **Editor Overlay** -- Dear ImGui debug overlay toggled with F1. Displays entity inspector, system timings, and engine state.
- **Arena Allocator** -- Linear bump allocator with cache-line alignment and per-frame reset. Zero heap allocations in hot paths.
- **Logging** -- printf-style logging with compile-time macro filtering and minimal lock scope.
- **Texture Loading** -- stb_image-based loader with path traversal prevention, write-once asset root, and configurable filter/wrap modes (LINEAR or NEAREST for pixel art).

---

## Hardware Tiers

Every FFE system declares which hardware tiers it supports. No feature may silently degrade performance on a lower tier.

| Tier | Era | GPU API | Min VRAM | Threading | Default |
|------|-----|---------|----------|-----------|---------|
| **RETRO** | ~2005 | OpenGL 2.1 | 512 MB | Single-core safe | No |
| **LEGACY** | ~2012 | OpenGL 3.3 | 1 GB | Single-core safe | **Yes** |
| **STANDARD** | ~2016 | OpenGL 4.5 / Vulkan | 2 GB | Multi-threaded | No |
| **MODERN** | ~2022 | Vulkan | 4 GB+ | Multi-threaded | No |

**LEGACY is the default tier.** If you are unsure which to pick, use LEGACY. It targets hardware from around 2012 -- most laptops and desktops from the last decade will run it comfortably.

---

## Building from Source

### Prerequisites

**Linux (Ubuntu 22.04+)** is the development platform. Install the required packages:

```bash
sudo apt-get install \
    build-essential gcc-13 g++-13 \
    clang-18 clangd-18 clang-tidy-18 clang-format-18 \
    cmake ninja-build \
    mold ccache \
    libgl1-mesa-dev libvulkan-dev \
    libglfw3-dev libasound2-dev libpulse-dev \
    luajit libluajit-5.1-dev \
    pkg-config curl unzip \
    sox  # optional -- used to generate audio test assets
```

### vcpkg Setup

FFE uses [vcpkg](https://github.com/microsoft/vcpkg) for C++ dependency management:

```bash
cd ~
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```

Add to your `~/.bashrc` (or equivalent):

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

Then reload: `source ~/.bashrc`

### Configure and Build

**Build with Clang-18 (primary compiler):**

```bash
git clone https://github.com/user/FastFreeEngine.git
cd FastFreeEngine

cmake -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFFE_TIER=LEGACY

cmake --build build
```

**Build with GCC-13 (secondary compiler):**

```bash
cmake -B build-gcc -G Ninja \
    -DCMAKE_CXX_COMPILER=g++-13 \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build build-gcc
```

### Running Tests

362 Catch2 tests covering core, renderer, scripting, audio, physics, and texture loading:

```bash
ctest --test-dir build --output-on-failure
```

### Selecting a Hardware Tier

Pass `-DFFE_TIER=<TIER>` during configuration. The default is `LEGACY`.

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DFFE_TIER=RETRO
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DFFE_TIER=MODERN
```

---

## Running the Demos

### Collect the Stars (lua_demo)

The flagship demo -- a complete mini-game written entirely in Lua exercising every engine subsystem.

```bash
./build/examples/lua_demo/ffe_lua_demo
```

- **WASD** to move the player
- Collect animated spinning stars for points
- **F1** to toggle the editor overlay
- **ESC** to quit
- Background music and pickup sound effects

### Pong

Classic two-player Pong with visual effects -- ball trail, paddle flash on hit, speed color shift, goal flash panels, and background music.

```bash
./build/examples/pong/ffe_pong
```

- **W/S** left paddle, **UP/DOWN** right paddle
- **SPACE** to serve the ball
- First to 5 wins -- ball speeds up each rally
- **M** music toggle, **F1** editor, **ESC** to quit

### Breakout

Classic brick-breaking game with particle effects -- brick destruction particles, ball trail, speed-based color shifting, paddle flash, life indicators, and a victory particle burst.

```bash
./build/examples/breakout/ffe_breakout
```

- **A/D** or **LEFT/RIGHT** move paddle
- **SPACE** to launch ball, restart after game over
- 84 colorful bricks in 6 rows, ball speeds up per hit
- Particle effects on brick destruction, ball pulsing before launch
- 3 lives, **M** music toggle, **F1** editor, **ESC** quit

### Other Demos

```bash
# Bouncing sprites demo
./build/examples/hello_sprites/ffe_hello_sprites

# Interactive WASD movement demo
./build/examples/interactive_demo/ffe_interactive_demo

# Headless test (CI-safe, no display required)
./build/examples/headless_test/ffe_headless_test
```

---

## Project Structure

```
engine/
  core/         ECS, types, arena allocator, logging, input, application loop
  renderer/     OpenGL 3.3 backend, sprite batching, textures, sprite animation
  audio/        miniaudio integration, WAV/OGG, SFX + streaming music
  physics/      2D collision detection, spatial hash, AABB/circle colliders
  scripting/    Lua sandbox, ffe.* API bindings, instruction budget
  editor/       Dear ImGui overlay, entity inspector

tests/          362 Catch2 tests (core, renderer, scripting, audio, physics)
examples/       Demo games (lua_demo, pong, breakout, hello_sprites, interactive_demo, headless_test)
assets/
  textures/     PNG textures and spritesheets
  audio/        WAV sound effects and OGG music tracks
docs/
  architecture/ ADR design documents for each subsystem
  devlog.md     Session-by-session development history
```

Every engine subdirectory contains a `.context.md` file with API documentation, usage patterns, and anti-patterns -- written for both humans and AI assistants.

---

## Dependencies

Managed via vcpkg (see `vcpkg.json`):

| Dependency | Purpose |
|------------|---------|
| [EnTT](https://github.com/skypjack/entt) | Entity Component System |
| [GLM](https://github.com/g-truc/glm) | Math library |
| [Dear ImGui](https://github.com/ocornut/imgui) | Editor overlay (with GLFW + OpenGL3 bindings) |
| [sol2](https://github.com/ThePhD/sol2) | Lua C++ bindings |
| [Tracy](https://github.com/wolfpld/tracy) | Frame profiler |
| [Catch2](https://github.com/catchorg/Catch2) | Test framework |
| [nlohmann-json](https://github.com/nlohmann/json) | JSON parsing |

System packages:

| Dependency | Purpose |
|------------|---------|
| LuaJIT | Lua scripting runtime |
| GLFW | Window and input management |
| glad | OpenGL function loading (vendored in `third_party/`) |
| stb_image | Texture loading (vendored in `third_party/`) |
| miniaudio | Audio playback (vendored in `third_party/`) |

---

## License

FastFreeEngine is licensed under the [MIT License](LICENSE). Free and open source, forever.

---

## Status

Active development. The engine has a working game loop, 2D rendering, audio, physics, scripting, and an editor overlay -- all demonstrated in three playable demo games. See `docs/devlog.md` for the full session-by-session development history.

**Getting started?** Read the [Quick-Start Tutorial](docs/tutorial.md) to build your first game in Lua.

**Want to contribute?** See [CONTRIBUTING.md](CONTRIBUTING.md) for code style, commit format, and PR process.
