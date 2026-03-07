-- levels/level3.lua -- "The Summit" (Level 3) for "Echoes of the Ancients"
--
-- An open-air tower summit above the clouds. Floating stone platforms,
-- ancient pillars, dramatic sunset lighting, and a central battle arena.
-- The final level of the showcase game.
--
-- GAMEPLAY:
--   Platforming: jump across floating stepping stones to reach the arena
--   Moving platforms: 3 platforms oscillate via sine/cosine motion
--   Guardians: 4 guardians on separate floating platforms (one per quadrant)
--   Victory: collect the final artifact on the central altar
--
-- Entity budget: ~60 mesh entities, 3 point lights, ~30 physics bodies
-- Well within LEGACY-tier 60 fps budget.

ffe.log("[Level3] Loading The Summit...")

--------------------------------------------------------------------
-- Load mesh assets
--------------------------------------------------------------------
local cubeMesh = ffe.loadMesh("models/cube.glb")
if cubeMesh == 0 then
    ffe.log("[Level3] WARNING: cube.glb not found -- geometry will be missing")
    ffe.log("[Level3] Place cube.glb at assets/models/cube.glb")
end

local foxMesh = ffe.loadMesh("models/fox.glb")
if foxMesh == 0 then foxMesh = cubeMesh end

local helmetMesh = ffe.loadMesh("models/damaged_helmet.glb")
if helmetMesh == 0 then helmetMesh = cubeMesh end

local duckMesh = ffe.loadMesh("models/duck.glb")
if duckMesh == 0 then duckMesh = cubeMesh end

local cesiumMesh = ffe.loadMesh("models/cesium_man.glb")
if cesiumMesh == 0 then cesiumMesh = cubeMesh end

ffe.log("[Level3] Meshes loaded: cube=" .. tostring(cubeMesh)
    .. " fox=" .. tostring(foxMesh) .. " helmet=" .. tostring(helmetMesh)
    .. " duck=" .. tostring(duckMesh))

--------------------------------------------------------------------
-- Load audio assets
--------------------------------------------------------------------
local sfxCollect = ffe.loadSound("audio/sfx_collect.wav")
local sfxHit     = ffe.loadSound("audio/sfx_hit.wav")
local sfxGate    = ffe.loadSound("audio/sfx_gate.wav")
local musicHandle = ffe.loadMusic("audio/music_pixelcrown.ogg")

-- Start background music (epic/adventurous for the finale)
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.35)
end

--------------------------------------------------------------------
-- Lighting: dramatic low-angle sunset
--------------------------------------------------------------------
-- Warm orange directional light from low angle (sunset feel)
ffe.setLightDirection(0.8, 0.4, 0.1)
ffe.setLightColor(1.0, 0.7, 0.3)

-- Warm purple-pink ambient for twilight atmosphere
ffe.setAmbientColor(0.2, 0.1, 0.15)

-- Shadows: wide area for the outdoor summit
ffe.enableShadows(1024)
ffe.setShadowBias(0.005)
ffe.setShadowArea(40, 40, 0.1, 80)

--------------------------------------------------------------------
-- Fog: cloud layer effect (warm orange-purple)
--------------------------------------------------------------------
ffe.setFog(0.6, 0.35, 0.5, 10.0, 40.0)

-- Deep purple-blue background (void below the clouds)
ffe.setBackgroundColor(0.15, 0.08, 0.2)

--------------------------------------------------------------------
-- Skybox: unload any previous skybox; use fog + background for sky
--------------------------------------------------------------------
ffe.unloadSkybox()

--------------------------------------------------------------------
-- Helper: create a static box entity with mesh, color, and physics
--------------------------------------------------------------------
local function createStaticBox(x, y, z, sx, sy, sz, r, g, b, rx, ry, rz)
    rx = rx or 0
    ry = ry or 0
    rz = rz or 0
    local ent = 0
    if cubeMesh ~= 0 then
        ent = ffe.createEntity3D(cubeMesh, x, y, z)
        if ent ~= 0 then
            ffe.setTransform3D(ent, x, y, z, rx, ry, rz, sx, sy, sz)
            ffe.setMeshColor(ent, r, g, b, 1.0)
            ffe.setMeshSpecular(ent, 0.2, 0.15, 0.1, 16)
            ffe.createPhysicsBody(ent, {
                shape       = "box",
                halfExtents = { sx, sy, sz },
                motion      = "static",
            })
        end
    end
    return ent
end

--------------------------------------------------------------------
-- Helper: create a visual-only box (no physics)
--------------------------------------------------------------------
local function createVisualBox(x, y, z, sx, sy, sz, r, g, b, a, rx, ry, rz)
    rx = rx or 0
    ry = ry or 0
    rz = rz or 0
    a  = a or 1.0
    local ent = 0
    if cubeMesh ~= 0 then
        ent = ffe.createEntity3D(cubeMesh, x, y, z)
        if ent ~= 0 then
            ffe.setTransform3D(ent, x, y, z, rx, ry, rz, sx, sy, sz)
            ffe.setMeshColor(ent, r, g, b, a)
        end
    end
    return ent
end

--------------------------------------------------------------------
-- CENTRAL ARENA: Large circular-ish platform (12x1x12) at Y=0
-- The main battleground where the final artifact awaits.
--------------------------------------------------------------------
-- Main arena floor (layered for visual interest)
createStaticBox(0, -0.5, 0, 6, 0.5, 6, 0.45, 0.35, 0.28)

-- Arena edge ring (decorative border boxes around the perimeter)
createVisualBox(0, -0.3, 0, 6.5, 0.2, 6.5, 0.35, 0.25, 0.2, 1.0)

-- Arena inner ring (slightly raised center)
createStaticBox(0, 0.05, 0, 3.5, 0.05, 3.5, 0.5, 0.4, 0.32)

--------------------------------------------------------------------
-- ALTAR: Central raised platform for the final artifact
--------------------------------------------------------------------
-- Altar base
createStaticBox(0, 0.4, 0, 1.5, 0.4, 1.5, 0.55, 0.42, 0.3)
-- Altar top step
createStaticBox(0, 0.9, 0, 0.8, 0.1, 0.8, 0.6, 0.5, 0.35)

--------------------------------------------------------------------
-- STAIRWAY PLATFORMS: 4 ascending stepping stones from spawn to arena
-- Player spawns at (0, 3, -15) and jumps across these to reach the arena.
--------------------------------------------------------------------
-- Step 1: spawn platform (wide, safe landing)
createStaticBox(0, 2.5, -15, 2.5, 0.3, 2.5, 0.5, 0.38, 0.3)
-- Step 2: slightly higher
createStaticBox(0, 1.8, -11, 2.0, 0.25, 2.0, 0.48, 0.36, 0.28)
-- Step 3: closer to arena height
createStaticBox(0, 1.0, -7.5, 1.8, 0.25, 1.8, 0.46, 0.34, 0.26)
-- Step 4: final jump onto arena edge
createStaticBox(0, 0.3, -4.5, 1.5, 0.2, 1.5, 0.44, 0.33, 0.25)

--------------------------------------------------------------------
-- STATIC FLOATING PLATFORMS: 5 platforms at varying heights
-- Arranged in a ring around the central arena.
--------------------------------------------------------------------
local staticPlatforms = {
    -- NE platform (guardian 1)
    { x =  10, y = 3.0, z = -8,  sx = 2.5, sy = 0.3, sz = 2.5, r = 0.42, g = 0.32, b = 0.25 },
    -- NW platform (guardian 2)
    { x = -10, y = 4.0, z = -8,  sx = 2.0, sy = 0.3, sz = 2.0, r = 0.4,  g = 0.3,  b = 0.23 },
    -- SE platform (gem pickup)
    { x =  12, y = 2.0, z =  6,  sx = 2.0, sy = 0.25, sz = 2.0, r = 0.44, g = 0.34, b = 0.26 },
    -- SW platform (guardian 3)
    { x = -11, y = 5.0, z =  7,  sx = 2.5, sy = 0.3, sz = 2.5, r = 0.38, g = 0.28, b = 0.22 },
    -- Far north high platform (gem pickup)
    { x =  0,  y = 6.0, z =  14, sx = 1.8, sy = 0.25, sz = 1.8, r = 0.46, g = 0.36, b = 0.28 },
}

for _, p in ipairs(staticPlatforms) do
    createStaticBox(p.x, p.y, p.z, p.sx, p.sy, p.sz, p.r, p.g, p.b)
end

--------------------------------------------------------------------
-- MOVING PLATFORMS: 3 platforms oscillate on sine/cosine paths
-- Period ~3-4 seconds. Updated via ffe.every timer.
--------------------------------------------------------------------
local movingPlatforms = {}

-- Moving platform A: oscillates on X axis (connects NE region)
local movPlatA = createStaticBox(7, 2.0, -3, 1.5, 0.2, 1.5, 0.55, 0.45, 0.3)
if movPlatA ~= 0 then
    movingPlatforms[#movingPlatforms + 1] = {
        entity = movPlatA,
        baseX  = 7,   baseY = 2.0, baseZ = -3,
        sx     = 1.5, sy    = 0.2, sz    = 1.5,
        ampX   = 3.0, ampY  = 0,   ampZ  = 0,
        speed  = 1.8,  -- radians per second
        offset = 0,
    }
end

-- Moving platform B: oscillates on Y axis (vertical elevator near SW)
local movPlatB = createStaticBox(-7, 1.5, 5, 1.5, 0.2, 1.5, 0.55, 0.45, 0.3)
if movPlatB ~= 0 then
    movingPlatforms[#movingPlatforms + 1] = {
        entity = movPlatB,
        baseX  = -7,  baseY = 1.5, baseZ = 5,
        sx     = 1.5, sy    = 0.2, sz    = 1.5,
        ampX   = 0,   ampY  = 3.5, ampZ  = 0,
        speed  = 1.5,
        offset = 1.0,  -- phase offset
    }
end

-- Moving platform C: oscillates on Z axis (connects to far north, hard to reach)
local movPlatC = createStaticBox(0, 5.5, 9, 1.2, 0.2, 1.2, 0.6, 0.5, 0.35)
if movPlatC ~= 0 then
    movingPlatforms[#movingPlatforms + 1] = {
        entity = movPlatC,
        baseX  = 0,   baseY = 5.5, baseZ = 9,
        sx     = 1.2, sy    = 0.2, sz    = 1.2,
        ampX   = 0,   ampY  = 0,   ampZ  = 4.0,
        speed  = 1.6,
        offset = 2.0,
    }
end

--------------------------------------------------------------------
-- STONE PILLARS: Tall pillars rising from platforms like ancient ruins
--------------------------------------------------------------------
-- Pillars on the central arena (4 corners)
createVisualBox(-5, 4, -5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)
createVisualBox( 5, 4, -5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)
createVisualBox(-5, 4,  5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)
createVisualBox( 5, 4,  5, 0.5, 4, 0.5, 0.38, 0.28, 0.22, 1.0)

-- Pillars on floating platforms (decorative ruins reaching into the sky)
createVisualBox( 10, 6.5, -8, 0.4, 3.5, 0.4, 0.36, 0.26, 0.2, 1.0)
createVisualBox(-10, 7.5, -8, 0.4, 3.5, 0.4, 0.36, 0.26, 0.2, 1.0)
createVisualBox(-11, 9.0,  7, 0.45, 4.0, 0.45, 0.34, 0.24, 0.18, 1.0)
createVisualBox(  0, 9.5, 14, 0.35, 3.5, 0.35, 0.34, 0.24, 0.18, 1.0)

-- Broken pillar stumps on the arena edge (atmosphere)
createVisualBox( 6, 1.0, 0, 0.4, 1.0, 0.4, 0.4, 0.3, 0.24, 1.0)
createVisualBox(-6, 0.8, 0, 0.35, 0.8, 0.35, 0.4, 0.3, 0.24, 1.0)
createVisualBox( 0, 0.6, 6, 0.3, 0.6, 0.3, 0.4, 0.3, 0.24, 1.0)

--------------------------------------------------------------------
-- POINT LIGHTS: Warm golden brazier glow on key platforms
-- (max 4 point lights; use 3 to leave headroom)
--------------------------------------------------------------------
-- Brazier on the central altar (bright warm gold)
ffe.addPointLight(0, 0, 2.0, 0, 1.0, 0.8, 0.4, 12)

-- Brazier on NE floating platform
ffe.addPointLight(1, 10, 4.5, -8, 1.0, 0.75, 0.3, 10)

-- Brazier on SW floating platform
ffe.addPointLight(2, -11, 6.5, 7, 1.0, 0.7, 0.25, 10)

--------------------------------------------------------------------
-- MAIN ARTIFACT: The FINAL artifact (damaged_helmet) on the central altar
--------------------------------------------------------------------
local mainArtifact = 0
local mainArtifactPos = { x = 0, y = 1.8, z = 0 }

if helmetMesh ~= 0 then
    mainArtifact = ffe.createEntity3D(helmetMesh,
        mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z)
    if mainArtifact ~= 0 then
        ffe.setTransform3D(mainArtifact,
            mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z,
            0, 0, 0, 0.8, 0.8, 0.8)
        ffe.setMeshColor(mainArtifact, 1.0, 0.85, 0.1, 1.0)
        ffe.setMeshSpecular(mainArtifact, 1.0, 0.9, 0.4, 128)
        ffe.createPhysicsBody(mainArtifact, {
            shape       = "box",
            halfExtents = { 0.5, 0.5, 0.5 },
            motion      = "kinematic",
        })
    end
end

--------------------------------------------------------------------
-- OPTIONAL GEM PICKUPS: 3 on floating platforms + 1 hidden on moving platform
--------------------------------------------------------------------
local gems = {}
local gemPositions = {
    { x =  12, y = 2.6, z =  6   },  -- SE static platform
    { x =   0, y = 6.6, z =  14  },  -- far north high platform
    { x = -10, y = 4.6, z = -8   },  -- NW static platform
}
local gemColors = {
    { 0.9, 0.6, 0.1 },   -- topaz (warm, sunset theme)
    { 1.0, 0.3, 0.5 },   -- rose
    { 0.4, 0.8, 1.0 },   -- sky blue
}

for i, pos in ipairs(gemPositions) do
    if duckMesh ~= 0 then
        local gem = ffe.createEntity3D(duckMesh, pos.x, pos.y, pos.z)
        if gem ~= 0 then
            ffe.setTransform3D(gem, pos.x, pos.y, pos.z, 0, 0, 0, 0.008, 0.008, 0.008)
            local c = gemColors[i]
            ffe.setMeshColor(gem, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(gem, 0.8, 0.8, 0.8, 128)
            ffe.createPhysicsBody(gem, {
                shape       = "box",
                halfExtents = { 0.35, 0.35, 0.35 },
                motion      = "kinematic",
            })
            gems[gem] = { index = i, collected = false, pos = pos }
        end
    end
end

-- Hidden bonus gem: on the Z-oscillating moving platform (hard to reach!)
local hiddenGemPos = { x = 0, y = 6.2, z = 9 }
local hiddenGem = 0
if duckMesh ~= 0 then
    hiddenGem = ffe.createEntity3D(duckMesh,
        hiddenGemPos.x, hiddenGemPos.y, hiddenGemPos.z)
    if hiddenGem ~= 0 then
        ffe.setTransform3D(hiddenGem,
            hiddenGemPos.x, hiddenGemPos.y, hiddenGemPos.z,
            0, 0, 0, 0.008, 0.008, 0.008)
        ffe.setMeshColor(hiddenGem, 1.0, 1.0, 0.2, 1.0)  -- bright gold
        ffe.setMeshSpecular(hiddenGem, 1.0, 1.0, 0.5, 128)
        ffe.createPhysicsBody(hiddenGem, {
            shape       = "box",
            halfExtents = { 0.35, 0.35, 0.35 },
            motion      = "kinematic",
        })
        gems[hiddenGem] = { index = 4, collected = false, pos = hiddenGemPos }
    end
end

-- Track all rotating entities (artifact + gems) for animation
local artifactEntities = {}
if mainArtifact ~= 0 then
    artifactEntities[#artifactEntities + 1] = {
        entity = mainArtifact,
        pos    = mainArtifactPos,
        scale  = 0.8,
        isMain = true,
    }
end
for entId, data in pairs(gems) do
    artifactEntities[#artifactEntities + 1] = {
        entity = entId,
        pos    = data.pos,
        scale  = 0.008,
        isMain = false,
    }
end

--------------------------------------------------------------------
-- Level state
--------------------------------------------------------------------
local artifactAngle  = 0
local gemsCollected  = 0
local totalGems      = 4   -- 3 normal + 1 hidden
local mainCollected  = false
local levelComplete  = false
local elapsed        = 0   -- global elapsed time for moving platforms
local victoryTimer   = 0   -- countdown after collecting final artifact

-- Set global artifact count for game.lua
totalArtifacts = 1  -- only the main artifact is needed for game.lua tracking

--------------------------------------------------------------------
-- GUARDIANS: 4 guardians on separate floating platforms
-- One per quadrant: 2 regular (80 HP), 1 tough (120 HP), 1 boss (200 HP)
--------------------------------------------------------------------
AI.reset()

-- Guardian 1 (NE platform): regular, 80 HP
local guardian1 = 0
if foxMesh ~= 0 then
    local gx, gy, gz = 10, 3.8, -8
    guardian1 = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if guardian1 ~= 0 then
        ffe.setTransform3D(guardian1, gx, gy, gz, 0, 0, 0, 0.03, 0.03, 0.03)
        ffe.setMeshColor(guardian1, 0.9, 0.4, 0.15, 1.0)  -- orange-red sunset tint
        ffe.setMeshSpecular(guardian1, 0.4, 0.2, 0.1, 32)
        ffe.createPhysicsBody(guardian1, {
            shape       = "box",
            halfExtents = { 0.7, 0.65, 1.0 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian1, {
            { x =  9,  y = 3.8, z = -9 },
            { x =  11, y = 3.8, z = -9 },
            { x =  11, y = 3.8, z = -7 },
            { x =  9,  y = 3.8, z = -7 },
        }, 80)
    end
end

-- Guardian 2 (NW platform): regular, 80 HP
local guardian2 = 0
if foxMesh ~= 0 then
    local gx, gy, gz = -10, 4.8, -8
    guardian2 = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if guardian2 ~= 0 then
        ffe.setTransform3D(guardian2, gx, gy, gz, 0, 0, 0, 0.03, 0.03, 0.03)
        ffe.setMeshColor(guardian2, 0.9, 0.4, 0.15, 1.0)
        ffe.setMeshSpecular(guardian2, 0.4, 0.2, 0.1, 32)
        ffe.createPhysicsBody(guardian2, {
            shape       = "box",
            halfExtents = { 0.7, 0.65, 1.0 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian2, {
            { x = -11, y = 4.8, z = -9 },
            { x =  -9, y = 4.8, z = -9 },
            { x =  -9, y = 4.8, z = -7 },
            { x = -11, y = 4.8, z = -7 },
        }, 80)
    end
end

-- Guardian 3 (SW platform): tough, 120 HP
local guardian3 = 0
if foxMesh ~= 0 then
    local gx, gy, gz = -11, 5.8, 7
    guardian3 = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if guardian3 ~= 0 then
        ffe.setTransform3D(guardian3, gx, gy, gz, 0, 0, 0, 0.035, 0.035, 0.035)
        ffe.setMeshColor(guardian3, 0.8, 0.3, 0.1, 1.0)  -- darker, meaner
        ffe.setMeshSpecular(guardian3, 0.5, 0.25, 0.1, 48)
        ffe.createPhysicsBody(guardian3, {
            shape       = "box",
            halfExtents = { 0.8, 0.7, 1.1 },
            motion      = "dynamic",
            mass        = 4.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian3, {
            { x = -12, y = 5.8, z =  6 },
            { x = -10, y = 5.8, z =  6 },
            { x = -10, y = 5.8, z =  8 },
            { x = -12, y = 5.8, z =  8 },
        }, 120)
    end
end

-- Guardian 4 (central arena): BOSS, 200 HP, larger, gold tint
local bossGuardian = 0
if foxMesh ~= 0 then
    local gx, gy, gz = 4, 0.8, 4
    bossGuardian = ffe.createEntity3D(foxMesh, gx, gy, gz)
    if bossGuardian ~= 0 then
        -- 1.5x scale compared to regular guardians
        ffe.setTransform3D(bossGuardian, gx, gy, gz, 0, 0, 0, 0.045, 0.045, 0.045)
        -- Gold tint: the ancient summit guardian
        ffe.setMeshColor(bossGuardian, 1.0, 0.8, 0.2, 1.0)
        ffe.setMeshSpecular(bossGuardian, 0.9, 0.7, 0.3, 64)
        ffe.createPhysicsBody(bossGuardian, {
            shape       = "box",
            halfExtents = { 1.0, 0.9, 1.3 },
            motion      = "dynamic",
            mass        = 6.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(bossGuardian, {
            { x =  4, y = 0.8, z =  4 },
            { x = -4, y = 0.8, z =  4 },
            { x = -4, y = 0.8, z = -4 },
            { x =  4, y = 0.8, z = -4 },
        }, 200)
    end
end

--------------------------------------------------------------------
-- Collision callback: artifact/gem pickups
--------------------------------------------------------------------
ffe.onCollision3D(function(entityA, entityB, px, py, pz, nx, ny, nz, eventType)
    if eventType ~= "enter" then return end

    local playerEnt = Player and Player.getEntity() or 0
    if playerEnt == 0 then return end

    -- Determine which entity is not the player
    local otherEnt = nil
    if entityA == playerEnt then otherEnt = entityB
    elseif entityB == playerEnt then otherEnt = entityA
    end
    if not otherEnt then return end

    -- Check if it is the main artifact (victory condition)
    if otherEnt == mainArtifact and not mainCollected then
        mainCollected = true
        ffe.destroyPhysicsBody(otherEnt)
        ffe.destroyEntity(otherEnt)
        mainArtifact = 0

        -- Remove from rotation list
        for i, ad in ipairs(artifactEntities) do
            if ad.entity == otherEnt then
                table.remove(artifactEntities, i)
                break
            end
        end

        -- Victory sequence!
        if sfxCollect then ffe.playSound(sfxCollect, 1.0) end
        ffe.cameraShake(2.0, 0.5)

        if HUD then
            HUD.showPrompt("THE FINAL ARTIFACT! You have conquered The Summit!", 5.0)
        end
        ffe.log("[Level3] FINAL ARTIFACT COLLECTED! Victory!")

        -- Trigger game.lua artifact collection (starts LEVEL_COMPLETE state)
        if collectArtifact then collectArtifact() end

        -- Start victory timer for dramatic delay
        victoryTimer = 3.0
        levelComplete = true
        return
    end

    -- Check if it is a gem
    if gems[otherEnt] and not gems[otherEnt].collected then
        gems[otherEnt].collected = true
        ffe.destroyPhysicsBody(otherEnt)
        ffe.destroyEntity(otherEnt)

        -- Remove from rotation list
        for i, ad in ipairs(artifactEntities) do
            if ad.entity == otherEnt then
                table.remove(artifactEntities, i)
                break
            end
        end

        gemsCollected = gemsCollected + 1
        if sfxCollect then ffe.playSound(sfxCollect, 0.8) end
        ffe.cameraShake(0.3, 0.1)
        if HUD then
            HUD.showPrompt("Gem collected! (" .. tostring(gemsCollected)
                .. "/" .. tostring(totalGems) .. ")", 2.0)
        end
        ffe.log("[Level3] Gem collected: " .. tostring(gemsCollected)
            .. "/" .. tostring(totalGems))
        return
    end
end)

--------------------------------------------------------------------
-- Player spawn (on first stepping stone platform)
--------------------------------------------------------------------
Player.create(0, 3.5, -15, cesiumMesh)
Camera.setPosition(0, 3.5, -15)
Camera.setYawPitch(0, 20)  -- slight downward pitch to see the arena below

if HUD then
    HUD.showPrompt("The Summit -- Reach the central altar and claim the final artifact!", 5.0)
end

--------------------------------------------------------------------
-- Per-frame level tick
--------------------------------------------------------------------
local TICK_RATE = 0.016  -- ~60 Hz

ffe.every(TICK_RATE, function()
    if getGameState and getGameState() ~= "PLAYING" then return end

    elapsed = elapsed + TICK_RATE

    -- ================================================================
    -- Moving platforms: update positions with sine/cosine oscillation
    -- ================================================================
    for _, mp in ipairs(movingPlatforms) do
        local t = elapsed * mp.speed + mp.offset
        local nx = mp.baseX + mp.ampX * math.sin(t)
        local ny = mp.baseY + mp.ampY * math.sin(t)
        local nz = mp.baseZ + mp.ampZ * math.sin(t)
        ffe.setTransform3D(mp.entity, nx, ny, nz, 0, 0, 0, mp.sx, mp.sy, mp.sz)
    end

    -- Update hidden gem position to track moving platform C
    if hiddenGem ~= 0 and not (gems[hiddenGem] and gems[hiddenGem].collected) then
        local mp = movingPlatforms[3]  -- platform C
        if mp then
            local t = elapsed * mp.speed + mp.offset
            local pz = mp.baseZ + mp.ampZ * math.sin(t)
            local gemY = mp.baseY + mp.ampY * math.sin(t) + 0.7
            hiddenGemPos.z = pz
            hiddenGemPos.y = gemY

            -- Update gem position in the artifact tracking table
            for _, ad in ipairs(artifactEntities) do
                if ad.entity == hiddenGem then
                    ad.pos.z = pz
                    ad.pos.y = gemY
                    break
                end
            end
        end
    end

    -- ================================================================
    -- Rotate and bob artifacts / gems
    -- ================================================================
    artifactAngle = artifactAngle + 90 * TICK_RATE
    if artifactAngle > 360 then artifactAngle = artifactAngle - 360 end

    for _, ad in ipairs(artifactEntities) do
        local bobY = ad.pos.y + math.sin(math.rad(artifactAngle * 2)) * 0.3
        ffe.setTransform3D(ad.entity, ad.pos.x, bobY, ad.pos.z,
            0, artifactAngle, 0,
            ad.scale, ad.scale, ad.scale)
    end

    -- ================================================================
    -- Victory timer: dramatic pause then level complete
    -- ================================================================
    if levelComplete and victoryTimer > 0 then
        victoryTimer = victoryTimer - TICK_RATE

        -- Periodic camera shakes during victory sequence
        if victoryTimer > 1.5 and math.floor(victoryTimer * 4) % 2 == 0 then
            ffe.cameraShake(0.5, 0.1)
        end
    end

    -- ================================================================
    -- Fall detection: respawn at central arena if player falls below clouds
    -- ================================================================
    if Player then
        local px, py, pz = Player.getPosition()
        if py < -5 then
            Player.cleanup()
            Player.create(0, 2.0, 0, cesiumMesh)
            Camera.setPosition(0, 2.0, 0)
            if HUD then HUD.showPrompt("Lost in the clouds... regaining footing!", 2.0) end
        end
    end
end)

--------------------------------------------------------------------
-- Done
--------------------------------------------------------------------
ffe.log("[Level3] The Summit loaded successfully!")
ffe.log("[Level3] Objectives:")
ffe.log("[Level3]   1. Jump across the floating stepping stones to reach the arena")
ffe.log("[Level3]   2. Navigate moving platforms to reach distant areas")
ffe.log("[Level3]   3. Defeat or avoid the 4 summit guardians")
ffe.log("[Level3]   4. Collect the Final Artifact from the central altar")
ffe.log("[Level3]   5. Explore for hidden gems on floating platforms")
ffe.log("[Level3] Controls: WASD move, SPACE jump, LMB attack, Right-click drag camera")
