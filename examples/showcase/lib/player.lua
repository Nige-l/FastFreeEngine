-- lib/player.lua -- Player controller module for "Echoes of the Ancients"
--
-- Creates a physics-driven player entity (box shape) with:
--   WASD movement relative to camera direction
--   Xbox controller support (left stick move, A jump, X attack)
--   Jump via physics impulse with ground detection (raycast)
--   Health system (100 HP, death triggers game over)
--
-- Globals set: Player (table with public API)

Player = {}

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
local MOVE_SPEED       = 8.0    -- meters per second
local JUMP_IMPULSE     = 6.0    -- upward impulse magnitude
local GROUND_RAY_DIST  = 1.2    -- raycast distance for ground check
local MAX_HEALTH       = 100
local DAMAGE_COOLDOWN  = 0.5    -- seconds of invulnerability after taking damage
local PLAYER_HALF_Y    = 0.5    -- half-height of player box
local PLAYER_HALF_XZ   = 0.4    -- half-width/depth of player box
local X_ROT_CORRECTION = -90    -- CesiumMan is Y-up; rotate -90 on X to stand upright
local ANIM_IDLE        = 0      -- first animation clip (idle/default)
local ANIM_WALK        = -1     -- set dynamically based on clip count

--------------------------------------------------------------------
-- State
--------------------------------------------------------------------
local playerEntity    = 0
local health          = MAX_HEALTH
local isGrounded      = false
local damageCooldown  = 0
local meshHandle      = 0
local isMoving        = false   -- tracks whether player is walking (for animation)

-- Pre-allocated transform buffer (avoid GC pressure)
local tbuf = {}

--------------------------------------------------------------------
-- Player.create(x, y, z, cubeMeshHandle)
-- Creates the player entity at the given position.
-- cubeMeshHandle: handle from ffe.loadMesh (the cube.glb shared mesh)
--------------------------------------------------------------------
function Player.create(x, y, z, cubeMeshHandle)
    meshHandle = cubeMeshHandle or 0

    if meshHandle ~= 0 then
        playerEntity = ffe.createEntity3D(meshHandle, x, y, z)
    else
        -- Fallback: create entity without mesh (physics-only placeholder)
        playerEntity = ffe.createEntity3D(0, x, y, z)
    end

    if playerEntity == 0 then
        ffe.log("[Player] ERROR: failed to create player entity")
        return
    end

    -- Set transform: position, X rotation correction for Y-up glTF models
    -- CesiumMan is ~1.7m at scale 1.0; scale to 1.8 for a visible player character
    ffe.setTransform3D(playerEntity, x, y, z, X_ROT_CORRECTION, 0, 0, 1.8, 1.8, 1.8)

    -- Distinct player color: bright cyan
    ffe.setMeshColor(playerEntity, 0.1, 0.8, 0.9, 1.0)
    ffe.setMeshSpecular(playerEntity, 0.5, 0.8, 1.0, 64)

    -- Physics body: dynamic box
    ffe.createPhysicsBody(playerEntity, {
        shape       = "box",
        halfExtents = { PLAYER_HALF_XZ, PLAYER_HALF_Y, PLAYER_HALF_XZ },
        motion      = "dynamic",
        mass        = 1.0,
        restitution = 0.0,
        friction    = 0.8,
    })

    health         = MAX_HEALTH
    damageCooldown = 1.0   -- 1-second spawn invulnerability (Bug 3 fix)
    isGrounded     = false
    isMoving       = false

    -- Start skeletal animation if the mesh has animation clips
    local animCount = ffe.getAnimationCount3D(playerEntity)
    if animCount > 0 then
        ANIM_IDLE = 0
        -- If there are multiple clips, use clip 1 as walk (common convention)
        if animCount > 1 then
            ANIM_WALK = 1
        else
            ANIM_WALK = 0  -- only one clip; use it for everything
        end
        ffe.playAnimation3D(playerEntity, ANIM_IDLE, true)
        ffe.log("[Player] Started animation (clips=" .. tostring(animCount) .. ")")
    else
        ffe.log("[Player] No animation clips found on mesh")
    end

    ffe.log("[Player] Created at ("
        .. tostring(x) .. ", " .. tostring(y) .. ", " .. tostring(z) .. ")")
end

--------------------------------------------------------------------
-- Player.update(dt)
-- Call once per frame from the main update loop.
--------------------------------------------------------------------
function Player.update(dt)
    if playerEntity == 0 then return end

    -- Tick damage cooldown
    if damageCooldown > 0 then
        damageCooldown = damageCooldown - dt
    end

    -- Read current position
    ffe.fillTransform3D(playerEntity, tbuf)
    local px = tbuf.x or 0
    local py = tbuf.y or 0
    local pz = tbuf.z or 0

    -- Ground detection: raycast downward from player center
    local hitEnt = ffe.castRay(px, py, pz, 0, -1, 0, GROUND_RAY_DIST)
    isGrounded = (hitEnt ~= nil)

    -- Get camera yaw for movement direction
    local camYaw = Camera and Camera.getYaw() or 0
    local yawRad = math.rad(camYaw)

    -- Forward and right vectors in XZ plane (relative to camera)
    -- Camera orbit places camera at target + radius*(sin(yaw), _, cos(yaw)),
    -- so the camera looks TOWARD -sin(yaw), -cos(yaw). Player forward must
    -- match that look direction (i.e. the direction from camera to target).
    local fwdX = -math.sin(yawRad)
    local fwdZ = -math.cos(yawRad)
    local rgtX =  math.cos(yawRad)
    local rgtZ = -math.sin(yawRad)

    -- Accumulate input
    local moveX = 0
    local moveZ = 0

    -- Keyboard input (WASD)
    if ffe.isKeyHeld(ffe.KEY_W) then
        moveX = moveX + fwdX
        moveZ = moveZ + fwdZ
    end
    if ffe.isKeyHeld(ffe.KEY_S) then
        moveX = moveX - fwdX
        moveZ = moveZ - fwdZ
    end
    if ffe.isKeyHeld(ffe.KEY_D) then
        moveX = moveX + rgtX
        moveZ = moveZ + rgtZ
    end
    if ffe.isKeyHeld(ffe.KEY_A) then
        moveX = moveX - rgtX
        moveZ = moveZ - rgtZ
    end

    -- Gamepad input: left stick for movement (with dead-zone)
    if ffe.isGamepadConnected(0) then
        local lx = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_LEFT_X)
        local ly = ffe.getGamepadAxis(0, ffe.GAMEPAD_AXIS_LEFT_Y)

        -- Dead-zone: ignore stick values below 0.15 to avoid drift
        local DEAD_ZONE = 0.15
        if math.abs(lx) < DEAD_ZONE then lx = 0 end
        if math.abs(ly) < DEAD_ZONE then ly = 0 end

        -- Left stick: X = strafe, Y = forward/back (inverted: -Y is forward)
        moveX = moveX + (fwdX * (-ly) + rgtX * lx)
        moveZ = moveZ + (fwdZ * (-ly) + rgtZ * lx)
    end

    -- Normalize direction then scale by speed.
    -- When moveMag > 1.0 (e.g. diagonal keyboard), clamp to unit length.
    -- When moveMag <= 1.0 (e.g. partial gamepad stick), preserve magnitude
    -- as a fraction of full speed, but still normalize the direction first.
    local moveMag = math.sqrt(moveX * moveX + moveZ * moveZ)
    if moveMag > 0.001 then
        local invMag = 1.0 / moveMag
        local dirX = moveX * invMag
        local dirZ = moveZ * invMag
        -- Clamp magnitude to [0, 1] so diagonal keyboard input doesn't exceed speed
        local mag = moveMag
        if mag > 1.0 then mag = 1.0 end
        moveX = dirX * mag * MOVE_SPEED
        moveZ = dirZ * mag * MOVE_SPEED
    end

    -- Apply movement by setting horizontal velocity (preserve vertical for gravity)
    local vx, vy, vz = ffe.getLinearVelocity(playerEntity)
    vy = vy or 0
    ffe.setLinearVelocity(playerEntity, moveX, vy, moveZ)

    -- Jump: Space key or Gamepad A button
    local wantJump = ffe.isKeyPressed(ffe.KEY_SPACE)
    if ffe.isGamepadConnected(0) then
        wantJump = wantJump or ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_A)
    end

    if wantJump and isGrounded then
        ffe.applyImpulse(playerEntity, 0, JUMP_IMPULSE, 0)
        ffe.log("[Player] Jump!")
    end

    -- Attack: left mouse button, F key (fallback when cursor not captured), or Gamepad X button
    -- LMB uses the latched isMousePressed API (correct for Session 101 engine fix).
    -- F key provides a reliable fallback until mouse capture is confirmed working.
    local wantAttack = ffe.isMousePressed(ffe.MOUSE_LEFT)
                    or ffe.isKeyPressed(ffe.KEY_F)
                    or ffe.isKeyHeld(ffe.KEY_F)
    if ffe.isGamepadConnected(0) then
        wantAttack = wantAttack or ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_X)
    end
    if wantAttack and Combat then
        -- Forward direction for attack (matches camera look direction: -sin, -cos)
        local atkFwdX = -math.sin(yawRad)
        local atkFwdZ = -math.cos(yawRad)
        Combat.attack(
            { x = px, y = py, z = pz },
            { x = atkFwdX, y = 0, z = atkFwdZ }
        )
    end

    -- Rotate player to face movement direction (combine yaw with X correction)
    if moveMag > 0.1 then
        local facingDeg = math.deg(math.atan2(moveX, moveZ))
        ffe.setTransform3D(playerEntity,
            px, py, pz,
            X_ROT_CORRECTION, facingDeg, 0,
            1.8, 1.8, 1.8)
    end

    -- Switch animation between walk and idle based on movement
    local wasMoving = isMoving
    isMoving = (moveMag > 0.1)
    if isMoving ~= wasMoving then
        local animCount = ffe.getAnimationCount3D(playerEntity)
        if animCount > 0 then
            if isMoving and ANIM_WALK >= 0 then
                ffe.playAnimation3D(playerEntity, ANIM_WALK, true)
                ffe.setAnimationSpeed3D(playerEntity, 1.5)  -- slightly faster walk
            elseif not isMoving then
                ffe.playAnimation3D(playerEntity, ANIM_IDLE, true)
                ffe.setAnimationSpeed3D(playerEntity, 1.0)
            end
        end
    end
end

--------------------------------------------------------------------
-- Player.getPosition() -> x, y, z
--------------------------------------------------------------------
function Player.getPosition()
    if playerEntity == 0 then return 0, 0, 0 end
    ffe.fillTransform3D(playerEntity, tbuf)
    return tbuf.x or 0, tbuf.y or 0, tbuf.z or 0
end

--------------------------------------------------------------------
-- Player.getEntity() -> entityId
--------------------------------------------------------------------
function Player.getEntity()
    return playerEntity
end

--------------------------------------------------------------------
-- Player.takeDamage(amount)
--------------------------------------------------------------------
function Player.takeDamage(amount)
    if damageCooldown > 0 then return end
    if health <= 0 then return end

    health = health - (amount or 10)
    damageCooldown = DAMAGE_COOLDOWN

    -- Flash red on hit
    if playerEntity ~= 0 then
        ffe.setMeshColor(playerEntity, 1, 0.2, 0.2, 1.0)
        ffe.after(0.15, function()
            if playerEntity ~= 0 then
                ffe.setMeshColor(playerEntity, 0.1, 0.8, 0.9, 1.0)
            end
        end)
    end

    ffe.log("[Player] Took " .. tostring(amount) .. " damage, HP: " .. tostring(health))

    if health <= 0 then
        health = 0
        ffe.log("[Player] Dead!")
        if triggerGameOver then triggerGameOver() end
    end
end

--------------------------------------------------------------------
-- Player.getHealth() -> number
--------------------------------------------------------------------
function Player.getHealth()
    return health
end

--------------------------------------------------------------------
-- Player.getMaxHealth() -> number
--------------------------------------------------------------------
function Player.getMaxHealth()
    return MAX_HEALTH
end

--------------------------------------------------------------------
-- Player.isAlive() -> boolean
--------------------------------------------------------------------
function Player.isAlive()
    return health > 0
end

--------------------------------------------------------------------
-- Player.isInteracting() -> boolean
-- Returns true on the frame the player presses E (keyboard) or
-- Y (gamepad). Level scripts poll this for crystal activation, etc.
--------------------------------------------------------------------
function Player.isInteracting()
    local interact = ffe.isKeyPressed(ffe.KEY_E)
    if ffe.isGamepadConnected(0) then
        interact = interact or ffe.isGamepadButtonPressed(0, ffe.GAMEPAD_Y)
    end
    return interact
end

--------------------------------------------------------------------
-- Player.cleanup()
-- Destroy physics body and entity.
--------------------------------------------------------------------
function Player.cleanup()
    if playerEntity ~= 0 then
        ffe.destroyPhysicsBody(playerEntity)
        ffe.destroyEntity(playerEntity)
        playerEntity = 0
    end
end

ffe.log("[Player] Module loaded")
