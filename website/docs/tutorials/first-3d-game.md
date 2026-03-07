# Your First 3D Game

In this tutorial you will build a 3D scene from scratch: a lit cube sitting on a ground plane, with shadows, a skybox, point lights, and a camera you can orbit around the scene using the keyboard. By the end, you will understand the core FFE 3D APIs and be ready to build your own 3D games.

*(Your 3D scene will look something like this -- a lit cube on a ground plane with shadows.)*

---

## What You Will Build

A 3D scene containing:

- A **cube mesh** loaded from a `.glb` file
- A flat **ground plane** to catch shadows
- **Directional lighting** (like sunlight) with configurable color
- A **point light** that orbits the cube for dramatic local illumination
- **Shadow mapping** so the cube casts a shadow on the ground
- A **skybox** surrounding the entire scene
- An **orbit camera** controlled with WASD and arrow keys

All of this in about 120 lines of Lua.

---

## Prerequisites

Before starting this tutorial, make sure you have:

- **FFE installed and running** -- see the [Getting Started](../getting-started.md) guide
- A `.glb` mesh file in your assets folder -- FFE ships with `assets/models/cube.glb` which we will use

!!! info "What is a .glb file?"
    GLB is the binary form of the **glTF 2.0** format -- an open standard for 3D models. Think of it as the "JPEG of 3D": compact, widely supported, and easy to export from tools like Blender. FFE loads `.glb` files and uploads them to the GPU automatically.

---

## Step 1: Set Up the 3D Camera

Every 3D scene needs a camera -- the virtual eye through which you see the world. FFE provides three camera modes, but for this tutorial we will use the **orbit camera**, which circles around a target point. It is perfect for inspecting objects.

Create a file called `game.lua` in your project and add:

```lua
-- game.lua -- Your First 3D Game

-- Camera state
local camYaw   = 45.0   -- horizontal angle in degrees
local camPitch = 20.0   -- vertical angle in degrees
local camRadius = 8.0   -- distance from the target

-- Position the camera looking at the origin (0, 0, 0)
ffe.set3DCameraOrbit(0, 0, 0, camRadius, camYaw, camPitch)
```

`ffe.set3DCameraOrbit` takes six arguments:

| Argument | Meaning |
|----------|---------|
| `target_x, target_y, target_z` | The point the camera looks at (world center) |
| `radius` | How far the camera is from that point |
| `yaw_deg` | Horizontal rotation around the target (degrees) |
| `pitch_deg` | Vertical tilt -- how high or low the camera sits (degrees, clamped to -85..85) |

!!! tip "Coordinate system"
    FFE uses a **right-handed, Y-up** coordinate system. That means Y points up toward the sky, and the XZ plane is the "floor."

---

## Step 2: Load a Mesh

A **mesh** is a 3D shape made of triangles. Before you can put anything on screen, you need to load a mesh from a `.glb` file. This is a one-time operation -- do it at the top of your script, not inside `update()`.

Add this below the camera code:

```lua
-- Load the cube mesh (once, at startup)
local meshHandle = ffe.loadMesh("models/cube.glb")

if meshHandle == 0 then
    ffe.log("ERROR: Could not load cube.glb!")
    return
end
```

`ffe.loadMesh` returns a **handle** -- a number that identifies the loaded mesh. If the file is missing or invalid, it returns `0`. Always check for this.

!!! warning "Cold path only"
    `ffe.loadMesh` reads a file from disk and uploads geometry to the GPU. Never call it inside `update()` -- that would reload the mesh every single frame and destroy performance.

---

## Step 3: Create 3D Entities

Now that the mesh is loaded, you can create **entities** -- objects in the scene that use that mesh. Let's create a cube and a ground plane.

```lua
-- Create the main cube at the origin
local cube = ffe.createEntity3D(meshHandle, 0, 1, 0)
ffe.setTransform3D(cube,
    0, 1, 0,       -- position: slightly above the ground
    0, 0, 0,       -- rotation: no rotation (degrees)
    1, 1, 1        -- scale: normal size
)

-- Create a ground plane (a cube scaled flat and wide)
local ground = ffe.createEntity3D(meshHandle, 0, -0.5, 0)
ffe.setTransform3D(ground,
    0, -0.5, 0,    -- position: below the cube
    0, 0, 0,       -- rotation: none
    12, 0.15, 12   -- scale: wide and thin
)
```

`ffe.createEntity3D` creates a new entity with a 3D transform and mesh component attached. It takes the mesh handle and an initial position (x, y, z).

`ffe.setTransform3D` gives you full control over position, rotation (in degrees), and scale. It takes **nine numbers** after the entity ID:

| Group | Arguments | Meaning |
|-------|-----------|---------|
| Position | `x, y, z` | Where the object sits in the world |
| Rotation | `rx, ry, rz` | Euler angles in degrees (YXZ order) |
| Scale | `sx, sy, sz` | Size multiplier on each axis |

!!! warning "Always set scale explicitly"
    If you omit the scale arguments, they default to `0` -- which collapses the mesh to an invisible point. Always pass all nine values.

---

## Step 4: Set Colors

By default, every mesh renders as plain white. You can tint meshes with `ffe.setMeshColor` to give them personality.

```lua
-- Tint the cube a warm orange
ffe.setMeshColor(cube, 0.9, 0.5, 0.2, 1.0)

-- Make the ground a neutral grey
ffe.setMeshColor(ground, 0.45, 0.45, 0.45, 1.0)
```

The four values are **red, green, blue, alpha** -- each between 0.0 and 1.0. Alpha controls transparency (1.0 = fully opaque).

You can also add **specular highlights** to make surfaces look shiny:

```lua
-- Make the cube shiny (white specular, high shininess)
ffe.setMeshSpecular(cube, 1.0, 1.0, 1.0, 64)

-- Give the ground a subtle sheen
ffe.setMeshSpecular(ground, 0.3, 0.3, 0.3, 16)
```

The first three values are the specular highlight color. The fourth is **shininess** -- higher numbers mean a tighter, sharper reflection (like metal), lower numbers mean a broad, soft reflection (like rubber).

---

## Step 5: Directional Lighting

A scene without light is just a black screen. The simplest light type is a **directional light** -- it shines in one direction everywhere, like sunlight.

```lua
-- Set the sun direction (pointing down and to the right)
ffe.setLightDirection(1.0, -1.5, 0.8)

-- Warm white sunlight
ffe.setLightColor(1.0, 0.95, 0.8)

-- Cool ambient fill so shadows are not pitch black
ffe.setAmbientColor(0.08, 0.08, 0.15)
```

!!! info "What is ambient light?"
    In the real world, light bounces off walls, floors, and the sky, so even shadowed areas are not completely dark. **Ambient light** simulates this by adding a small constant color to every surface. Without it, any face pointing away from the sun would be invisible.

**`ffe.setLightDirection(x, y, z)`** sets which way the light shines. You do not need to normalize the vector -- FFE does it for you. A direction of `(1, -1.5, 0.8)` means the light comes from the upper-left-back of the scene.

**`ffe.setLightColor(r, g, b)`** controls the sunlight color. Try `(1, 0.7, 0.4)` for a sunset feel, or `(0.8, 0.9, 1.0)` for a cold overcast sky.

**`ffe.setAmbientColor(r, g, b)`** is the minimum light level everywhere. Keep this low (0.05--0.15) or your scene will look flat.

---

## Step 6: Point Lights

A **point light** is a light source at a specific position -- like a torch or a lamp. Unlike directional light, it only affects nearby objects and gets dimmer with distance.

FFE supports up to 8 point lights at once, addressed by slot index (0--7).

```lua
-- Add an orange point light floating above the cube
ffe.addPointLight(0,       -- slot index
    2, 2, 0,               -- position (x, y, z)
    1.0, 0.6, 0.2,         -- color (warm orange)
    8.0                     -- radius (how far the light reaches)
)
```

The **radius** controls how far the light reaches before fading to zero. A radius of 8 means objects more than 8 units away will not be lit by this light.

We will animate the point light's position later to make it orbit the cube.

---

## Step 7: Shadow Mapping

Shadows make a 3D scene look dramatically more realistic. FFE uses **shadow mapping** -- the engine renders the scene from the sun's perspective to figure out what is in shadow.

```lua
-- Enable shadows with a 1024x1024 shadow map
ffe.enableShadows(1024)

-- Reduce shadow artifacts (shadow acne)
ffe.setShadowBias(0.005)

-- Configure the shadow area to cover our scene
ffe.setShadowArea(20, 20, 0.1, 40)
```

!!! info "What is a shadow map?"
    A shadow map is a texture that stores how far each pixel is from the light. When the engine renders the actual scene, it checks each pixel against the shadow map: if something is closer to the light, this pixel must be in shadow. The `size` parameter (1024) controls the resolution -- higher means sharper shadows but uses more GPU memory.

**`ffe.enableShadows(size)`** turns on shadow mapping. The size must be a power of 2 (256, 512, 1024, 2048, or 4096). Start with 1024 -- that is a good balance.

**`ffe.setShadowBias(bias)`** prevents an artifact called "shadow acne" -- a moire pattern where surfaces incorrectly shadow themselves. A bias of 0.005 works well for most scenes.

**`ffe.setShadowArea(width, height, near, far)`** defines how large an area the shadow covers. Think of it as the "window" the sun looks through. If your scene is small (say, 10 units across), use `(15, 15, 0.1, 30)`. If it is large, increase these values.

---

## Step 8: Skybox

A **skybox** is a giant cube surrounding the entire scene, textured with sky images on all six faces. It gives the illusion of a vast environment.

```lua
-- Load skybox (6 face images: right, left, top, bottom, front, back)
local skyboxLoaded = ffe.loadSkybox(
    "skybox/right.png", "skybox/left.png",
    "skybox/top.png",   "skybox/bottom.png",
    "skybox/front.png", "skybox/back.png"
)

if skyboxLoaded then
    ffe.log("Skybox loaded!")
else
    ffe.log("Skybox images not found -- using background color")
end

-- Set a fallback background color (visible if skybox fails to load)
ffe.setBackgroundColor(0.05, 0.05, 0.12)
```

!!! note "Skybox assets"
    You need six square images -- one for each face of the cube. You can find free skybox textures online by searching for "free cubemap skybox." Place them in your `assets/skybox/` folder with the names shown above.

    If the images are missing, `ffe.loadSkybox` returns `false` and the scene falls back to the background color. Your game still works -- it just will not have a sky.

The six faces follow the standard OpenGL cubemap convention:

| Parameter | Face | Direction |
|-----------|------|-----------|
| 1st | Right | +X |
| 2nd | Left | -X |
| 3rd | Top | +Y |
| 4th | Bottom | -Y |
| 5th | Front | +Z |
| 6th | Back | -Z |

---

## Step 9: Camera Controls and Animation

Now let's bring the scene to life. The `update()` function runs every frame. We will use it to:

1. Move the camera with keyboard input
2. Spin the cube
3. Orbit the point light around the scene

```lua
-- Movement speeds
local CAM_YAW_SPEED   = 60.0   -- degrees per second
local CAM_PITCH_SPEED  = 40.0   -- degrees per second
local CAM_ZOOM_SPEED   = 3.0    -- units per second

-- Animation state
local cubeRotation   = 0.0
local lightAngle     = 0.0

function update(entityId, dt)
    -- -----------------------------------------------
    -- Camera controls
    -- -----------------------------------------------
    if ffe.isKeyHeld(ffe.KEY_A) then
        camYaw = camYaw - CAM_YAW_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_D) then
        camYaw = camYaw + CAM_YAW_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_UP) then
        camPitch = math.min(85, camPitch + CAM_PITCH_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_DOWN) then
        camPitch = math.max(-85, camPitch - CAM_PITCH_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_W) then
        camRadius = math.max(2, camRadius - CAM_ZOOM_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_S) then
        camRadius = math.min(20, camRadius + CAM_ZOOM_SPEED * dt)
    end

    -- Apply camera
    ffe.set3DCameraOrbit(0, 0, 0, camRadius, camYaw, camPitch)

    -- -----------------------------------------------
    -- Spin the cube
    -- -----------------------------------------------
    cubeRotation = cubeRotation + 45 * dt
    if cubeRotation > 360 then cubeRotation = cubeRotation - 360 end

    ffe.setTransform3D(cube,
        0, 1, 0,                  -- position (unchanged)
        0, cubeRotation, 0,       -- rotate around Y axis
        1, 1, 1                   -- scale (unchanged)
    )

    -- -----------------------------------------------
    -- Orbit the point light around the scene
    -- -----------------------------------------------
    lightAngle = lightAngle + 40 * dt
    if lightAngle > 360 then lightAngle = lightAngle - 360 end

    local rad = math.rad(lightAngle)
    ffe.setPointLightPosition(0,
        math.sin(rad) * 3.5,     -- X: circular path
        2.0,                      -- Y: fixed height
        math.cos(rad) * 3.5      -- Z: circular path
    )

    -- -----------------------------------------------
    -- HUD overlay
    -- -----------------------------------------------
    ffe.drawText("YOUR FIRST 3D GAME", 12, 8, 2, 1, 0.9, 0.3, 1)
    ffe.drawText("WASD: orbit/zoom | UP/DOWN: pitch | ESC: quit",
        12, 690, 2, 0.5, 0.6, 0.7, 0.9)

    -- -----------------------------------------------
    -- Quit on ESC
    -- -----------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end
```

A few things to notice:

- **`dt`** is the time since the last frame, in seconds. Multiplying speeds by `dt` makes movement smooth regardless of frame rate.
- We **clamp** the pitch to [-85, 85] degrees to avoid flipping the camera upside down (FFE clamps it internally too, but it is good practice).
- We **wrap** rotation angles at 360 degrees to prevent the numbers from growing forever.
- `ffe.drawText` renders 2D text on top of the 3D scene -- useful for HUD elements like score, controls, or debug info.

---

## Step 10: Clean Up on Shutdown

When the game exits, you should free the resources you loaded. Define a `shutdown()` function -- FFE calls it automatically before closing.

```lua
function shutdown()
    -- Remove the point light
    ffe.removePointLight(0)

    -- Disable shadows and free GPU resources
    ffe.disableShadows()

    -- Unload the skybox cubemap
    ffe.unloadSkybox()

    -- Unload the mesh (frees GPU memory)
    if meshHandle ~= 0 then
        ffe.unloadMesh(meshHandle)
    end

    ffe.log("Shutdown complete")
end
```

!!! tip "Why clean up?"
    FFE will not crash if you skip cleanup -- the engine frees everything when it exits. But cleaning up explicitly is a good habit: it prevents GPU memory leaks in games with scene transitions, and it makes your code easier to understand.

---

## Complete Code

Here is the full `game.lua` in one listing, ready to copy and run:

```lua
-- game.lua -- Your First 3D Game
-- A lit 3D scene with shadows, a skybox, and orbit camera controls.
--
-- Controls:
--   A/D         orbit camera left/right
--   W/S         zoom in/out
--   UP/DOWN     tilt camera up/down
--   ESC         quit

-- ---------------------------------------------------------
-- Camera state
-- ---------------------------------------------------------
local camYaw    = 45.0
local camPitch  = 20.0
local camRadius = 8.0

ffe.set3DCameraOrbit(0, 0, 0, camRadius, camYaw, camPitch)

-- ---------------------------------------------------------
-- Load mesh
-- ---------------------------------------------------------
local meshHandle = ffe.loadMesh("models/cube.glb")

if meshHandle == 0 then
    ffe.log("ERROR: Could not load cube.glb!")
    return
end

-- ---------------------------------------------------------
-- Create entities
-- ---------------------------------------------------------

-- Main cube
local cube = ffe.createEntity3D(meshHandle, 0, 1, 0)
ffe.setTransform3D(cube, 0, 1, 0,  0, 0, 0,  1, 1, 1)
ffe.setMeshColor(cube, 0.9, 0.5, 0.2, 1.0)
ffe.setMeshSpecular(cube, 1.0, 1.0, 1.0, 64)

-- Ground plane
local ground = ffe.createEntity3D(meshHandle, 0, -0.5, 0)
ffe.setTransform3D(ground, 0, -0.5, 0,  0, 0, 0,  12, 0.15, 12)
ffe.setMeshColor(ground, 0.45, 0.45, 0.45, 1.0)
ffe.setMeshSpecular(ground, 0.3, 0.3, 0.3, 16)

-- ---------------------------------------------------------
-- Lighting
-- ---------------------------------------------------------
ffe.setLightDirection(1.0, -1.5, 0.8)
ffe.setLightColor(1.0, 0.95, 0.8)
ffe.setAmbientColor(0.08, 0.08, 0.15)

-- Point light (warm orange, will orbit the cube)
ffe.addPointLight(0,  2, 2, 0,  1.0, 0.6, 0.2,  8.0)

-- ---------------------------------------------------------
-- Shadows
-- ---------------------------------------------------------
ffe.enableShadows(1024)
ffe.setShadowBias(0.005)
ffe.setShadowArea(20, 20, 0.1, 40)

-- ---------------------------------------------------------
-- Skybox
-- ---------------------------------------------------------
local skyboxLoaded = ffe.loadSkybox(
    "skybox/right.png", "skybox/left.png",
    "skybox/top.png",   "skybox/bottom.png",
    "skybox/front.png", "skybox/back.png"
)
if skyboxLoaded then
    ffe.log("Skybox loaded!")
else
    ffe.log("Skybox images not found -- using background color")
end

ffe.setBackgroundColor(0.05, 0.05, 0.12)

-- ---------------------------------------------------------
-- Animation state
-- ---------------------------------------------------------
local CAM_YAW_SPEED   = 60.0
local CAM_PITCH_SPEED = 40.0
local CAM_ZOOM_SPEED  = 3.0

local cubeRotation = 0.0
local lightAngle   = 0.0

ffe.log("Your First 3D Game -- ready! WASD camera, ESC quit")

-- ---------------------------------------------------------
-- Per-frame update
-- ---------------------------------------------------------
function update(entityId, dt)

    -- Camera controls
    if ffe.isKeyHeld(ffe.KEY_A) then
        camYaw = camYaw - CAM_YAW_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_D) then
        camYaw = camYaw + CAM_YAW_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_UP) then
        camPitch = math.min(85, camPitch + CAM_PITCH_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_DOWN) then
        camPitch = math.max(-85, camPitch - CAM_PITCH_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_W) then
        camRadius = math.max(2, camRadius - CAM_ZOOM_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_S) then
        camRadius = math.min(20, camRadius + CAM_ZOOM_SPEED * dt)
    end

    ffe.set3DCameraOrbit(0, 0, 0, camRadius, camYaw, camPitch)

    -- Spin the cube on its Y axis
    cubeRotation = cubeRotation + 45 * dt
    if cubeRotation > 360 then cubeRotation = cubeRotation - 360 end

    ffe.setTransform3D(cube,
        0, 1, 0,
        0, cubeRotation, 0,
        1, 1, 1
    )

    -- Orbit the point light around the scene
    lightAngle = lightAngle + 40 * dt
    if lightAngle > 360 then lightAngle = lightAngle - 360 end

    local rad = math.rad(lightAngle)
    ffe.setPointLightPosition(0,
        math.sin(rad) * 3.5,
        2.0,
        math.cos(rad) * 3.5
    )

    -- HUD text
    ffe.drawText("YOUR FIRST 3D GAME", 12, 8, 2, 1, 0.9, 0.3, 1)
    ffe.drawText("WASD: orbit/zoom | UP/DOWN: pitch | ESC: quit",
        12, 690, 2, 0.5, 0.6, 0.7, 0.9)

    -- Quit
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------
-- Cleanup
-- ---------------------------------------------------------
function shutdown()
    ffe.removePointLight(0)
    ffe.disableShadows()
    ffe.unloadSkybox()
    if meshHandle ~= 0 then
        ffe.unloadMesh(meshHandle)
    end
    ffe.log("Shutdown complete")
end
```

---

## What You Learned

In this tutorial you learned how to:

- **Load a 3D mesh** from a `.glb` file with `ffe.loadMesh`
- **Create 3D entities** and position them with `ffe.createEntity3D` and `ffe.setTransform3D`
- **Set up an orbit camera** with `ffe.set3DCameraOrbit`
- **Configure lighting** -- directional light, ambient light, and point lights
- **Apply materials** -- diffuse color with `ffe.setMeshColor` and specular highlights with `ffe.setMeshSpecular`
- **Enable shadow mapping** with `ffe.enableShadows`, `ffe.setShadowBias`, and `ffe.setShadowArea`
- **Load a skybox** with `ffe.loadSkybox`
- **Animate objects** by updating transforms and light positions every frame
- **Clean up resources** in the `shutdown()` callback

## Next Steps

Now that you have a 3D foundation, try these experiments:

- **Add more cubes** -- create several entities at different positions, colors, and scales
- **Add a second point light** -- use slot 1 with a cool blue color (`ffe.addPointLight(1, ...)`)
- **Try FPS camera mode** -- replace orbit camera with `ffe.set3DCameraFPS(x, y, z, yaw, pitch)` and move with WASD
- **Load a different mesh** -- export a model from [Blender](https://www.blender.org/) as `.glb` and load it in FFE
- **Add a diffuse texture** -- use `ffe.loadTexture("textures/crate.png")` and `ffe.setMeshTexture(cube, tex)` to texture your mesh
