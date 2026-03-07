# Getting Started

Get from "I just cloned the repo" to "I have a game running" in 15 minutes.

FastFreeEngine games are written in **Lua**. The engine handles rendering, input, audio, physics, and everything else in C++ -- you write gameplay logic in simple, readable scripts. No C++ knowledge required to make games.

---

## Prerequisites

FFE develops on **Linux (Ubuntu 22.04+)**. macOS and Windows cross-compilation are supported in CI, but Linux is the primary platform.

### Install System Packages

Open a terminal and install everything in one go:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential gcc-13 g++-13 \
    clang-18 \
    cmake ninja-build \
    mold ccache \
    libgl1-mesa-dev \
    libglfw3-dev libasound2-dev libpulse-dev \
    luajit libluajit-5.1-dev \
    libvulkan-dev vulkan-tools \
    pkg-config curl unzip
```

!!! tip "What are these packages?"
    - **clang-18** -- the primary C++ compiler FFE uses
    - **ninja-build** -- a fast build system (much faster than Make)
    - **mold** -- a modern linker that speeds up link times
    - **ccache** -- caches compiled objects so rebuilds are near-instant
    - **libgl1-mesa-dev, libglfw3-dev** -- OpenGL and window management
    - **libasound2-dev, libpulse-dev** -- audio support
    - **luajit, libluajit-5.1-dev** -- the Lua scripting runtime
    - **libvulkan-dev, vulkan-tools** -- Vulkan rendering backend (optional, needed for STANDARD/MODERN tiers)

### Set Up vcpkg

FFE uses [vcpkg](https://github.com/microsoft/vcpkg) to manage C++ library dependencies. You only need to do this once:

```bash
cd ~
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
```

Add vcpkg to your shell environment. Append these lines to your `~/.bashrc`:

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

Then reload your shell:

```bash
source ~/.bashrc
```

!!! warning "Don't skip the vcpkg step"
    The CMake configure step will fail if `VCPKG_ROOT` is not set. If you see errors about missing packages like `entt` or `catch2`, this is almost always the cause.

---

## Build from Source

### Clone the Repository

```bash
git clone https://github.com/user/FastFreeEngine.git
cd FastFreeEngine
```

### Configure with CMake

=== "Clang-18 (recommended)"

    ```bash
    cmake -B build -G Ninja \
        -DCMAKE_CXX_COMPILER=clang++-18 \
        -DCMAKE_BUILD_TYPE=Debug \
        -DFFE_TIER=LEGACY
    ```

=== "GCC-13"

    ```bash
    cmake -B build -G Ninja \
        -DCMAKE_CXX_COMPILER=g++-13 \
        -DCMAKE_BUILD_TYPE=Debug \
        -DFFE_TIER=LEGACY
    ```

The first configure will take a few minutes as vcpkg downloads and builds dependencies. Subsequent configures are fast.

`-DFFE_TIER=LEGACY` targets hardware from around 2012 (OpenGL 3.3, 1 GB VRAM). This is the default tier and runs on most laptops and desktops from the last decade.

### Build

```bash
cmake --build build
```

This compiles the engine, all tests, and all demo games. With ccache and Ninja, a clean build takes about 2-3 minutes. Incremental rebuilds after code changes are much faster.

### Verify with Tests

```bash
ctest --test-dir build --output-on-failure --parallel $(nproc)
```

You should see all tests pass with zero failures. If any test fails, check that all prerequisites are installed correctly.

!!! tip "Speed up test runs"
    The `--parallel $(nproc)` flag runs tests across all your CPU cores. On a 4-core machine, this cuts test time roughly in half.

---

## Run a Demo

The fastest way to see FFE in action is to run one of the included demo games.

### Collect the Stars

This is the flagship demo -- a complete mini-game written entirely in Lua that exercises every engine subsystem:

```bash
./build/examples/lua_demo/ffe_lua_demo
```

**Controls:**

- **WASD** -- move the player
- **Collect the spinning stars** for points
- **F1** -- toggle the debug editor overlay
- **ESC** -- quit

You should see a player sprite moving around the screen, animated stars to collect, background music, and pickup sound effects. If you see all of this, your build is working perfectly.

### Other Demos

```bash
# Classic two-player Pong with particle effects and camera shake
./build/examples/pong/ffe_pong

# Breakout with brick destruction particles and ball trails
./build/examples/breakout/ffe_breakout

# 3D scene with mesh loading, lighting, shadows, and skybox
./build/examples/3d_demo/ffe_3d_demo

# Multiplayer arena with client-side prediction
./build/examples/net_demo/ffe_net_demo
```

Each demo is a self-contained Lua script. Reading their source code is a great way to learn the engine's API.

---

## Write Your First Game

Now for the fun part. You are going to write a game from scratch and run it.

### Create Your Script

Create a new file called `my_game.lua` anywhere you like (your home directory is fine):

```lua
-- my_game.lua: A player you can move around the screen

-- Create a player entity
local player = ffe.createEntity()
ffe.addTransform(player, 0, 0, 0, 1, 1)
ffe.addSprite(player, ffe.loadTexture("textures/white.png"), 32, 32, 0.2, 0.8, 1.0, 1, 1)
ffe.addPreviousTransform(player)

-- Set a dark background
ffe.setBackgroundColor(0.05, 0.05, 0.15)

-- This runs every frame (~60 times per second)
function update(entityId, dt)
    local t = {}
    ffe.fillTransform(player, t)

    local speed = 200
    if ffe.isKeyHeld(ffe.KEY_W) then t.y = t.y + speed * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then t.y = t.y - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then t.x = t.x - speed * dt end
    if ffe.isKeyHeld(ffe.KEY_D) then t.x = t.x + speed * dt end

    ffe.setTransform(player, t.x, t.y, 0, 1, 1)

    ffe.drawText("WASD to move | ESC to quit", 10, 10)

    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end
```

### Understanding the Code

Here is what each part does:

- **`ffe.createEntity()`** -- creates a new game object in the ECS (Entity Component System). Returns an ID you use to refer to it.
- **`ffe.addTransform(entity, x, y, rotation, scaleX, scaleY)`** -- gives the entity a position in the world. `(0, 0)` is the center of the screen.
- **`ffe.addSprite(entity, texture, width, height, r, g, b, a, layer)`** -- gives the entity a visual. The `r, g, b` values tint the sprite (0.2, 0.8, 1.0 makes it light blue).
- **`ffe.addPreviousTransform(entity)`** -- enables smooth movement interpolation between frames.
- **`function update(entityId, dt)`** -- the engine calls this every frame. `dt` is the time step (1/60th of a second at 60 fps).
- **`ffe.fillTransform(player, t)`** -- reads the entity's current position into the table `t` without allocating memory.
- **`ffe.isKeyHeld(key)`** -- returns `true` while a key is held down. `speed * dt` ensures movement is frame-rate independent.
- **`ffe.setTransform(...)`** -- updates the entity's position.
- **`ffe.drawText(text, x, y)`** -- draws HUD text in screen coordinates (top-left is 0,0).
- **`ffe.requestShutdown()`** -- tells the engine to exit cleanly after the current frame.

### Run It

```bash
./build/examples/lua_demo/ffe_lua_demo --script /full/path/to/my_game.lua
```

!!! tip "Use an absolute path"
    The `--script` flag needs the full path to your Lua file. Use `$(pwd)/my_game.lua` if the file is in your current directory.

You should see a light blue square on a dark background that you can move with WASD. That is your first FFE game.

### Next Steps from Here

Try extending your game:

- **Add a collectible:** Create another entity, give it a collider with `ffe.addCollider()`, and use `ffe.setCollisionCallback()` to detect when the player touches it.
- **Add sound:** Load a sound with `ffe.loadSound("sfx.wav")` and play it with `ffe.playSound(handle, 0.8)` when something happens.
- **Add particles:** Attach an emitter to the player with `ffe.addEmitter()` for a movement trail.
- **Add a score:** Use a Lua variable and `ffe.drawText()` to display it on screen.

The [Lua Quick-Start Tutorial](tutorials/first-2d-game.md) covers all of these in detail with working code examples.

---

## What's Next

### Tutorials

- [**Lua Quick-Start Tutorial**](tutorials/first-2d-game.md) -- the complete reference for building 2D games, covering entities, sprites, input, audio, collision, animation, tilemaps, particles, save/load, and more
- [**Your First 3D Scene**](tutorials/first-3d-game.md) -- loading meshes, setting up cameras, lights, and materials
- [**Multiplayer Basics**](tutorials/multiplayer-basics.md) -- client-server networking, lobbies, and prediction

### API Reference

Every engine subdirectory contains a `.context.md` file with the complete API documentation for that subsystem:

- `engine/scripting/.context.md` -- full Lua API reference (~198 bindings)
- `engine/renderer/.context.md` -- rendering system, sprites, 3D meshes, shaders
- `engine/audio/.context.md` -- sound and music playback
- `engine/physics/.context.md` -- collision detection and physics
- `engine/core/.context.md` -- ECS, input, timers, application loop

### AI-Assisted Development

FFE is designed to work with AI coding assistants out of the box. Point your LLM at the project directory and it will pick up the `.context.md` files automatically. These files are written specifically for LLM consumption -- they contain API signatures, usage patterns, and common mistakes so your AI assistant generates correct FFE code on the first try.

!!! tip "Try it now"
    Open your AI coding assistant, point it at your FFE project directory, and ask it to "write an FFE Lua game with a player that shoots projectiles." The `.context.md` files give it everything it needs.

### The Editor

FFE includes a standalone editor application with an ImGui dockspace layout: scene hierarchy tree, entity inspector, FBO viewport with translate/rotate/scale gizmos, undo/redo, asset browser with drag-and-drop, play-in-editor (snapshot/restore), keyboard shortcuts, and a build pipeline for exporting games as standalone executables. Press **F1** in any demo to toggle the debug overlay.

### Community and Contributing

FFE is open source and MIT licensed. See [CONTRIBUTING.md](https://github.com/user/FastFreeEngine/blob/main/CONTRIBUTING.md) for code style, commit format, and how to submit pull requests. We welcome contributions of all sizes -- from typo fixes to new engine subsystems.
