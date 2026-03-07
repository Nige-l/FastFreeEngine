# Build Games That Run Everywhere

**FastFreeEngine is a free, open-source C++ game engine built to run on old hardware.**

A student with a ten-year-old laptop should be able to build a game that runs beautifully and share it with the world. FFE guarantees 60 fps on its minimum hardware tier, or the feature does not ship. Every performance decision is an accessibility decision.

---

## Get Started in Minutes

Write your game logic in Lua. The engine handles rendering, input, audio, physics, and more:

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

[Get Started](getting-started.md){ .md-button .md-button--primary }
[Browse Tutorials](tutorials/index.md){ .md-button }

---

## What FFE Gives You

**2D and 3D Rendering** -- Sprite batching, 3D mesh loading (glTF), Blinn-Phong lighting, shadow mapping, skybox environments. All on OpenGL 3.3.

**Lua Scripting** -- Sandboxed LuaJIT with 85+ bindings covering input, entities, audio, physics, collisions, cameras, and more. Write game logic without touching C++.

**Audio** -- WAV and OGG playback via miniaudio. One-shot sound effects, streaming music, and 3D spatial audio.

**Physics** -- 2D collision detection with spatial hash broadphase. 3D rigid body physics via Jolt with collision callbacks and raycasting.

**Editor Overlay** -- Dear ImGui debug overlay with entity inspector, system timings, and engine state. Toggle with F1.

**AI-Native Documentation** -- Every subsystem ships a `.context.md` file. Point your AI coding assistant at the project and get correct, idiomatic game code generated immediately.

**Hardware Tiers** -- Declare what your game needs. FFE scales from 2005-era GPUs (OpenGL 2.1) to modern Vulkan hardware, with LEGACY (~2012, OpenGL 3.3) as the default.

**MIT Licensed, Forever** -- Free and open source. No royalties, no strings, no debate.

---

## Why FFE?

FFE exists to unlock creativity and get young people into engineering. The mainstream engines are powerful, but they require powerful hardware and come with licensing complexity. FFE takes a different path:

- **Performance is accessibility.** Running well on old hardware means more people can build and play games.
- **Designed to be learned from.** The engine is not just a tool -- it is a teaching platform. Every subsystem is documented for both humans and AI assistants.
- **No hidden costs.** MIT licensed. No per-seat fees, no revenue share, no surprise license changes.
- **Ship real games.** FFE is not a toy engine. It supports 2D and 3D rendering, with multiplayer networking and a standalone editor in development. The goal is everything needed to build and ship a complete game.

---

## Explore

- [Getting Started](getting-started.md) -- Build from source and run your first game
- [Tutorials](tutorials/index.md) -- Step-by-step guides for 2D, 3D, and multiplayer
- [API Reference](api/index.md) -- Complete Lua and C++ API documentation
- [How It Works](internals/index.md) -- Understand the engine internals
- [Community](community.md) -- Contribute, connect, and learn together
