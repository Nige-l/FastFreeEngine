# Breakout

Classic single-player Breakout. Clear all 84 bricks to win. You have three lives.

![Breakout](../../docs/recordings/breakout.gif)

## What It Demonstrates

- **Mass entity creation and destruction:** 84 brick entities (14 columns × 6 rows) created
  at startup; each brick is destroyed individually on hit with `destroyEntity`
- **Entity pool pattern:** Ball trail (5-entity fixed pool) and particle system (up to 40
  particles) reuse entity slots rather than allocating per frame
- **Particle effects:** Brick-colored burst particles with gravity, fade-out, and upward
  velocity bias spawn on every brick break; a multi-color victory burst fires on win
- **Collision detection:** Overlap-based AABB with side-vs-top bounce discrimination; ball
  angle on paddle hit is controlled by where the ball strikes the paddle (±60°)
- **Progressive difficulty:** Ball speed increases by 3 units per brick break up to 600
  units/second; ball and trail color shifts from white toward red as speed climbs
- **Visual juice:** Paddle flash on hit, camera shake on brick break and life loss, pulsing
  ball while waiting to launch
- **Audio:** Background music (OGG), SFX for brick hits, paddle bounces, wall bounces, and
  life loss; press M to toggle music at runtime
- **Scoring:** Top rows (red) worth 6 points, descending to 1 point for the bottom row (blue)

## Controls

| Key | Action |
|-----|--------|
| A or Left | Move paddle left |
| D or Right | Move paddle right |
| Space | Launch ball (or restart after game over) |
| M | Toggle background music |
| ESC | Quit |

Press Space on the title screen to start, then Space again to launch the ball. The ball
follows the paddle until launched.

## How to Run

```sh
./build/examples/breakout/ffe_breakout
```

Assets are loaded from the shared `assets/` directory at the repository root. Required:
`textures/white.png`. Optional audio: `audio/sfx_pong_paddle.wav`, `audio/sfx_pong_wall.wav`,
`audio/sfx_pong_score.wav`, `audio/music_pixelcrown.ogg`. The demo runs without audio if
files are missing.

## Tier

LEGACY (OpenGL 3.3). 2D only — no 3D or post-processing.
