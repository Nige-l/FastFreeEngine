# Game Showcase

Games and demos built with FastFreeEngine. Everything on this page is open source and included in the FFE repository -- you can read the code, run it yourself, and learn from it.

---

## Official Demos

These demos ship with the engine in the `examples/` directory. Each one exercises a different set of FFE subsystems.

### Collect the Stars

A mini-game written entirely in Lua. Move with WASD, collect spinning stars, watch your score climb.

**What it demonstrates:** ECS entity lifecycle, sprite rendering, collision detection, audio (music + sound effects), input handling, texture loading, HUD text, and clean shutdown from Lua.

**How to run:**

```bash
./build/examples/lua_demo/ffe_lua_demo
```

**Controls:** WASD to move, M to toggle music, UP/DOWN to adjust volume, ESC to quit.

---

### Pong

Two-player Pong with visual juice. Ball trail effects, paddle flash on hit, speed-based color shifting, and background music.

**What it demonstrates:** Input (two-player on one keyboard), collision response, entity creation/destruction, sprite transforms, audio (music + SFX), HUD score display, and visual effects.

**How to run:**

```bash
./build/examples/pong/ffe_pong
```

**Controls:** Left paddle: W/S. Right paddle: UP/DOWN. SPACE to serve. First to 5 wins.

---

### Breakout

Classic brick-breaking with particle effects, ball trail, life indicators, and a victory burst when you clear the board.

**What it demonstrates:** Mass entity destruction (breaking bricks), particle effects, entity creation at runtime, collision detection, ball physics, HUD lives/score, and audio feedback.

**How to run:**

```bash
./build/examples/breakout/ffe_breakout
```

**Controls:** A/D or LEFT/RIGHT to move the paddle. SPACE to launch the ball.

---

### 3D Demo

A 3D scene with mesh rendering, Blinn-Phong lighting, orbiting point lights, shadow mapping, skybox, and a physics simulation with falling objects.

**What it demonstrates:** 3D mesh loading (glTF/GLB), Blinn-Phong shading with specular highlights, directional and point lights, shadow mapping, skybox rendering, 3D physics (gravity, collision callbacks, raycasting, impulses), orbit camera, and 2D HUD drawn on top of a 3D scene.

**How to run:**

```bash
./build/examples/3d_demo/ffe_3d_demo
```

**Controls:** WASD to orbit the camera, F to cast a ray, ESC to quit.

---

### Net Arena

A 2D multiplayer arena. Colored squares moving in a shared space over the network. One instance hosts the server, another connects as a client.

**What it demonstrates:** Client-server architecture, UDP networking, game message serialization, input replication, client-side prediction, authoritative server, snapshot interpolation, and player spawn/despawn.

**How to run:**

```bash
# Terminal 1: host a server
./build/examples/net_demo/ffe_net_demo
# Press S to start hosting on port 7777

# Terminal 2: connect as a client
./build/examples/net_demo/ffe_net_demo
# Press C to connect to localhost:7777
```

**Controls:** S to host, C to connect, WASD to move, ESC to quit.

---

## Submit Your Game

Built something with FastFreeEngine? We want to see it.

**How to submit:**

1. Open an issue on the [FFE GitHub repository](https://github.com/user/FastFreeEngine/issues) with the title "Showcase: [Your Game Name]".
2. Include a brief description of your game and what FFE features it uses.
3. Link to your source code (GitHub, GitLab, or any public repository).
4. Include a screenshot if you have one.

We will add community games to this page as the project grows. A more streamlined submission process (pull request template, screenshot gallery) is on the roadmap.

---

## Made Something Cool?

FastFreeEngine was built so that anyone can make games -- especially people who thought they could not. If you are a student, a hobbyist, or someone who just started learning to code and you built something that works, no matter how small, that counts. A bouncing square is a game. A moving sprite is progress. Ship it, share it, and keep building.

The engine is free and open source forever. What you build with it is yours.
