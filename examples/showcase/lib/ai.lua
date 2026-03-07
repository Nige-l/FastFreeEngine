-- lib/ai.lua -- Guardian AI module for "Echoes of the Ancients"
--
-- State machine per enemy: IDLE -> PATROL -> CHASE -> ATTACK -> DEAD
-- Patrol: move between waypoints.
-- Chase: move toward player when within detection range.
-- Attack: melee at close range.
--
-- Globals set: AI (table with public API)

AI = {}

--------------------------------------------------------------------
-- Constants
--------------------------------------------------------------------
local STATE_IDLE    = "IDLE"
local STATE_PATROL  = "PATROL"
local STATE_CHASE   = "CHASE"
local STATE_ATTACK  = "ATTACK"
local STATE_DEAD    = "DEAD"

local DETECTION_RANGE   = 12.0   -- meters to start chasing
local ATTACK_RANGE      = 2.5    -- meters to start attacking
local PATROL_SPEED      = 2.0    -- meters per second
local CHASE_SPEED       = 4.0    -- meters per second
local ATTACK_COOLDOWN   = 1.5    -- seconds between attacks
local ATTACK_DAMAGE     = 15     -- damage to player per hit
local WAYPOINT_THRESHOLD = 1.0   -- meters to consider waypoint reached
local DEFAULT_HEALTH    = 100

--------------------------------------------------------------------
-- Enemy registry: maps entityId -> enemy data table
--------------------------------------------------------------------
local enemies = {}

-- Grace period: enemies only patrol (no detect/chase/attack) for this many seconds
-- after AI.reset() or AI.init(). Prevents instant damage on level load (Bug 3).
local graceTimer = 3.0

-- Pre-allocated transform buffer
local tbuf = {}

--------------------------------------------------------------------
-- Internal: distance between two 3D points
--------------------------------------------------------------------
local function distance3D(ax, ay, az, bx, by, bz)
    local dx = ax - bx
    local dy = ay - by
    local dz = az - bz
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

--------------------------------------------------------------------
-- Internal: move entity toward a target position at given speed
--------------------------------------------------------------------
local function moveToward(entityId, tx, ty, tz, speed, dt)
    ffe.fillTransform3D(entityId, tbuf)
    local ex = tbuf.x or 0
    local ey = tbuf.y or 0
    local ez = tbuf.z or 0

    local dx = tx - ex
    local dz = tz - ez
    local dist = math.sqrt(dx * dx + dz * dz)

    if dist < 0.01 then return end

    local nx = dx / dist
    local nz = dz / dist
    local step = speed * dt
    if step > dist then step = dist end

    -- Use velocity for physics-driven movement (preserve Y for gravity)
    local vx, vy, vz = ffe.getLinearVelocity(entityId)
    vy = vy or 0
    ffe.setLinearVelocity(entityId, nx * speed, vy, nz * speed)

    -- Face movement direction, using the entity's registered scale
    local enemy = enemies[entityId]
    local facingDeg = math.deg(math.atan2(nx, nz))
    local scX = enemy and enemy.scaleX or 1.0
    local scY = enemy and enemy.scaleY or 1.0
    local scZ = enemy and enemy.scaleZ or 1.0
    ffe.setTransform3D(entityId, ex, ey, ez, 0, facingDeg, 0, scX, scY, scZ)
end

--------------------------------------------------------------------
-- AI.create(entityId, waypoints, health)
-- Register an entity as an AI-controlled enemy.
-- waypoints: array of { x, y, z } positions for patrol
-- health: optional starting health (default 100)
--------------------------------------------------------------------
function AI.create(entityId, waypoints, health)
    -- Capture the entity's current scale at registration time so that
    -- moveToward() preserves it instead of hardcoding a single value.
    -- This fixes the bug where cube-based guardians (Level 2, scale ~1.0)
    -- were overwritten to fox-model scale (0.03) every frame.
    ffe.fillTransform3D(entityId, tbuf)
    local sx = tbuf.sx or 1.0
    local sy = tbuf.sy or 1.0
    local sz = tbuf.sz or 1.0

    -- Also capture the entity's current mesh color so we can restore it
    -- after a damage flash, instead of hardcoding a single color.
    -- (The Lua API does not have getMeshColor, so we store it at registration.)
    enemies[entityId] = {
        entity        = entityId,
        state         = STATE_PATROL,
        health        = health or DEFAULT_HEALTH,
        maxHealth     = health or DEFAULT_HEALTH,
        waypoints     = waypoints or {},
        waypointIndex = 1,
        attackTimer   = 0,
        scaleX        = sx,
        scaleY        = sy,
        scaleZ        = sz,
        colorR        = 0.8,
        colorG        = 0.2,
        colorB        = 0.15,
        colorA        = 1.0,
    }
    ffe.log("[AI] Enemy registered: " .. tostring(entityId)
        .. " with " .. tostring(#(waypoints or {})) .. " waypoints"
        .. " scale=(" .. tostring(sx) .. ", " .. tostring(sy) .. ", " .. tostring(sz) .. ")")
end

--------------------------------------------------------------------
-- AI.updateAll(dt)
-- Update all registered enemies. Call once per frame.
--------------------------------------------------------------------
function AI.updateAll(dt)
    if not Player then return end

    -- Tick grace timer: during grace period, enemies only patrol (no chase/attack)
    if graceTimer > 0 then
        graceTimer = graceTimer - dt
    end

    local px, py, pz = Player.getPosition()

    for entId, enemy in pairs(enemies) do
        if enemy.state ~= STATE_DEAD then
            if graceTimer > 0 then
                -- Grace period: force patrol only (skip detection/chase/attack)
                AI.updatePatrolOnly(enemy, dt)
            else
                AI.updateOne(enemy, px, py, pz, dt)
            end
        end
    end
end

--------------------------------------------------------------------
-- AI.updatePatrolOnly(enemy, dt)
-- Patrol-only update during grace period: no player detection.
--------------------------------------------------------------------
function AI.updatePatrolOnly(enemy, dt)
    local entId = enemy.entity

    -- Force back to PATROL if chasing/attacking (from a previous life)
    if enemy.state == STATE_CHASE or enemy.state == STATE_ATTACK then
        enemy.state = STATE_PATROL
        ffe.setLinearVelocity(entId, 0, 0, 0)
    end

    if enemy.state == STATE_PATROL and #enemy.waypoints > 0 then
        ffe.fillTransform3D(entId, tbuf)
        local ex = tbuf.x or 0
        local ey = tbuf.y or 0
        local ez = tbuf.z or 0

        local wp = enemy.waypoints[enemy.waypointIndex]
        moveToward(entId, wp.x, wp.y, wp.z, PATROL_SPEED, dt)

        local dx = ex - wp.x
        local dz = ez - wp.z
        local distToWP = math.sqrt(dx * dx + dz * dz)
        if distToWP < WAYPOINT_THRESHOLD then
            enemy.waypointIndex = enemy.waypointIndex + 1
            if enemy.waypointIndex > #enemy.waypoints then
                enemy.waypointIndex = 1
            end
        end
    end
end

--------------------------------------------------------------------
-- AI.updateOne(enemy, playerX, playerY, playerZ, dt)
-- Update a single enemy's state machine.
--------------------------------------------------------------------
function AI.updateOne(enemy, playerX, playerY, playerZ, dt)
    local entId = enemy.entity

    -- Read enemy position
    ffe.fillTransform3D(entId, tbuf)
    local ex = tbuf.x or 0
    local ey = tbuf.y or 0
    local ez = tbuf.z or 0

    local distToPlayer = distance3D(ex, ey, ez, playerX, playerY, playerZ)

    -- Tick attack cooldown
    if enemy.attackTimer > 0 then
        enemy.attackTimer = enemy.attackTimer - dt
    end

    -- State transitions
    if enemy.state == STATE_IDLE then
        -- Check for player proximity
        if distToPlayer < DETECTION_RANGE then
            enemy.state = STATE_CHASE
        elseif #enemy.waypoints > 0 then
            enemy.state = STATE_PATROL
        end

    elseif enemy.state == STATE_PATROL then
        -- Move toward current waypoint
        if #enemy.waypoints > 0 then
            local wp = enemy.waypoints[enemy.waypointIndex]
            moveToward(entId, wp.x, wp.y, wp.z, PATROL_SPEED, dt)

            -- Check if reached waypoint
            local distToWP = distance3D(ex, ey, ez, wp.x, wp.y, wp.z)
            if distToWP < WAYPOINT_THRESHOLD then
                enemy.waypointIndex = enemy.waypointIndex + 1
                if enemy.waypointIndex > #enemy.waypoints then
                    enemy.waypointIndex = 1
                end
            end
        end

        -- Detect player
        if distToPlayer < DETECTION_RANGE then
            enemy.state = STATE_CHASE
        end

    elseif enemy.state == STATE_CHASE then
        -- Move toward player
        moveToward(entId, playerX, playerY, playerZ, CHASE_SPEED, dt)

        -- Transition to attack if close enough
        if distToPlayer < ATTACK_RANGE then
            enemy.state = STATE_ATTACK
        end

        -- Lose interest if player is too far
        if distToPlayer > DETECTION_RANGE * 1.5 then
            enemy.state = STATE_PATROL
            -- Stop moving
            ffe.setLinearVelocity(entId, 0, 0, 0)
        end

    elseif enemy.state == STATE_ATTACK then
        -- Stop moving
        local vx, vy, vz = ffe.getLinearVelocity(entId)
        ffe.setLinearVelocity(entId, 0, vy or 0, 0)

        -- Attack if cooldown allows
        if enemy.attackTimer <= 0 and distToPlayer < ATTACK_RANGE then
            Player.takeDamage(ATTACK_DAMAGE)
            enemy.attackTimer = ATTACK_COOLDOWN
            ffe.log("[AI] Enemy " .. tostring(entId) .. " attacks player!")

            -- Camera shake for feedback
            ffe.cameraShake(0.8, 0.2)
        end

        -- Chase again if player runs away
        if distToPlayer > ATTACK_RANGE * 1.5 then
            enemy.state = STATE_CHASE
        end
    end
end

--------------------------------------------------------------------
-- AI.setEnemyColor(entityId, r, g, b, a)
-- Store the enemy's base color for damage flash restoration.
-- Call after AI.create if the enemy is not the default red.
--------------------------------------------------------------------
function AI.setEnemyColor(entityId, r, g, b, a)
    local enemy = enemies[entityId]
    if not enemy then return end
    enemy.colorR = r or 0.8
    enemy.colorG = g or 0.2
    enemy.colorB = b or 0.15
    enemy.colorA = a or 1.0
end

--------------------------------------------------------------------
-- AI.isEnemy(entityId) -> boolean
--------------------------------------------------------------------
function AI.isEnemy(entityId)
    return enemies[entityId] ~= nil and enemies[entityId].state ~= STATE_DEAD
end

--------------------------------------------------------------------
-- AI.damageEnemy(entityId, amount)
--------------------------------------------------------------------
function AI.damageEnemy(entityId, amount)
    local enemy = enemies[entityId]
    if not enemy then return end
    if enemy.state == STATE_DEAD then return end

    enemy.health = enemy.health - (amount or 10)

    -- Flash white on hit, then restore to the enemy's registered base color
    ffe.setMeshColor(entityId, 1, 1, 1, 1.0)
    ffe.after(0.1, function()
        if enemies[entityId] and enemies[entityId].state ~= STATE_DEAD then
            local e = enemies[entityId]
            ffe.setMeshColor(entityId, e.colorR, e.colorG, e.colorB, e.colorA)
        end
    end)

    ffe.log("[AI] Enemy " .. tostring(entityId)
        .. " took " .. tostring(amount) .. " damage, HP: " .. tostring(enemy.health))

    if enemy.health <= 0 then
        enemy.state = STATE_DEAD
        -- Stop the body and make it fall over (apply a small sideways impulse)
        ffe.setLinearVelocity(entityId, 0, 0, 0)
        ffe.applyImpulse(entityId, 0, 2, 0)

        -- Grey out the dead enemy
        ffe.setMeshColor(entityId, 0.3, 0.3, 0.3, 0.6)

        ffe.log("[AI] Enemy " .. tostring(entityId) .. " defeated!")
    end
end

--------------------------------------------------------------------
-- AI.getEnemyCount() -> alive, total
--------------------------------------------------------------------
function AI.getEnemyCount()
    local alive = 0
    local total = 0
    for _, enemy in pairs(enemies) do
        total = total + 1
        if enemy.state ~= STATE_DEAD then
            alive = alive + 1
        end
    end
    return alive, total
end

--------------------------------------------------------------------
-- AI.reset()
-- Clear all registered enemies.
--------------------------------------------------------------------
function AI.reset()
    enemies = {}
    graceTimer = 3.0  -- Reset grace period on level load
end

ffe.log("[AI] Module loaded")
