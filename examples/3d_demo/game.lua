-- game.lua -- "3D Spinning Cubes + Physics" demo for FFE
--
-- Demonstrates the 3D Mesh Rendering API:
--   ffe.loadMesh / ffe.unloadMesh
--   ffe.createEntity3D
--   ffe.setTransform3D
--   ffe.setMeshColor / ffe.setMeshSpecular
--   ffe.set3DCameraOrbit
--   ffe.setLightDirection / ffe.setLightColor / ffe.setAmbientColor
--   ffe.addPointLight / ffe.setPointLightPosition / ffe.removePointLight
--
-- Demonstrates the 3D Physics API:
--   ffe.createPhysicsBody  (static ground + dynamic falling objects)
--   ffe.onCollision3D      (collision event callback)
--   ffe.castRay             (raycast from camera on key press)
--   ffe.applyImpulse       (kick objects upward on collision)
--
-- Three mesh entities orbit the world origin at different speeds and radii,
-- each tinted a distinct color with specular highlights. Two point lights
-- orbit the scene to demonstrate dynamic local illumination. A 2D HUD
-- (entity count, FPS, controls) is drawn on top using the standard
-- ffe.drawText / ffe.drawRect API, showing how 3D and 2D coexist in the
-- same frame.
--
-- Physics: A static ground plane sits at y=-3.  Four dynamic cubes spawn
-- above it and fall under gravity.  Collision callbacks log impacts.
-- Press F to cast a ray from the camera and log what it hits.
--
-- Controls:
--   WASD          orbit camera left/right, zoom in/out
--   UP/DOWN       raise/lower camera elevation
--   R             reset camera to default position
--   F             fire raycast from camera
--   ESC           quit
--
-- Asset path: "models/cube.glb" relative to the asset root.
-- If the file is absent (handle == 0) the demo logs a warning,
-- skips entity creation, and renders only the HUD -- so the
-- executable is useful as a smoke test even without assets.
--
-- Coordinate system: right-handed, Y-up.
-- Window: 1280 x 720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local ROTATION_SPEED_A =  45.0   -- degrees per second, red cube
local ROTATION_SPEED_B =  72.0   -- degrees per second, green cube
local ROTATION_SPEED_C = 120.0   -- degrees per second, blue cube

local ORBIT_RADIUS_A = 0.0       -- centre (stationary)
local ORBIT_RADIUS_B = 2.5       -- orbits the centre
local ORBIT_RADIUS_C = 4.0       -- wider orbit

local ORBIT_SPEED_B  = 30.0      -- degrees per second for orbit
local ORBIT_SPEED_C  = 20.0      -- degrees per second for orbit

-- Point light orbit parameters
local POINT_LIGHT_ORBIT_SPEED = 40.0  -- degrees per second
local POINT_LIGHT_RADIUS      = 3.5   -- orbit radius around origin
local POINT_LIGHT_HEIGHT      = 1.5   -- Y elevation above ground

-- Camera defaults
local CAM_DEFAULT_DIST = 8.0
local CAM_DEFAULT_ELEV = 4.0
local CAM_MOVE_SPEED   = 60.0    -- degrees per second (yaw)
local CAM_ZOOM_SPEED   = 3.0     -- units per second
local CAM_ELEV_SPEED   = 2.0     -- units per second
local CAM_MIN_DIST     = 2.0
local CAM_MAX_DIST     = 20.0
local CAM_MIN_ELEV     = -8.0
local CAM_MAX_ELEV     = 12.0

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local meshHandle   = 0        -- ffe.MeshHandle integer (0 = invalid)
local entityA      = 0        -- red cube  (centre, spins on Y)
local entityB      = 0        -- green cube (orbits + spins)
local entityC      = 0        -- blue cube  (wide orbit + spins)
local meshLoaded   = false    -- true when cube.glb loaded successfully

-- Per-entity rotation accumulators (degrees)
local rotA = 0.0
local rotB = 0.0
local rotC = 0.0

-- Orbit angle accumulators (degrees)
local orbitB = 0.0
local orbitC = 120.0   -- start offset so cubes are not bunched together

-- Point light orbit angle (degrees)
local pointLightAngle = 0.0

-- Camera state
local camYaw  = 45.0          -- horizontal angle around Y axis (degrees)
local camDist = CAM_DEFAULT_DIST
local camElev = CAM_DEFAULT_ELEV

-- FPS tracking
local fpsTimer    = 0.0
local fpsFrames   = 0
local fpsDisplay  = 0

-- ---------------------------------------------------------------------------
-- Physics demo state
-- ---------------------------------------------------------------------------
local PHYSICS_GROUND_Y    = -3.0    -- ground plane Y position
local PHYSICS_SPAWN_Y     = 12.0    -- height where dynamic cubes spawn
local PHYSICS_CUBE_COUNT  = 4       -- number of falling cubes

local physicsCubes   = {}           -- entity IDs of dynamic cubes
local physicsGround  = 0            -- entity ID of static ground plane
local collisionCount = 0            -- total collision events received
local lastRayHitMsg  = ""           -- HUD message for last raycast result

-- ---------------------------------------------------------------------------
-- Helper: compute camera world position from yaw/dist/elevation
-- Uses ffe.set3DCameraOrbit: converts the horizontal camDist + vertical camElev
-- into an orbit radius and pitch angle so the visual result is identical to
-- the old manual computation.
-- ---------------------------------------------------------------------------
local function updateCamera()
    -- Total 3D distance from the world origin to the camera.
    local radius = math.sqrt(camDist * camDist + camElev * camElev)
    -- Pitch: angle above the XZ plane, in degrees.
    local pitch  = math.deg(math.atan(camElev, camDist))
    -- ffe.set3DCameraOrbit orbits around target (0,0,0) at the given radius,
    -- yaw, and pitch.  The result is identical to the previous manual cx/cz math.
    ffe.set3DCameraOrbit(0, 0, 0, radius, camYaw, pitch)
end

-- ---------------------------------------------------------------------------
-- Scene init
-- ---------------------------------------------------------------------------

-- Try to load a skybox cubemap. If the images are not present, the call
-- returns false and we fall back to the flat background colour.
-- Face order: right (+X), left (-X), top (+Y), bottom (-Y), front (+Z), back (-Z).
local skyboxLoaded = ffe.loadSkybox(
    "skybox/right.png", "skybox/left.png",
    "skybox/top.png",   "skybox/bottom.png",
    "skybox/front.png", "skybox/back.png"
)
if skyboxLoaded then
    ffe.log("Skybox loaded successfully")
else
    ffe.log("Skybox images not found -- using flat background colour")
end

-- Detect software renderer for graceful degradation of advanced effects
local softwareRenderer = ffe.isSoftwareRenderer()

-- Set a sky colour visible even without a skybox.
-- On software renderers, use a brighter background so the scene is not black.
if softwareRenderer then
    ffe.setBackgroundColor(0.15, 0.15, 0.25)
else
    ffe.setBackgroundColor(0.05, 0.05, 0.12)
end

-- Configure scene lighting:
--   A warm directional light from the upper-right front,
--   plus a cool ambient fill so shadow faces are not pitch-black.
ffe.setLightDirection(1.0, -1.5, 0.8)
if softwareRenderer then
    -- Brighter ambient for software renderer (no shadows / post-processing)
    ffe.setLightColor(1.0, 0.92, 0.78)
    ffe.setAmbientColor(0.2, 0.2, 0.3)
else
    ffe.setLightColor(1.0, 0.92, 0.78)       -- warm white / golden hour
    ffe.setAmbientColor(0.08, 0.08, 0.18)    -- cool blue-grey fill
end

-- Add two point lights that will orbit the scene:
--   Point light 0: warm orange/yellow — orbits CW
--   Point light 1: cool blue/cyan — orbits CCW (offset 180 degrees)
ffe.addPointLight(0,  POINT_LIGHT_RADIUS, POINT_LIGHT_HEIGHT, 0,
                      1.0, 0.6, 0.2,  8.0)
ffe.addPointLight(1, -POINT_LIGHT_RADIUS, POINT_LIGHT_HEIGHT, 0,
                      0.2, 0.5, 1.0,  8.0)

-- Position the camera at default location.
updateCamera()

-- Load the cube mesh from the asset root.
-- "cube.glb" lives at <assetRoot>/models/cube.glb.
meshHandle = ffe.loadMesh("models/cube.glb")

if meshHandle == 0 then
    -- ffe.loadMesh already logged the specific error.
    -- The demo continues; only the HUD will be visible.
    ffe.log("WARNING: cube.glb not found -- running in HUD-only mode")
    ffe.log("         Place a cube.glb at assets/models/cube.glb to see 3D geometry")
else
    ffe.log("cube.glb loaded (handle=" .. tostring(meshHandle) .. ")")

    -- Enable shadow mapping for the 3D scene (skip on software renderer).
    if not softwareRenderer then
        ffe.enableShadows(1024)
        ffe.setShadowBias(0.005)
        ffe.setShadowArea(20, 20, 0.1, 40)
    end

    -- Ground plane to receive shadows (reuses the cube mesh, scaled flat).
    local ground = ffe.createEntity3D(meshHandle, 0, -1.5, 0)
    if ground ~= 0 then
        ffe.setMeshColor(ground, 0.45, 0.45, 0.45, 1.0)
        ffe.setMeshSpecular(ground, 0.3, 0.3, 0.3, 16)   -- subtle specular
        ffe.setTransform3D(ground, 0, -1.5, 0,  0, 0, 0,  12, 0.15, 12)
    end

    -- -----------------------------------------------------------------
    -- Entity A: red cube at the origin, spins on Y axis only
    -- High shininess for bright specular highlights.
    -- -----------------------------------------------------------------
    entityA = ffe.createEntity3D(meshHandle, 0, 0, 0)
    if entityA ~= 0 then
        ffe.setMeshColor(entityA, 0.9, 0.25, 0.20, 1.0)   -- red tint
        ffe.setMeshSpecular(entityA, 1.0, 0.8, 0.6, 128)   -- warm specular, high shine
        ffe.setTransform3D(entityA, 0, 0, 0,  0, 0, 0,  1, 1, 1)
        ffe.log("Entity A (red)   created id=" .. tostring(entityA))
    end

    -- -----------------------------------------------------------------
    -- Entity B: green cube, orbits the origin at radius 2.5
    -- Medium shininess.
    -- -----------------------------------------------------------------
    entityB = ffe.createEntity3D(meshHandle, ORBIT_RADIUS_B, 0, 0)
    if entityB ~= 0 then
        ffe.setMeshColor(entityB, 0.20, 0.85, 0.35, 1.0)  -- green tint
        ffe.setMeshSpecular(entityB, 0.6, 1.0, 0.6, 64)    -- green-tinted specular
        ffe.setTransform3D(entityB, ORBIT_RADIUS_B, 0, 0,  0, 0, 0,  0.8, 0.8, 0.8)
        ffe.log("Entity B (green) created id=" .. tostring(entityB))
    end

    -- -----------------------------------------------------------------
    -- Entity C: blue cube, wider orbit, smaller scale
    -- Low shininess for a soft specular highlight.
    -- -----------------------------------------------------------------
    entityC = ffe.createEntity3D(meshHandle, ORBIT_RADIUS_C, 0, 0)
    if entityC ~= 0 then
        ffe.setMeshColor(entityC, 0.25, 0.50, 1.00, 1.0)  -- blue tint
        ffe.setMeshSpecular(entityC, 0.5, 0.7, 1.0, 32)    -- cool specular, soft
        ffe.setTransform3D(entityC, ORBIT_RADIUS_C, 0, 0,  0, 0, 0,  0.6, 0.6, 0.6)
        ffe.log("Entity C (blue)  created id=" .. tostring(entityC))
    end

    meshLoaded = (entityA ~= 0)

    -- -----------------------------------------------------------------
    -- Physics demo: static ground plane + falling dynamic cubes
    -- -----------------------------------------------------------------

    -- Static ground plane: a large, flat box at PHYSICS_GROUND_Y.
    -- Reuses the cube mesh scaled flat and wide.
    physicsGround = ffe.createEntity3D(meshHandle, 0, PHYSICS_GROUND_Y, 0)
    if physicsGround ~= 0 then
        ffe.setMeshColor(physicsGround, 0.35, 0.55, 0.35, 1.0)  -- muted green
        ffe.setMeshSpecular(physicsGround, 0.2, 0.2, 0.2, 16)
        ffe.setTransform3D(physicsGround,
            0, PHYSICS_GROUND_Y, 0,   -- position
            0, 0, 0,                  -- rotation
            50, 0.5, 50)             -- scale: wide and thin
        ffe.createPhysicsBody(physicsGround, {
            shape       = "box",
            halfExtents = {50, 0.5, 50},
            motion      = "static"
        })
        ffe.log("Physics: static ground plane created id=" .. tostring(physicsGround))
    end

    -- Spawn dynamic cubes that fall under gravity.
    -- Spread them across the X axis so they don't stack directly.
    local cubeColors = {
        {1.0, 0.4, 0.1},   -- orange
        {0.3, 0.8, 1.0},   -- cyan
        {1.0, 0.9, 0.2},   -- yellow
        {0.8, 0.3, 0.9},   -- purple
    }
    for i = 1, PHYSICS_CUBE_COUNT do
        local spawnX = (i - 1) * 2.5 - 3.75  -- spread from -3.75 to 3.75
        local spawnY = PHYSICS_SPAWN_Y + (i - 1) * 2.0  -- stagger heights
        local cube = ffe.createEntity3D(meshHandle, spawnX, spawnY, 0)
        if cube ~= 0 then
            local c = cubeColors[i]
            ffe.setMeshColor(cube, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(cube, 0.5, 0.5, 0.5, 64)
            ffe.setTransform3D(cube,
                spawnX, spawnY, 0,
                0, 0, 0,
                0.7, 0.7, 0.7)
            ffe.createPhysicsBody(cube, {
                shape       = "box",
                halfExtents = {0.35, 0.35, 0.35},  -- half of 0.7 scale
                motion      = "dynamic",
                mass        = 1.0,
                restitution = 0.4,
                friction    = 0.6
            })
            physicsCubes[i] = cube
            ffe.log("Physics: dynamic cube " .. i .. " created id=" .. tostring(cube))
        end
    end

    -- Register a collision callback to log when objects hit the ground.
    ffe.onCollision3D(function(entityA, entityB, px, py, pz, nx, ny, nz, eventType)
        if eventType == "enter" then
            collisionCount = collisionCount + 1
            ffe.log("Collision #" .. collisionCount
                .. " between " .. tostring(entityA) .. " and " .. tostring(entityB)
                .. " at (" .. string.format("%.1f, %.1f, %.1f", px, py, pz) .. ")")
        end
    end)
    ffe.log("Physics: collision callback registered")
end

-- ---------------------------------------------------------------------------
-- 3D Positional Audio
-- ---------------------------------------------------------------------------
-- Play a looping ambient sound near the red cube at the origin.
-- The sound will attenuate as the camera orbits further away and pan left/right
-- based on camera orientation.
--
-- NOTE: Requires an audio file at assets/sfx/ambient_hum.wav (or .ogg).
-- If the file is absent, playSound3D silently no-ops (loadSound fails gracefully).
-- To hear the effect, place any short looping SFX at that path.
--
-- Configure 3D audio attenuation range for the demo scene scale.
ffe.setSound3DMinDistance(1.0)
ffe.setSound3DMaxDistance(15.0)

-- Fire a positioned sound at the origin (where the red cube lives).
-- In a real game, this would be triggered by a game event (e.g., campfire start).
ffe.playSound3D("sfx/ambient_hum.wav", 0, 0, 0, 0.8)

ffe.log("3D demo init complete. WASD camera, UP/DOWN elevation, R reset, ESC quit")

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called per tick by the C++ host
--
-- entityId: the host-created entity (unused in this demo -- we manage our own)
-- dt:       delta time in seconds
-- ---------------------------------------------------------------------------
function update(entityId, dt)

    -- ------------------------------------------------------------------
    -- FPS counter
    -- ------------------------------------------------------------------
    fpsFrames = fpsFrames + 1
    fpsTimer  = fpsTimer  + dt
    if fpsTimer >= 0.5 then
        fpsDisplay = math.floor(fpsFrames / fpsTimer + 0.5)
        fpsFrames  = 0
        fpsTimer   = 0.0
    end

    -- ------------------------------------------------------------------
    -- Camera controls
    -- ------------------------------------------------------------------
    if ffe.isKeyHeld(ffe.KEY_A) then
        camYaw = camYaw - CAM_MOVE_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_D) then
        camYaw = camYaw + CAM_MOVE_SPEED * dt
    end
    if ffe.isKeyHeld(ffe.KEY_W) then
        camDist = math.max(CAM_MIN_DIST, camDist - CAM_ZOOM_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_S) then
        camDist = math.min(CAM_MAX_DIST, camDist + CAM_ZOOM_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_UP) then
        camElev = math.min(CAM_MAX_ELEV, camElev + CAM_ELEV_SPEED * dt)
    end
    if ffe.isKeyHeld(ffe.KEY_DOWN) then
        camElev = math.max(CAM_MIN_ELEV, camElev - CAM_ELEV_SPEED * dt)
    end
    if ffe.isKeyPressed(ffe.KEY_R) then
        camYaw  = 45.0
        camDist = CAM_DEFAULT_DIST
        camElev = CAM_DEFAULT_ELEV
    end

    updateCamera()

    -- ------------------------------------------------------------------
    -- Physics raycast: press F to cast a ray from the camera forward
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_F) then
        -- Compute camera position from orbit parameters (same math as updateCamera)
        local radius = math.sqrt(camDist * camDist + camElev * camElev)
        local pitch  = math.atan(camElev, camDist)
        local yawRad = math.rad(camYaw)

        -- Camera world position (matches orbit camera math)
        local camX = math.sin(yawRad) * math.cos(pitch) * radius
        local camY = math.sin(pitch) * radius
        local camZ = math.cos(yawRad) * math.cos(pitch) * radius

        -- Direction: camera looks toward the orbit target (origin)
        local dirX = -camX
        local dirY = -camY
        local dirZ = -camZ
        -- Normalize direction
        local len = math.sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ)
        if len > 0.001 then
            dirX = dirX / len
            dirY = dirY / len
            dirZ = dirZ / len
        end

        local hitEntity, hx, hy, hz = ffe.castRay(camX, camY, camZ, dirX, dirY, dirZ, 100.0)
        if hitEntity then
            lastRayHitMsg = "Ray hit entity " .. tostring(hitEntity)
                .. " at (" .. string.format("%.1f, %.1f, %.1f", hx, hy, hz) .. ")"
            ffe.log(lastRayHitMsg)
        else
            lastRayHitMsg = "Ray hit nothing"
            ffe.log(lastRayHitMsg)
        end
    end

    -- ------------------------------------------------------------------
    -- Animate point lights (orbit around the scene)
    -- ------------------------------------------------------------------
    pointLightAngle = pointLightAngle + POINT_LIGHT_ORBIT_SPEED * dt
    if pointLightAngle > 360 then pointLightAngle = pointLightAngle - 360 end

    local plRad = math.rad(pointLightAngle)
    -- Light 0: orbits clockwise
    ffe.setPointLightPosition(0,
        math.sin(plRad) * POINT_LIGHT_RADIUS,
        POINT_LIGHT_HEIGHT,
        math.cos(plRad) * POINT_LIGHT_RADIUS)
    -- Light 1: orbits counter-clockwise (180-degree offset)
    ffe.setPointLightPosition(1,
        math.sin(plRad + math.pi) * POINT_LIGHT_RADIUS,
        POINT_LIGHT_HEIGHT,
        math.cos(plRad + math.pi) * POINT_LIGHT_RADIUS)

    -- ------------------------------------------------------------------
    -- Animate 3D entities
    -- ------------------------------------------------------------------
    if meshLoaded then

        -- Advance rotation accumulators
        rotA   = rotA   + ROTATION_SPEED_A * dt
        rotB   = rotB   + ROTATION_SPEED_B * dt
        rotC   = rotC   + ROTATION_SPEED_C * dt
        orbitB = orbitB + ORBIT_SPEED_B    * dt
        orbitC = orbitC + ORBIT_SPEED_C    * dt

        -- Wrap angles to prevent unbounded growth over long sessions.
        -- Using modulo 360 is safe for degree values.
        if rotA   > 360 then rotA   = rotA   - 360 end
        if rotB   > 360 then rotB   = rotB   - 360 end
        if rotC   > 360 then rotC   = rotC   - 360 end
        if orbitB > 360 then orbitB = orbitB - 360 end
        if orbitC > 360 then orbitC = orbitC - 360 end

        -- Entity A: stationary at origin, spins on Y only
        if entityA ~= 0 then
            ffe.setTransform3D(entityA,
                0, 0, 0,              -- position
                0, rotA, 0,           -- rotation (ry = Y spin)
                1, 1, 1)              -- scale (always explicit -- see "What NOT to Do")
        end

        -- Entity B: orbits origin in the XZ plane, also tumbles on its local axes
        if entityB ~= 0 then
            local radB = math.rad(orbitB)
            local bx = math.sin(radB) * ORBIT_RADIUS_B
            local bz = math.cos(radB) * ORBIT_RADIUS_B
            ffe.setTransform3D(entityB,
                bx, 0, bz,            -- position on orbit
                rotB, rotB * 0.5, 0,  -- tumble: rx + slower ry
                0.8, 0.8, 0.8)        -- scale
        end

        -- Entity C: wider orbit at a slight Y elevation, fast self-spin
        if entityC ~= 0 then
            local radC = math.rad(orbitC)
            local cx = math.sin(radC) * ORBIT_RADIUS_C
            local cz = math.cos(radC) * ORBIT_RADIUS_C
            -- Bob gently on Y using a sine wave
            local cy = math.sin(math.rad(orbitC * 2)) * 0.5
            ffe.setTransform3D(entityC,
                cx, cy, cz,           -- position (bobs on Y)
                0, rotC, rotC * 0.3,  -- ry spin + slight rz roll
                0.6, 0.6, 0.6)        -- scale
        end

    end

    -- ------------------------------------------------------------------
    -- 2D HUD overlay
    -- ------------------------------------------------------------------
    local sw = ffe.getScreenWidth()

    -- Title bar
    ffe.drawRect(0, 0, sw, 36, 0, 0, 0, 0.55)
    ffe.drawText("FFE  3D DEMO", 12, 8, 2, 1, 0.85, 0.3, 1)

    -- FPS counter (right-aligned)
    local fpsStr = "FPS: " .. tostring(fpsDisplay)
    ffe.drawText(fpsStr, sw - (#fpsStr * 8 * 2) - 12, 8, 2, 0.4, 0.9, 0.4, 1)

    -- Status: mesh load result
    if meshLoaded then
        local skyStr = skyboxLoaded and "Skybox: ON" or "Skybox: OFF"
        local shadowStr = softwareRenderer and "Shadows: OFF" or "Shadows: ON"
        local physStr = "Physics: " .. tostring(PHYSICS_CUBE_COUNT) .. " cubes"
        local entStr = "Mesh: cube.glb  |  " .. shadowStr .. "  |  " .. skyStr .. "  |  Lights: 2  |  " .. physStr
        ffe.drawText(entStr, sw / 2 - (#entStr * 8), 8, 2, 0.6, 0.8, 1.0, 0.85)
    else
        ffe.drawText("cube.glb NOT FOUND -- HUD only mode", 12, 44, 2, 1, 0.4, 0.2, 1)
    end

    -- Control hint bar at bottom
    ffe.drawRect(0, 680, sw, 40, 0, 0, 0, 0.5)
    ffe.drawText(
        "WASD: orbit/zoom  |  UP/DOWN: elevation  |  R: reset  |  F: raycast  |  ESC: quit",
        12, 685, 2, 0.45, 0.55, 0.65, 0.9)

    -- Color key for the three cubes and point lights
    if meshLoaded then
        ffe.drawText("RED: stationary (shininess=128)", 12, 44, 2, 0.9, 0.3, 0.2, 1)
        ffe.drawText("GREEN: inner orbit (shininess=64)", 12, 68, 2, 0.2, 0.85, 0.35, 1)
        ffe.drawText("BLUE: outer orbit (shininess=32)", 12, 92, 2, 0.3, 0.55, 1.0, 1)
        ffe.drawText("POINT LIGHT 0: warm orange (orbiting)", 12, 116, 2, 1.0, 0.6, 0.2, 1)
        ffe.drawText("POINT LIGHT 1: cool blue (orbiting)", 12, 140, 2, 0.2, 0.5, 1.0, 1)

        -- Physics info
        ffe.drawText("PHYSICS: " .. tostring(PHYSICS_CUBE_COUNT) .. " falling cubes  |  Collisions: " .. tostring(collisionCount),
            12, 172, 2, 0.9, 0.75, 0.3, 1)
        if lastRayHitMsg ~= "" then
            ffe.drawText("RAYCAST: " .. lastRayHitMsg, 12, 196, 2, 0.5, 1.0, 0.5, 1)
        end
    end

    -- ------------------------------------------------------------------
    -- ESC to quit
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- called by ScriptEngine before lua_close()
-- ---------------------------------------------------------------------------
function shutdown()
    -- Unload skybox cubemap (safe even if not loaded).
    ffe.unloadSkybox()

    -- Remove point lights before shutdown.
    ffe.removePointLight(0)
    ffe.removePointLight(1)

    -- Clean up physics bodies before destroying entities.
    for i = 1, #physicsCubes do
        if physicsCubes[i] ~= 0 then
            ffe.destroyPhysicsBody(physicsCubes[i])
        end
    end
    if physicsGround ~= 0 then
        ffe.destroyPhysicsBody(physicsGround)
    end
    physicsCubes = {}
    physicsGround = 0

    -- Disable shadows before unloading mesh to release GPU resources.
    ffe.disableShadows()

    if meshHandle ~= 0 then
        ffe.unloadMesh(meshHandle)
        meshHandle = 0
        ffe.log("cube.glb unloaded")
    end

    -- Reset entity IDs so stale values are not accidentally reused.
    entityA = 0
    entityB = 0
    entityC = 0

    ffe.log("3D demo shutdown complete")
end
