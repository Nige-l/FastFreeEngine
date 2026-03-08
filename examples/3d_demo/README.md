# 3D Demo

Spinning cubes with physics and 3D positional audio — the primary demonstration of FFE's 3D
mesh rendering, Jolt physics, and audio attenuation APIs.

![3D Demo](../../docs/recordings/3d_demo.gif)

## What It Demonstrates

- **3D mesh rendering:** Three colored cubes (red, green, blue) loaded from `cube.glb`, each
  with distinct specular shininess settings; the red cube spins at the origin, the green and
  blue cubes orbit at different radii and speeds
- **Dynamic lighting:** Two point lights (warm orange + cool blue) orbit the scene continuously,
  showing real-time local illumination changes
- **Shadow mapping:** Directional shadow map (1024x1024 PCF) cast onto a flat ground plane
- **Skybox:** Cubemap environment loaded from six face images; falls back to flat background
  if images are absent
- **Jolt physics:** A static ground plane at y=-3 and four dynamic cubes that fall under
  gravity; collision callbacks log impact events to the console
- **Physics raycasting:** Press F to fire a ray from the camera and see which entity it hits
- **3D positional audio:** A looping ambient sound plays at the scene origin and attenuates
  with camera distance (requires `assets/sfx/ambient_hum.wav`)
- **2D/3D coexistence:** A HUD (title, FPS, status, controls) drawn on top of the 3D scene
  using the standard 2D draw API

## Controls

| Key | Action |
|-----|--------|
| W / S | Zoom in / out |
| A / D | Orbit left / right |
| Up / Down | Raise / lower camera elevation |
| R | Reset camera to default position |
| F | Fire a physics raycast from the camera |
| ESC | Quit |

## How to Run

```sh
./build/examples/3d_demo/ffe_3d_demo
```

Assets are loaded from the shared `assets/` directory at the repository root. The demo requires
`models/cube.glb`; if it is absent the demo runs in HUD-only mode and logs a warning. Skybox
images (`skybox/right.png`, etc.) and `sfx/ambient_hum.wav` are optional — the demo degrades
gracefully when they are missing.

## Tier

LEGACY (OpenGL 3.3). Shadow mapping and advanced lighting are disabled automatically on software
renderers.
