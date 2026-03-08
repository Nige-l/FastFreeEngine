# 3D Feature Test

Comprehensive 3D feature arena that exercises all major 3D engine capabilities in a single scene.
Four spatial zones each demonstrate a distinct feature set, with an auto-orbiting camera that
slowly pans around the arena.

![3D Feature Test](../../docs/recordings/3d_feature_test.gif)

## What It Demonstrates

- **Zone 1 — Materials & Colors:** Six colored cubes (red, green, blue, yellow, cyan, magenta)
  with per-cube specular settings; cubes float and rotate on their Y axis
- **Zone 2 — Lighting:** Cluster of high-shininess white cubes illuminated by two orbiting
  point lights (warm orange + cool blue)
- **Zone 3 — Skeletal Animation:** Fox model and Cesium Man playing looping glTF animations,
  driven automatically by the engine's animation system
- **Zone 4 — PBR & Textures:** Damaged Helmet (PBR showcase model), three checkerboard-textured
  cubes, and a rotating Duck model
- **Center — Physics:** Five dynamic cubes fall from above and land on a static ground plane;
  collision count tracked live
- **Post-processing:** Bloom (ACES filmic tone mapping), SSAO (32-sample), FXAA, atmospheric fog;
  each effect is toggle-able at runtime
- **Shadow mapping:** 1024x1024 PCF shadow map covering the full arena

## Controls

| Key | Action |
|-----|--------|
| W / S | Zoom in / out |
| A / D | Orbit left / right |
| Up / Down | Raise / lower camera elevation |
| R | Reset to auto-orbit mode |
| 1 | Toggle shadows |
| 2 | Toggle bloom |
| 3 | Toggle SSAO |
| 4 | Toggle atmospheric fog |
| 5 | Toggle FXAA |
| F | Fire a physics raycast from the camera toward the scene center |
| ESC | Quit |

## How to Run

```sh
./build/examples/3d_feature_test/ffe_3d_feature_test
```

Assets are loaded from the shared `assets/` directory at the repository root. Required models:
`models/cube.glb`, `models/fox.glb`, `models/cesium_man.glb`, `models/damaged_helmet.glb`,
`models/duck.glb`, and `textures/checkerboard.png`. If models are missing the demo falls back
to HUD-only mode and logs a warning.

## Tier

LEGACY (OpenGL 3.3). Advanced post-processing effects are skipped automatically on software
renderers (Mesa llvmpipe).
