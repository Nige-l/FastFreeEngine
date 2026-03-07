-- levels/level2.lua -- "The Temple" (Level 2) for "Echoes of the Ancients"
--
-- A dark underground temple with pillars, a lava pit, narrow bridges,
-- glowing crystals, and a central altar. Dramatic colored point lights
-- in near-darkness create an oppressive, atmospheric environment.
--
-- Entity budget: ~57 mesh entities, 4 point lights, ~31 physics bodies
-- Well within LEGACY-tier 60 fps budget.

ffe.log("[Level2] Loading The Temple...")

--------------------------------------------------------------------
-- Load the shared cube mesh (used for all geometry)
--------------------------------------------------------------------
local cubeMesh = ffe.loadMesh("models/cube.glb")
if cubeMesh == 0 then
    ffe.log("[Level2] WARNING: cube.glb not found -- geometry will be missing")
    ffe.log("[Level2] Place cube.glb at assets/models/cube.glb")
end

--------------------------------------------------------------------
-- Load audio assets (optional -- game plays fine without audio)
--------------------------------------------------------------------
local sfxCollect = ffe.loadSound("audio/sfx_collect.wav")
local sfxHit     = ffe.loadSound("audio/sfx_hit.wav")
local musicHandle = ffe.loadMusic("audio/music_courtyard.ogg")

-- Start background music (lower volume for underground atmosphere)
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.25)
end

--------------------------------------------------------------------
-- Lighting: dark underground — near-total darkness with point lights
--------------------------------------------------------------------
-- Very dim directional light (faint overhead glow through cracks)
ffe.setLightDirection(0.0, -1.0, 0.0)
ffe.setLightColor(0.05, 0.04, 0.06)
ffe.setAmbientColor(0.05, 0.04, 0.06)

-- Enable shadows (lower resolution for underground — 512 is fine)
ffe.enableShadows(512)
ffe.setShadowBias(0.005)
ffe.setShadowArea(35, 35, 0.1, 60)

--------------------------------------------------------------------
-- Fog: dark reddish-brown, oppressive short range
--------------------------------------------------------------------
ffe.setFog(0.15, 0.08, 0.05, 5.0, 35.0)
ffe.setBackgroundColor(0.02, 0.01, 0.03)

--------------------------------------------------------------------
-- No skybox (underground)
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
            ffe.setMeshSpecular(ent, 0.15, 0.15, 0.15, 16)
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
-- FLOOR: Main stone platform sections
-- The temple is a cross-shaped layout with a central area and
-- 4 bridge arms extending over the lava pit.
--------------------------------------------------------------------
-- Central platform (12x0.5x12)
createStaticBox(0, -0.5, 0, 6, 0.5, 6, 0.25, 0.23, 0.22)

-- South platform (entrance area, 10x0.5x8)
createStaticBox(0, -0.5, -15, 5, 0.5, 4, 0.25, 0.23, 0.22)

-- North platform (exit area, 8x0.5x6)
createStaticBox(0, -0.5, 15, 4, 0.5, 3, 0.25, 0.23, 0.22)

-- East platform (crystal alcove, 6x0.5x6)
createStaticBox(13, -0.5, 0, 3, 0.5, 3, 0.25, 0.23, 0.22)

-- West platform (crystal alcove, 6x0.5x6)
createStaticBox(-13, -0.5, 0, 3, 0.5, 3, 0.25, 0.23, 0.22)

--------------------------------------------------------------------
-- LAVA PIT: Red-orange glowing flat plane below floor level
--------------------------------------------------------------------
createVisualBox(0, -2.0, 0, 18, 0.3, 18, 1.0, 0.35, 0.05, 1.0)

--------------------------------------------------------------------
-- NARROW BRIDGES: 3 stone bridges crossing the lava
--------------------------------------------------------------------
-- Bridge 1: South platform -> Central (along Z axis)
createStaticBox(0, -0.35, -9, 1, 0.15, 4, 0.3, 0.28, 0.26)

-- Bridge 2: East platform -> Central (along X axis)
createStaticBox(9, -0.35, 0, 3.5, 0.15, 1, 0.3, 0.28, 0.26)

-- Bridge 3: West platform -> Central (along X axis)
createStaticBox(-9, -0.35, 0, 3.5, 0.15, 1, 0.3, 0.28, 0.26)

-- Bridge 4: Central -> North (along Z axis)
createStaticBox(0, -0.35, 9, 1, 0.15, 3, 0.3, 0.28, 0.26)

--------------------------------------------------------------------
-- PERIMETER WALLS (dark stone, 6 units high)
--------------------------------------------------------------------
-- South wall: two segments with entrance gap
createStaticBox(-11, 3, -20, 7, 3, 0.5, 0.2, 0.18, 0.17)
createStaticBox( 11, 3, -20, 7, 3, 0.5, 0.2, 0.18, 0.17)

-- North wall: two segments with exit gap
createStaticBox(-11, 3, 20, 7, 3, 0.5, 0.2, 0.18, 0.17)
createStaticBox( 11, 3, 20, 7, 3, 0.5, 0.2, 0.18, 0.17)

-- East wall (solid)
createStaticBox(18, 3, 0, 0.5, 3, 20, 0.2, 0.18, 0.17)

-- West wall (solid)
createStaticBox(-18, 3, 0, 0.5, 3, 20, 0.2, 0.18, 0.17)

--------------------------------------------------------------------
-- PILLARS: Tall stone columns (1x6x1)
--------------------------------------------------------------------
-- Central platform corner pillars (4)
createStaticBox(-5, 3, -5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)
createStaticBox( 5, 3, -5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)
createStaticBox(-5, 3,  5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)
createStaticBox( 5, 3,  5, 0.5, 3, 0.5, 0.22, 0.2, 0.19)

-- Bridge-edge pillars (4, decorative only — no physics needed)
createVisualBox(-1.5, 2, -5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 1.5, 2, -5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)
createVisualBox(-1.5, 2,  5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 1.5, 2,  5.5, 0.4, 2, 0.4, 0.22, 0.2, 0.19, 1.0)

-- Wall-hugging pillars (4, decorative only — no physics needed)
createVisualBox(-17, 3, -10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 17, 3, -10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)
createVisualBox(-17, 3,  10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)
createVisualBox( 17, 3,  10, 0.6, 3, 0.6, 0.22, 0.2, 0.19, 1.0)

--------------------------------------------------------------------
-- CEILING: Dark flat plane above to enclose the space
--------------------------------------------------------------------
createVisualBox(0, 7, 0, 18, 0.3, 20, 0.08, 0.06, 0.07, 1.0)

--------------------------------------------------------------------
-- STALACTITES: Hanging boxes from ceiling for atmosphere
--------------------------------------------------------------------
createVisualBox(-8, 5.5, -7, 0.25, 1.2, 0.25, 0.18, 0.16, 0.15, 1.0)
createVisualBox( 10, 5.0, 5, 0.3, 1.5, 0.3, 0.16, 0.14, 0.13, 1.0)
createVisualBox( 3, 5.8, -12, 0.2, 0.9, 0.2, 0.17, 0.15, 0.14, 1.0)
createVisualBox(-6, 5.2, 11, 0.35, 1.3, 0.35, 0.15, 0.13, 0.12, 1.0)
createVisualBox( 14, 5.6, -3, 0.2, 1.0, 0.2, 0.16, 0.14, 0.13, 1.0)

--------------------------------------------------------------------
-- CRYSTAL PEDESTALS: 4 at cardinal positions
-- Each is a raised platform with a small bright "crystal" on top.
-- These are the puzzle elements (activation logic in Session 70).
--------------------------------------------------------------------
local crystalData = {
    { name = "blue",   px = 0,   pz = -13, cr = 0.3, cg = 0.5, cb = 1.0, lr = 0.3, lg = 0.5, lb = 1.0, radius = 12 },
    { name = "cyan",   px = 13,  pz = 0,   cr = 0.2, cg = 0.8, cb = 0.8, lr = 0.2, lg = 0.8, lb = 0.8, radius = 10 },
    { name = "purple", px = 0,   pz = 13,  cr = 0.7, cg = 0.3, cb = 0.9, lr = 0.7, lg = 0.3, lb = 0.9, radius = 10 },
    { name = "green",  px = -13, pz = 0,   cr = 0.3, cg = 0.9, cb = 0.4, lr = 0.3, lg = 0.9, lb = 0.4, radius = 8  },
}

local crystalEntities = {}

for i, cd in ipairs(crystalData) do
    -- Pedestal base (raised dark platform)
    createStaticBox(cd.px, 0.3, cd.pz, 1.0, 0.3, 1.0, 0.3, 0.28, 0.26)

    -- Crystal entity (small bright glowing box on top of pedestal)
    if cubeMesh ~= 0 then
        local cy = 1.0
        local crystal = ffe.createEntity3D(cubeMesh, cd.px, cy, cd.pz)
        if crystal ~= 0 then
            ffe.setTransform3D(crystal, cd.px, cy, cd.pz, 0, 45, 0, 0.3, 0.5, 0.3)
            ffe.setMeshColor(crystal, cd.cr, cd.cg, cd.cb, 1.0)
            ffe.setMeshSpecular(crystal, 1.0, 1.0, 1.0, 128)
            crystalEntities[#crystalEntities + 1] = {
                entity = crystal,
                pos    = { x = cd.px, y = cy, z = cd.pz },
                name   = cd.name,
            }
        end
    end

    -- Point light at crystal position (colored glow)
    -- Point light slots 0-3 correspond to crystals 1-4
    ffe.addPointLight(i - 1, cd.px, 1.5, cd.pz, cd.lr, cd.lg, cd.lb, cd.radius)
end

--------------------------------------------------------------------
-- CENTRAL ALTAR: Raised platform in the middle with the artifact
--------------------------------------------------------------------
-- Altar base (large stepped platform)
createStaticBox(0, 0.25, 0, 2.0, 0.25, 2.0, 0.28, 0.25, 0.23)
-- Altar top step
createStaticBox(0, 0.7, 0, 1.2, 0.2, 1.2, 0.32, 0.28, 0.25)

--------------------------------------------------------------------
-- EXIT PORTAL: Archway frame near north wall
-- (Inactive until puzzle solved in Session 70)
--------------------------------------------------------------------
-- Portal pillars (decorative — no physics)
createVisualBox(-3, 2.5, 19, 0.6, 2.5, 0.6, 0.35, 0.25, 0.4, 1.0)
createVisualBox( 3, 2.5, 19, 0.6, 2.5, 0.6, 0.35, 0.25, 0.4, 1.0)
-- Portal lintel
createVisualBox(0, 5.2, 19, 3.5, 0.4, 0.6, 0.35, 0.25, 0.4, 1.0)

-- Portal "glow" visual (dark for now — will light up when active)
createVisualBox(0, 2.5, 19.3, 2.4, 2.0, 0.1, 0.1, 0.05, 0.15, 1.0)

--------------------------------------------------------------------
-- ENTRANCE ARCHWAY (south side)
--------------------------------------------------------------------
createVisualBox(-3, 2.5, -19, 0.6, 2.5, 0.6, 0.3, 0.27, 0.25, 1.0)
createVisualBox( 3, 2.5, -19, 0.6, 2.5, 0.6, 0.3, 0.27, 0.25, 1.0)
createVisualBox(0, 5.2, -19, 3.5, 0.4, 0.6, 0.3, 0.27, 0.25, 1.0)

--------------------------------------------------------------------
-- MAIN ARTIFACT: Gold artifact on central altar (rotating)
--------------------------------------------------------------------
local mainArtifact = 0
local mainArtifactPos = { x = 0, y = 1.5, z = 0 }

if cubeMesh ~= 0 then
    mainArtifact = ffe.createEntity3D(cubeMesh,
        mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z)
    if mainArtifact ~= 0 then
        ffe.setTransform3D(mainArtifact,
            mainArtifactPos.x, mainArtifactPos.y, mainArtifactPos.z,
            0, 0, 0, 0.4, 0.4, 0.4)
        ffe.setMeshColor(mainArtifact, 1.0, 0.85, 0.1, 1.0)
        ffe.setMeshSpecular(mainArtifact, 1.0, 0.9, 0.4, 128)
        ffe.createPhysicsBody(mainArtifact, {
            shape       = "box",
            halfExtents = { 0.45, 0.45, 0.45 },
            motion      = "kinematic",
        })
    end
end

--------------------------------------------------------------------
-- OPTIONAL GEM PICKUPS: 2 on bridge platforms
--------------------------------------------------------------------
local gems = {}
local gemPositions = {
    { x = 9,  y = 0.3, z = 0 },   -- east bridge midpoint
    { x = -9, y = 0.3, z = 0 },   -- west bridge midpoint
}
local gemColors = {
    { 0.9, 0.2, 0.5 },  -- ruby
    { 0.1, 0.8, 1.0 },  -- aquamarine
}

for i, pos in ipairs(gemPositions) do
    if cubeMesh ~= 0 then
        local gem = ffe.createEntity3D(cubeMesh, pos.x, pos.y, pos.z)
        if gem ~= 0 then
            ffe.setTransform3D(gem, pos.x, pos.y, pos.z, 0, 0, 0, 0.25, 0.25, 0.25)
            local c = gemColors[i]
            ffe.setMeshColor(gem, c[1], c[2], c[3], 1.0)
            ffe.setMeshSpecular(gem, 0.8, 0.8, 0.8, 128)
            ffe.createPhysicsBody(gem, {
                shape       = "box",
                halfExtents = { 0.3, 0.3, 0.3 },
                motion      = "kinematic",
            })
            gems[gem] = { index = i, collected = false, pos = pos }
        end
    end
end

-- Track all rotating entities (artifact + gems + crystals)
local artifactEntities = {}
if mainArtifact ~= 0 then
    artifactEntities[#artifactEntities + 1] = {
        entity = mainArtifact,
        pos    = mainArtifactPos,
        scale  = 0.4,
        isMain = true,
    }
end
for entId, data in pairs(gems) do
    artifactEntities[#artifactEntities + 1] = {
        entity = entId,
        pos    = data.pos,
        scale  = 0.25,
        isMain = false,
    }
end

--------------------------------------------------------------------
-- Level state
--------------------------------------------------------------------
local artifactAngle    = 0
local crystalAngle     = 0
local gemsCollected    = 0
local totalGems        = 2
local mainCollected    = false
local levelComplete    = false
local levelCompleteTimer = 0

-- Set global artifact count for game.lua
totalArtifacts = 1  -- only the main artifact is needed

--------------------------------------------------------------------
-- GUARDIANS: 2 temple guardians (purple-tinted)
--------------------------------------------------------------------
AI.reset()

-- Guardian 1: patrols around central altar
local guardian1 = 0
if cubeMesh ~= 0 then
    local gx, gy, gz = 3, 0.8, 3
    guardian1 = ffe.createEntity3D(cubeMesh, gx, gy, gz)
    if guardian1 ~= 0 then
        ffe.setTransform3D(guardian1, gx, gy, gz, 0, 0, 0, 1.0, 1.3, 1.0)
        ffe.setMeshColor(guardian1, 0.55, 0.2, 0.6, 1.0)
        ffe.setMeshSpecular(guardian1, 0.4, 0.2, 0.5, 32)
        ffe.createPhysicsBody(guardian1, {
            shape       = "box",
            halfExtents = { 0.5, 0.65, 0.5 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian1, {
            { x =  4, y = 0.8, z =  4 },
            { x = -4, y = 0.8, z =  4 },
            { x = -4, y = 0.8, z = -4 },
            { x =  4, y = 0.8, z = -4 },
        }, 80)
    end
end

-- Guardian 2: patrols south bridge and entrance area
local guardian2 = 0
if cubeMesh ~= 0 then
    local gx, gy, gz = 0, 0.8, -11
    guardian2 = ffe.createEntity3D(cubeMesh, gx, gy, gz)
    if guardian2 ~= 0 then
        ffe.setTransform3D(guardian2, gx, gy, gz, 0, 0, 0, 1.0, 1.3, 1.0)
        ffe.setMeshColor(guardian2, 0.55, 0.2, 0.6, 1.0)
        ffe.setMeshSpecular(guardian2, 0.4, 0.2, 0.5, 32)
        ffe.createPhysicsBody(guardian2, {
            shape       = "box",
            halfExtents = { 0.5, 0.65, 0.5 },
            motion      = "dynamic",
            mass        = 3.0,
            restitution = 0.0,
            friction    = 0.9,
        })
        AI.create(guardian2, {
            { x =  0, y = 0.8, z = -13 },
            { x =  3, y = 0.8, z = -16 },
            { x = -3, y = 0.8, z = -16 },
            { x =  0, y = 0.8, z = -9  },
        }, 80)
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

    -- Check if it is the main artifact
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

        if sfxCollect then ffe.playSound(sfxCollect, 1.0) end
        ffe.cameraShake(0.5, 0.15)
        if collectArtifact then collectArtifact() end
        if HUD then HUD.showPrompt("Temple Artifact acquired!", 3.0) end
        ffe.log("[Level2] Main artifact collected!")
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
        ffe.log("[Level2] Gem collected: " .. tostring(gemsCollected)
            .. "/" .. tostring(totalGems))
        return
    end
end)

--------------------------------------------------------------------
-- Player spawn (south entrance bridge)
--------------------------------------------------------------------
Player.create(0, 1.5, -17, cubeMesh)
Camera.setPosition(0, 1.5, -17)
Camera.setYawPitch(0, 15)  -- lower pitch for enclosed underground space

if HUD then
    HUD.showPrompt("The Temple -- Retrieve the Altar Artifact", 4.0)
end

--------------------------------------------------------------------
-- Per-frame level tick: artifact/crystal animation, fall detection
--------------------------------------------------------------------
local TICK_RATE = 0.016  -- ~60 Hz

ffe.every(TICK_RATE, function()
    if getGameState and getGameState() ~= "PLAYING" then return end

    -- Rotate and bob artifacts / gems
    artifactAngle = artifactAngle + 90 * TICK_RATE
    if artifactAngle > 360 then artifactAngle = artifactAngle - 360 end

    for _, ad in ipairs(artifactEntities) do
        local bobY = ad.pos.y + math.sin(math.rad(artifactAngle * 2)) * 0.3
        ffe.setTransform3D(ad.entity, ad.pos.x, bobY, ad.pos.z,
            0, artifactAngle, 0,
            ad.scale, ad.scale, ad.scale)
    end

    -- Slowly rotate crystal entities for shimmer effect
    crystalAngle = crystalAngle + 45 * TICK_RATE
    if crystalAngle > 360 then crystalAngle = crystalAngle - 360 end

    for _, cd in ipairs(crystalEntities) do
        local bobY = cd.pos.y + math.sin(math.rad(crystalAngle * 1.5)) * 0.1
        ffe.setTransform3D(cd.entity, cd.pos.x, bobY, cd.pos.z,
            0, crystalAngle, 0,
            0.3, 0.5, 0.3)
    end

    -- Fall-off detection: respawn if player falls into lava
    if Player then
        local px, py, pz = Player.getPosition()
        if py < -3 then
            Player.cleanup()
            Player.create(0, 1.5, -17, cubeMesh)
            Camera.setPosition(0, 1.5, -17)
            if HUD then HUD.showPrompt("The lava claims another...", 2.0) end
        end
    end
end)

--------------------------------------------------------------------
-- Done
--------------------------------------------------------------------
ffe.log("[Level2] The Temple loaded successfully!")
ffe.log("[Level2] Objectives:")
ffe.log("[Level2]   1. Cross the bridges to reach the central altar")
ffe.log("[Level2]   2. Collect the Temple Artifact from the altar")
ffe.log("[Level2]   3. Defeat or avoid the 2 temple guardians")
ffe.log("[Level2]   4. Find optional gems on the bridge platforms")
ffe.log("[Level2] Controls: WASD move, SPACE jump, LMB attack, Right-click drag to orbit camera")
