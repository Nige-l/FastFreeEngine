-- lib/combat.lua -- Combat module for "Echoes of the Ancients"
--
-- Melee attack system: raycast in front of the player, check for enemies.
-- Attack cooldown timer to prevent spam.
--
-- Globals set: Combat (table with public API)

Combat = {}

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
local ATTACK_RANGE    = 3.0    -- meters
local ATTACK_COOLDOWN = 0.5    -- seconds between attacks
local ATTACK_DAMAGE   = 25     -- damage per hit

--------------------------------------------------------------------
-- State
--------------------------------------------------------------------
local cooldownTimer = 0
local swingTimer    = 0        -- visual attack swing feedback timer

--------------------------------------------------------------------
-- Combat.update(dt)
-- Called per frame to tick cooldown timers.
--------------------------------------------------------------------
function Combat.update(dt)
    if cooldownTimer > 0 then
        cooldownTimer = cooldownTimer - dt
    end
    if swingTimer > 0 then
        swingTimer = swingTimer - dt
    end
end

--------------------------------------------------------------------
-- Combat.isSwinging() -> boolean
-- Returns true while the attack swing animation is active.
-- Used by the HUD to show a crosshair flash.
--------------------------------------------------------------------
function Combat.isSwinging()
    return swingTimer > 0
end

--------------------------------------------------------------------
-- Combat.attack(playerPos, playerForward)
-- Performs a melee attack raycast.
-- playerPos:     { x, y, z } — player world position
-- playerForward: { x, y, z } — normalized forward direction
-- Returns: hitEntityId or nil
--------------------------------------------------------------------
function Combat.attack(playerPos, playerForward)
    if cooldownTimer > 0 then
        return nil
    end

    cooldownTimer = ATTACK_COOLDOWN
    swingTimer    = 0.15   -- brief visual swing indicator

    -- Cast a ray from the player position in the forward direction
    local ox = playerPos.x
    local oy = playerPos.y + 0.5  -- slightly above center (chest height)
    local oz = playerPos.z

    local dx = playerForward.x
    local dy = playerForward.y or 0
    local dz = playerForward.z

    local hitEntity, hx, hy, hz = ffe.castRay(ox, oy, oz, dx, dy, dz, ATTACK_RANGE)

    if hitEntity then
        -- Check if the hit entity is a guardian (AI-managed)
        if AI and AI.isEnemy(hitEntity) then
            AI.damageEnemy(hitEntity, ATTACK_DAMAGE)
            ffe.log("[Combat] Hit enemy " .. tostring(hitEntity) .. " for "
                .. tostring(ATTACK_DAMAGE) .. " damage")

            -- Camera shake on hit for feedback
            ffe.cameraShake(0.5, 0.15)
        else
            ffe.log("[Combat] Hit non-enemy entity " .. tostring(hitEntity))
        end

        return hitEntity
    end

    return nil
end

--------------------------------------------------------------------
-- Combat.canAttack() -> boolean
--------------------------------------------------------------------
function Combat.canAttack()
    return cooldownTimer <= 0
end

--------------------------------------------------------------------
-- Combat.getAttackDamage() -> number
--------------------------------------------------------------------
function Combat.getAttackDamage()
    return ATTACK_DAMAGE
end

ffe.log("[Combat] Module loaded")
