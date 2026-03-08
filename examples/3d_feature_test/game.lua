-- game.lua -- "3D Feature Showcase Arena" for FFE
--
-- A comprehensive visual test that exercises ALL 3D engine features in a
-- single scene.  The arena is divided into four zones, each demonstrating
-- a different feature set.  An orbit camera slowly rotates to show
-- everything, with manual WASD/mouse override.
--
-- Zone 1 (front-left):  Materials & Colors -- 6 colored cubes
-- Zone 2 (front-right): Lighting -- white cubes + orbiting point lights
-- Zone 3 (back-left):   Animated Models -- Fox + Cesium Man
-- Zone 4 (back-right):  PBR & Textures -- Damaged Helmet + textured cubes
-- Center:               Ground plane + falling physics cubes
--
-- Environment: Fog, SSAO, Bloom, ACES tone mapping, FXAA, shadows
--
-- Controls:
--   WASD       orbit camera (yaw/zoom)
--   UP/DOWN    raise/lower camera
--   R          reset camera to auto-orbit
--   1          toggle shadows
--   2          toggle bloom
--   3          toggle SSAO
--   4          toggle fog
--   5          toggle FXAA
--   F          fire raycast from camera
--   ESC        quit
--
-- Asset paths are relative to the shared assets/ root.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local PI = 3.14159265

-- Zone positions (quadrant centers, Y=0 ground level)
local ZONE_OFFSET  = 6.0   -- distance from center to zone center
local ZONE1_X      = -ZONE_OFFSET  -- front-left  (negative X, positive Z)
local ZONE1_Z      =  ZONE_OFFSET
local ZONE2_X      =  ZONE_OFFSET  -- front-right (positive X, positive Z)
local ZONE2_Z      =  ZONE_OFFSET
local ZONE3_X      = -ZONE_OFFSET  -- back-left   (negative X, negative Z)
local ZONE3_Z      = -ZONE_OFFSET
local ZONE4_X      =  ZONE_OFFSET  -- back-right  (positive X, negative Z)
local ZONE4_Z      = -ZONE_OFFSET

-- Camera
local CAM_AUTO_SPEED   = 12.0    -- degrees/sec for auto-orbit
local CAM_MANUAL_SPEED = 60.0    -- degrees/sec for manual orbit
local CAM_ZOOM_SPEED   = 3.0
local CAM_ELEV_SPEED   = 2.0
local CAM_DEFAULT_DIST = 22.0
local CAM_DEFAULT_ELEV = 18.0
local CAM_MIN_DIST     = 5.0
local CAM_MAX_DIST     = 35.0
local CAM_MIN_ELEV     = -5.0
local CAM_MAX_ELEV     = 20.0

-- Point light orbit
local PL_ORBIT_SPEED  = 50.0
local PL_ORBIT_RADIUS = 3.5
local PL_HEIGHT        = 2.0

-- Physics
local PHYSICS_GROUND_Y   = -1.5
local PHYSICS_SPAWN_Y    = 15.0
local PHYSICS_CUBE_COUNT = 5

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local cubeMesh     = 0
local helmetMesh   = 0
local foxMesh      = 0
local cesiumMesh   = 0
local duckMesh     = 0
local checkerTex   = 0

local meshLoaded   = false

-- Camera state
local camYaw       = 30.0
local camDist      = CAM_DEFAULT_DIST
local camElev      = CAM_DEFAULT_ELEV
local autoOrbit    = true    -- auto-orbit until user presses a camera key

-- FPS
local fpsTimer     = 0.0
local fpsFrames    = 0
local fpsDisplay   = 0

-- Time accumulator for animations
local totalTime    = 0.0

-- Point light angle
local plAngle      = 0.0

-- Feature toggle state
local shadowsOn    = true
local bloomOn      = true
local ssaoOn       = true
local fogOn        = true
local fxaaOn       = true

-- Entity storage
local groundEntity     = 0
local colorCubes       = {}    -- Zone 1: 6 colored cubes
local lightCubes       = {}    -- Zone 2: cluster of white cubes
local foxEntity        = 0
local cesiumEntity     = 0
local helmetEntity     = 0
local texCubes         = {}    -- Zone 4: textured cubes
local duckEntity       = 0
local physicsCubes     = {}    -- falling cubes
local physicsGround    = 0
local collisionCount   = 0
local lastRayMsg       = ""
local lastRayMsgTimer  = 0.0   -- seconds remaining to show ray hit message

-- Software renderer detection
local softwareRenderer = ffe.isSoftwareRenderer()

-- ---------------------------------------------------------------------------
-- Helper: update camera from state
-- ---------------------------------------------------------------------------
local function updateCamera()
    local radius = math.sqrt(camDist * camDist + camElev * camElev)
    local pitch  = math.deg(math.atan(camElev, camDist))
    ffe.set3DCameraOrbit(0, 0, 0, radius, camYaw, pitch)
end

-- ---------------------------------------------------------------------------
-- Scene Init
-- ---------------------------------------------------------------------------
ffe.log("=== 3D Feature Test: initializing ===")

-- Background color
if softwareRenderer then
    ffe.setBackgroundColor(0.12, 0.12, 0.20)
else
    ffe.setBackgroundColor(0.03, 0.03, 0.08)
end

-- Directional light (warm sun from upper-right)
ffe.setLightDirection(1.0, -1.2, 0.6)
ffe.setLightColor(1.0, 0.95, 0.85)
ffe.setAmbientColor(0.10, 0.10, 0.15)

-- Load meshes
cubeMesh   = ffe.loadMesh("models/cube.glb")
helmetMesh = ffe.loadMesh("models/damaged_helmet.glb")
foxMesh    = ffe.loadMesh("models/fox.glb")
cesiumMesh = ffe.loadMesh("models/cesium_man.glb")
duckMesh   = ffe.loadMesh("models/duck.glb")

-- Load textures
checkerTex = ffe.loadTexture("textures/checkerboard.png")

if cubeMesh == 0 then
    ffe.log("WARNING: cube.glb not found -- running in HUD-only mode")
else
    meshLoaded = true
    ffe.log("Meshes loaded: cube=" .. cubeMesh
        .. " helmet=" .. helmetMesh
        .. " fox=" .. foxMesh
        .. " cesium=" .. cesiumMesh
        .. " duck=" .. duckMesh)
end

-- Only set up the 3D scene if we have at least the cube mesh
if meshLoaded then

    -- === ENVIRONMENT SETUP (skip advanced effects on software renderer) ===
    if not softwareRenderer then
        -- Shadows
        ffe.enableShadows(1024)
        ffe.setShadowBias(0.005)
        ffe.setShadowArea(30, 30, 0.1, 50)

        -- Post-processing: bloom + ACES tone mapping
        ffe.enablePostProcessing()
        ffe.enableBloom(0.8, 0.8)
        ffe.setToneMapping(2)  -- ACES filmic

        -- FXAA anti-aliasing
        ffe.setAntiAliasing(2)

        -- SSAO
        ffe.enableSSAO(0.5, 0.025, 32)
        ffe.setSSAOIntensity(1.0)

        -- Fog (subtle atmospheric haze)
        ffe.setFog(0.55, 0.58, 0.65, 25.0, 60.0)
    else
        -- Software renderer: minimal setup
        shadowsOn = false
        bloomOn   = false
        ssaoOn    = false
        fogOn     = false
        fxaaOn    = false
    end

    -- === GROUND PLANE ===
    groundEntity = ffe.createEntity3D(cubeMesh, 0, PHYSICS_GROUND_Y, 0)
    if groundEntity ~= 0 then
        ffe.setMeshColor(groundEntity, 0.35, 0.35, 0.40, 1.0)
        ffe.setMeshSpecular(groundEntity, 0.2, 0.2, 0.2, 16)
        ffe.setTransform3D(groundEntity,
            0, PHYSICS_GROUND_Y, 0,
            0, 0, 0,
            25, 0.2, 25)
    end

    -- === TWO POINT LIGHTS (orbiting Zone 2) ===
    -- Light 0: warm orange
    ffe.addPointLight(0, ZONE2_X + PL_ORBIT_RADIUS, PL_HEIGHT, ZONE2_Z,
                      1.0, 0.65, 0.2, 10.0)
    -- Light 1: cool blue
    ffe.addPointLight(1, ZONE2_X - PL_ORBIT_RADIUS, PL_HEIGHT, ZONE2_Z,
                      0.2, 0.5, 1.0, 10.0)

    -- -----------------------------------------------------------------------
    -- ZONE 1: Materials & Colors (front-left)
    -- 6 cubes in a row, each a different color
    -- -----------------------------------------------------------------------
    local colors = {
        {0.95, 0.15, 0.15},  -- red
        {0.15, 0.85, 0.25},  -- green
        {0.20, 0.40, 1.00},  -- blue
        {0.95, 0.90, 0.10},  -- yellow
        {0.10, 0.90, 0.90},  -- cyan
        {0.90, 0.20, 0.85},  -- magenta
    }
    for i = 1, 6 do
        local xOff = (i - 3.5) * 1.5  -- spread from -3.75 to +3.75
        local e = ffe.createEntity3D(cubeMesh, ZONE1_X + xOff, 0.5, ZONE1_Z)
        if e ~= 0 then
            local c = colors[i]
            ffe.setMeshColor(e, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(e, 0.8, 0.8, 0.8, 64)
            ffe.setTransform3D(e,
                ZONE1_X + xOff, 0.5, ZONE1_Z,
                0, 0, 0,
                0.8, 0.8, 0.8)
            colorCubes[i] = e
        end
    end
    ffe.log("Zone 1: 6 colored cubes placed")

    -- -----------------------------------------------------------------------
    -- ZONE 2: Lighting (front-right)
    -- Cluster of white/grey cubes illuminated by orbiting point lights
    -- -----------------------------------------------------------------------
    local lightPositions = {
        {0, 0.5, 0},
        {1.5, 0.5, 0},
        {-1.5, 0.5, 0},
        {0, 0.5, 1.5},
        {0, 0.5, -1.5},
        {0.75, 1.5, 0.75},
        {-0.75, 1.5, -0.75},
    }
    for i, pos in ipairs(lightPositions) do
        local e = ffe.createEntity3D(cubeMesh,
            ZONE2_X + pos[1], pos[2], ZONE2_Z + pos[3])
        if e ~= 0 then
            ffe.setMeshColor(e, 0.85, 0.85, 0.90, 1.0)
            ffe.setMeshSpecular(e, 1.0, 1.0, 1.0, 128)
            local s = (i <= 5) and 0.7 or 0.5
            ffe.setTransform3D(e,
                ZONE2_X + pos[1], pos[2], ZONE2_Z + pos[3],
                0, 0, 0,
                s, s, s)
            lightCubes[i] = e
        end
    end
    ffe.log("Zone 2: " .. #lightCubes .. " lit cubes placed")

    -- -----------------------------------------------------------------------
    -- ZONE 3: Animated Models (back-left)
    -- Fox and Cesium Man playing animations via ffe.playAnimation3D.
    --
    -- NOTE: fox.glb has no embedded NORMAL attribute (only POSITION, TEXCOORD_0,
    -- JOINTS_0, WEIGHTS_0). The mesh loader falls back to default normals, so
    -- the fox renders flat. We set an explicit warm-orange tint so the model is
    -- visually distinct and animation movement is easier to see.
    --
    -- Animations are advanced automatically each tick by animationUpdateSystem3D
    -- (registered by the engine at priority 52, before gameplay at 100).
    -- Do NOT call ffe.playAnimation3D() inside update() — it resets time to 0.
    -- -----------------------------------------------------------------------
    if foxMesh ~= 0 then
        foxEntity = ffe.createEntity3D(foxMesh, ZONE3_X - 2, 0, ZONE3_Z)
        if foxEntity ~= 0 then
            ffe.setTransform3D(foxEntity,
                ZONE3_X - 2, 0, ZONE3_Z,
                0, 45, 0,
                0.02, 0.02, 0.02)  -- Fox model is large, scale down
            -- Warm orange tint — makes the fox visible against white default
            ffe.setMeshColor(foxEntity, 0.90, 0.55, 0.20, 1.0)
            ffe.setMeshSpecular(foxEntity, 0.3, 0.2, 0.1, 16)
            local animCount = ffe.getAnimationCount3D(foxEntity)
            ffe.log("Zone 3: Fox has " .. animCount .. " animation(s)")
            if animCount > 0 then
                -- Play the first animation (usually "Survey" or idle)
                ffe.playAnimation3D(foxEntity, 0, true)
                ffe.log("Zone 3: Fox animation started (clip 0)")
            else
                ffe.log("Zone 3: Fox has no animations (mesh may lack skeleton)")
            end
        end
    end

    if cesiumMesh ~= 0 then
        cesiumEntity = ffe.createEntity3D(cesiumMesh, ZONE3_X + 2, 0, ZONE3_Z)
        if cesiumEntity ~= 0 then
            ffe.setTransform3D(cesiumEntity,
                ZONE3_X + 2, 0, ZONE3_Z,
                0, -30, 0,
                1.5, 1.5, 1.5)
            -- Cool blue-grey tint — distinguishes Cesium Man from Fox
            ffe.setMeshColor(cesiumEntity, 0.35, 0.55, 0.80, 1.0)
            ffe.setMeshSpecular(cesiumEntity, 0.5, 0.5, 0.6, 32)
            local animCount = ffe.getAnimationCount3D(cesiumEntity)
            ffe.log("Zone 3: Cesium Man has " .. animCount .. " animation(s)")
            if animCount > 0 then
                ffe.playAnimation3D(cesiumEntity, 0, true)
                ffe.log("Zone 3: Cesium Man animation started (clip 0)")
            else
                ffe.log("Zone 3: Cesium Man has no animations (mesh may lack skeleton)")
            end
        end
    end

    -- -----------------------------------------------------------------------
    -- ZONE 4: PBR & Textures (back-right)
    -- Damaged Helmet (PBR showcase) + textured cubes + Duck
    -- -----------------------------------------------------------------------
    if helmetMesh ~= 0 then
        helmetEntity = ffe.createEntity3D(helmetMesh, ZONE4_X, 1.5, ZONE4_Z)
        if helmetEntity ~= 0 then
            ffe.setTransform3D(helmetEntity,
                ZONE4_X, 1.5, ZONE4_Z,
                0, 0, 0,
                1.2, 1.2, 1.2)
            ffe.log("Zone 4: Damaged Helmet placed")
        end
    end

    -- Textured cubes with checkerboard
    if checkerTex then
        for i = 1, 3 do
            local xOff = (i - 2) * 2.0
            local e = ffe.createEntity3D(cubeMesh,
                ZONE4_X + xOff, 0.5, ZONE4_Z + 3)
            if e ~= 0 then
                ffe.setMeshTexture(e, checkerTex)
                ffe.setMeshSpecular(e, 0.5, 0.5, 0.5, 32)
                ffe.setTransform3D(e,
                    ZONE4_X + xOff, 0.5, ZONE4_Z + 3,
                    0, 0, 0,
                    0.7, 0.7, 0.7)
                texCubes[i] = e
            end
        end
        ffe.log("Zone 4: 3 textured cubes placed")
    end

    -- Duck model
    if duckMesh ~= 0 then
        duckEntity = ffe.createEntity3D(duckMesh, ZONE4_X - 3, 0, ZONE4_Z - 1)
        if duckEntity ~= 0 then
            ffe.setTransform3D(duckEntity,
                ZONE4_X - 3, 0, ZONE4_Z - 1,
                0, 60, 0,
                0.015, 0.015, 0.015)  -- Duck model is large
            ffe.log("Zone 4: Duck placed")
        end
    end

    -- -----------------------------------------------------------------------
    -- PHYSICS: Falling cubes in the center
    -- -----------------------------------------------------------------------
    -- Static ground for physics
    physicsGround = ffe.createEntity3D(cubeMesh, 0, PHYSICS_GROUND_Y - 0.5, 0)
    if physicsGround ~= 0 then
        ffe.setMeshColor(physicsGround, 0.0, 0.0, 0.0, 0.0)  -- invisible
        ffe.setTransform3D(physicsGround,
            0, PHYSICS_GROUND_Y - 0.5, 0,
            0, 0, 0,
            50, 0.5, 50)
        ffe.createPhysicsBody(physicsGround, {
            shape       = "box",
            halfExtents = {50, 0.5, 50},
            motion      = "static"
        })
    end

    -- Falling dynamic cubes
    local physColors = {
        {1.0, 0.4, 0.1},   -- orange
        {0.3, 0.8, 1.0},   -- cyan
        {1.0, 0.9, 0.2},   -- yellow
        {0.8, 0.3, 0.9},   -- purple
        {0.2, 1.0, 0.4},   -- lime
    }
    for i = 1, PHYSICS_CUBE_COUNT do
        local spawnX = (i - 1) * 1.8 - 3.6
        local spawnY = PHYSICS_SPAWN_Y + (i - 1) * 2.0
        local e = ffe.createEntity3D(cubeMesh, spawnX, spawnY, 0)
        if e ~= 0 then
            local c = physColors[i]
            ffe.setMeshColor(e, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(e, 0.6, 0.6, 0.6, 48)
            ffe.setTransform3D(e,
                spawnX, spawnY, 0,
                0, 0, 0,
                0.6, 0.6, 0.6)
            ffe.createPhysicsBody(e, {
                shape       = "box",
                halfExtents = {0.3, 0.3, 0.3},
                motion      = "dynamic",
                mass        = 1.0,
                restitution = 0.5,
                friction    = 0.5
            })
            physicsCubes[i] = e
        end
    end

    -- Collision callback
    ffe.onCollision3D(function(eA, eB, px, py, pz, nx, ny, nz, eventType)
        if eventType == "enter" then
            collisionCount = collisionCount + 1
        end
    end)

    ffe.log("Physics: " .. PHYSICS_CUBE_COUNT .. " falling cubes spawned")

end -- if meshLoaded

-- Initial camera position
updateCamera()

ffe.log("=== 3D Feature Test: init complete ===")
ffe.log("Controls: WASD orbit, UP/DOWN elev, R reset, 1-5 toggle features, ESC quit")

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called per tick
-- ---------------------------------------------------------------------------
function update(entityId, dt)

    totalTime = totalTime + dt

    -- Tick down ray message display timer
    if lastRayMsgTimer > 0 then
        lastRayMsgTimer = lastRayMsgTimer - dt
        if lastRayMsgTimer <= 0 then
            lastRayMsg = ""
        end
    end

    -- ------------------------------------------------------------------
    -- FPS counter
    -- ------------------------------------------------------------------
    fpsFrames = fpsFrames + 1
    fpsTimer  = fpsTimer + dt
    if fpsTimer >= 0.5 then
        fpsDisplay = math.floor(fpsFrames / fpsTimer + 0.5)
        fpsFrames  = 0
        fpsTimer   = 0.0
    end

    -- ------------------------------------------------------------------
    -- Camera controls
    -- ------------------------------------------------------------------
    local userInput = false

    if ffe.isKeyHeld(ffe.KEY_A) then
        camYaw = camYaw - CAM_MANUAL_SPEED * dt
        userInput = true
    end
    if ffe.isKeyHeld(ffe.KEY_D) then
        camYaw = camYaw + CAM_MANUAL_SPEED * dt
        userInput = true
    end
    if ffe.isKeyHeld(ffe.KEY_W) then
        camDist = math.max(CAM_MIN_DIST, camDist - CAM_ZOOM_SPEED * dt)
        userInput = true
    end
    if ffe.isKeyHeld(ffe.KEY_S) then
        camDist = math.min(CAM_MAX_DIST, camDist + CAM_ZOOM_SPEED * dt)
        userInput = true
    end
    if ffe.isKeyHeld(ffe.KEY_UP) then
        camElev = math.min(CAM_MAX_ELEV, camElev + CAM_ELEV_SPEED * dt)
        userInput = true
    end
    if ffe.isKeyHeld(ffe.KEY_DOWN) then
        camElev = math.max(CAM_MIN_ELEV, camElev - CAM_ELEV_SPEED * dt)
        userInput = true
    end

    -- Disable auto-orbit when user manually moves camera
    if userInput then
        autoOrbit = false
    end

    -- R resets to auto-orbit
    if ffe.isKeyPressed(ffe.KEY_R) then
        autoOrbit = true
        camDist   = CAM_DEFAULT_DIST
        camElev   = CAM_DEFAULT_ELEV
    end

    -- Auto-orbit: slow rotation
    if autoOrbit then
        camYaw = camYaw + CAM_AUTO_SPEED * dt
    end

    -- Wrap yaw
    if camYaw > 360 then camYaw = camYaw - 360 end
    if camYaw < 0   then camYaw = camYaw + 360 end

    updateCamera()

    -- ------------------------------------------------------------------
    -- Feature toggles (1-5)
    -- ------------------------------------------------------------------
    if not softwareRenderer then
        if ffe.isKeyPressed(ffe.KEY_1) then
            shadowsOn = not shadowsOn
            if shadowsOn then
                ffe.enableShadows(1024)
                ffe.setShadowBias(0.005)
                ffe.setShadowArea(30, 30, 0.1, 50)
            else
                ffe.disableShadows()
            end
            ffe.log("Shadows: " .. (shadowsOn and "ON" or "OFF"))
        end

        if ffe.isKeyPressed(ffe.KEY_2) then
            bloomOn = not bloomOn
            if bloomOn then
                ffe.enableBloom(0.8, 0.8)
            else
                ffe.disableBloom()
            end
            ffe.log("Bloom: " .. (bloomOn and "ON" or "OFF"))
        end

        if ffe.isKeyPressed(ffe.KEY_3) then
            ssaoOn = not ssaoOn
            if ssaoOn then
                ffe.enableSSAO(0.5, 0.025, 32)
            else
                ffe.disableSSAO()
            end
            ffe.log("SSAO: " .. (ssaoOn and "ON" or "OFF"))
        end

        if ffe.isKeyPressed(ffe.KEY_4) then
            fogOn = not fogOn
            if fogOn then
                ffe.setFog(0.55, 0.58, 0.65, 25.0, 60.0)
            else
                ffe.disableFog()
            end
            ffe.log("Fog: " .. (fogOn and "ON" or "OFF"))
        end

        if ffe.isKeyPressed(ffe.KEY_5) then
            fxaaOn = not fxaaOn
            if fxaaOn then
                ffe.setAntiAliasing(2)
            else
                ffe.setAntiAliasing(0)
            end
            ffe.log("FXAA: " .. (fxaaOn and "ON" or "OFF"))
        end
    end

    -- ------------------------------------------------------------------
    -- Raycast (F key)
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_F) then
        local radius = math.sqrt(camDist * camDist + camElev * camElev)
        local pitch  = math.atan(camElev, camDist)
        local yawRad = math.rad(camYaw)

        local camX = math.sin(yawRad) * math.cos(pitch) * radius
        local camY = math.sin(pitch) * radius
        local camZ = math.cos(yawRad) * math.cos(pitch) * radius

        local dirX, dirY, dirZ = -camX, -camY, -camZ
        local len = math.sqrt(dirX*dirX + dirY*dirY + dirZ*dirZ)
        if len > 0.001 then
            dirX, dirY, dirZ = dirX/len, dirY/len, dirZ/len
        end

        local hitEntity, hx, hy, hz = ffe.castRay(
            camX, camY, camZ, dirX, dirY, dirZ, 100.0)
        if hitEntity then
            lastRayMsg = "Hit entity " .. tostring(hitEntity)
                .. " at " .. string.format("%.1f, %.1f, %.1f", hx, hy, hz)
            lastRayMsgTimer = 2.0
        else
            lastRayMsg = ""
            lastRayMsgTimer = 0.0
        end
    end

    -- ------------------------------------------------------------------
    -- Animate scene elements
    -- ------------------------------------------------------------------
    if meshLoaded then

        -- Zone 1: Slowly rotate color cubes on their Y axis
        for i, e in ipairs(colorCubes) do
            if e ~= 0 then
                local xOff = (i - 3.5) * 1.5
                local rot  = (totalTime * 30 + i * 60) % 360
                ffe.setTransform3D(e,
                    ZONE1_X + xOff, 0.5 + math.sin(totalTime * 2 + i) * 0.2,
                    ZONE1_Z,
                    0, rot, 0,
                    0.8, 0.8, 0.8)
            end
        end

        -- Zone 2: Point light orbits around the cube cluster
        plAngle = plAngle + PL_ORBIT_SPEED * dt
        if plAngle > 360 then plAngle = plAngle - 360 end
        local plRad = math.rad(plAngle)
        ffe.setPointLightPosition(0,
            ZONE2_X + math.sin(plRad) * PL_ORBIT_RADIUS,
            PL_HEIGHT,
            ZONE2_Z + math.cos(plRad) * PL_ORBIT_RADIUS)
        ffe.setPointLightPosition(1,
            ZONE2_X + math.sin(plRad + PI) * PL_ORBIT_RADIUS,
            PL_HEIGHT,
            ZONE2_Z + math.cos(plRad + PI) * PL_ORBIT_RADIUS)

        -- Zone 4: Slowly rotate the helmet
        if helmetEntity ~= 0 then
            local hRot = (totalTime * 20) % 360
            ffe.setTransform3D(helmetEntity,
                ZONE4_X, 1.5, ZONE4_Z,
                0, hRot, 0,
                1.2, 1.2, 1.2)
        end

        -- Zone 4: Rotate textured cubes
        for i, e in ipairs(texCubes) do
            if e ~= 0 then
                local xOff = (i - 2) * 2.0
                local rot  = (totalTime * 40 + i * 120) % 360
                ffe.setTransform3D(e,
                    ZONE4_X + xOff, 0.5, ZONE4_Z + 3,
                    rot * 0.3, rot, 0,
                    0.7, 0.7, 0.7)
            end
        end

        -- Zone 4: Rotate duck
        if duckEntity ~= 0 then
            local dRot = (totalTime * 25) % 360
            ffe.setTransform3D(duckEntity,
                ZONE4_X - 3, 0, ZONE4_Z - 1,
                0, dRot, 0,
                0.015, 0.015, 0.015)
        end

        -- Zone 3: Animations are advanced automatically by the engine's
        -- animationUpdateSystem3D (priority 52, before this system at 100).
        -- No per-frame Lua calls needed -- calling playAnimation3D() each frame
        -- would reset the animation to t=0 every tick, freezing it.

    end -- meshLoaded

    -- ------------------------------------------------------------------
    -- HUD overlay
    -- ------------------------------------------------------------------
    local sw = ffe.getScreenWidth()
    local sh = ffe.getScreenHeight()

    -- Title bar
    ffe.drawRect(0, 0, sw, 36, 0, 0, 0, 0.6)
    ffe.drawText("FFE  3D FEATURE TEST", 12, 8, 2, 1.0, 0.85, 0.3, 1)

    -- FPS (right-aligned)
    local fpsStr = "FPS: " .. tostring(fpsDisplay)
    ffe.drawText(fpsStr, sw - (#fpsStr * 16) - 12, 8, 2, 0.4, 0.9, 0.4, 1)

    -- Zone labels
    if meshLoaded then
        -- Feature status bar
        local statusY = 44
        local function statusText(label, on)
            if on then
                return label .. ":ON"
            else
                return label .. ":OFF"
            end
        end

        local featureStr = statusText("1:Shadows", shadowsOn)
            .. "  " .. statusText("2:Bloom", bloomOn)
            .. "  " .. statusText("3:SSAO", ssaoOn)
            .. "  " .. statusText("4:Fog", fogOn)
            .. "  " .. statusText("5:FXAA", fxaaOn)
        ffe.drawText(featureStr, 12, statusY, 2, 0.7, 0.8, 1.0, 1)

        -- Zone info
        ffe.drawText("Zone 1: Materials (6 colors)", 12, statusY + 24, 2,
            0.9, 0.3, 0.2, 0.9)
        ffe.drawText("Zone 2: Lighting (point lights + shadows)", 12, statusY + 44, 2,
            1.0, 0.8, 0.3, 0.9)
        local foxAnim   = (foxEntity ~= 0 and ffe.isAnimation3DPlaying(foxEntity))
        local cesiumAnim = (cesiumEntity ~= 0 and ffe.isAnimation3DPlaying(cesiumEntity))
        local animStatus = (foxAnim and "Fox:ANIM" or "Fox:IDLE")
            .. " | " .. (cesiumAnim and "Cesium:ANIM" or "Cesium:IDLE")
        ffe.drawText("Zone 3: Animation -- " .. animStatus, 12, statusY + 64, 2,
            0.3, 0.9, 0.4, 0.9)
        ffe.drawText("Zone 4: PBR + Textures (Helmet + Duck)", 12, statusY + 84, 2,
            0.4, 0.6, 1.0, 0.9)

        -- Physics + Ray info
        ffe.drawText("Physics: " .. PHYSICS_CUBE_COUNT .. " cubes | Collisions: "
            .. tostring(collisionCount), 12, statusY + 112, 2,
            0.9, 0.75, 0.3, 0.9)
        if lastRayMsg ~= "" then
            ffe.drawText("Ray: " .. lastRayMsg, 12, statusY + 132, 2,
                0.5, 1.0, 0.5, 0.9)
        end
    else
        ffe.drawText("cube.glb NOT FOUND -- HUD only mode", 12, 44, 2,
            1, 0.4, 0.2, 1)
    end

    -- Control hint bar at bottom
    ffe.drawRect(0, sh - 40, sw, 40, 0, 0, 0, 0.55)
    ffe.drawText(
        "WASD: orbit  |  UP/DOWN: elev  |  R: auto-orbit  |  1-5: toggles  |  F: ray  |  ESC: quit",
        12, sh - 35, 2, 0.45, 0.55, 0.65, 0.9)

    -- Camera mode indicator
    if autoOrbit then
        ffe.drawText("AUTO-ORBIT", sw - 180, sh - 35, 2, 0.3, 1.0, 0.6, 0.9)
    else
        ffe.drawText("MANUAL", sw - 120, sh - 35, 2, 1.0, 0.6, 0.3, 0.9)
    end

    -- ------------------------------------------------------------------
    -- ESC to quit
    -- ------------------------------------------------------------------
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- cleanup
-- ---------------------------------------------------------------------------
function shutdown()
    ffe.log("3D Feature Test: shutting down")

    -- Remove point lights
    ffe.removePointLight(0)
    ffe.removePointLight(1)

    -- Clean up physics bodies
    for i = 1, #physicsCubes do
        if physicsCubes[i] ~= 0 then
            ffe.destroyPhysicsBody(physicsCubes[i])
        end
    end
    if physicsGround ~= 0 then
        ffe.destroyPhysicsBody(physicsGround)
    end

    -- Disable effects
    ffe.disableShadows()
    ffe.disableSSAO()
    ffe.disableFog()
    ffe.disableBloom()

    -- Unload meshes
    if cubeMesh   ~= 0 then ffe.unloadMesh(cubeMesh)   end
    if helmetMesh ~= 0 then ffe.unloadMesh(helmetMesh) end
    if foxMesh    ~= 0 then ffe.unloadMesh(foxMesh)    end
    if cesiumMesh ~= 0 then ffe.unloadMesh(cesiumMesh) end
    if duckMesh   ~= 0 then ffe.unloadMesh(duckMesh)   end

    -- Unload textures
    if checkerTex ~= 0 and checkerTex ~= nil then
        ffe.unloadTexture(checkerTex)
    end

    ffe.log("3D Feature Test: shutdown complete")
end
