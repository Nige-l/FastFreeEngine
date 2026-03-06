-- game.lua -- player movement logic for the FFE lua_demo example
--
-- This script is loaded once at startup via scriptEngine.doFile().
-- It defines a global update() function that the C++ host calls each tick.
--
-- Coordinate system: centered origin, -640..640 (x), -360..360 (y).
-- The player entity is created in C++ and its ID is passed to update().
--
-- NOTE: ESC handling is not possible from Lua in the current API.
-- The ShutdownSignal lives in the ECS registry context, which has no Lua
-- binding. ESC is handled in the C++ host instead.
-- See the usage report for the recommended API addition.

local SPEED = 150.0

-- Half-extents of the visible area (1280x720 window, centered origin)
local HALF_W = 640.0
local HALF_H = 360.0

-- Sprite half-size for clamping (48px sprite / 2)
local HALF_SPRITE = 24.0

ffe.log("lua_demo: Lua script loaded successfully!")
ffe.log("lua_demo: Use WASD to move the player. Press ESC to quit (handled in C++).")

-- update(entityId, dt) -- called by the C++ LuaScriptSystem every tick.
-- entityId: raw u32 entity ID (Lua integer)
-- dt:       delta time in seconds (fixed at 1/60 for the default tick rate)
function update(entityId, dt)
    local t = ffe.getTransform(entityId)
    if t == nil then return end

    local dx = 0.0
    local dy = 0.0

    if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - SPEED * dt end
    if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + SPEED * dt end

    -- Clamp to visible screen bounds (centered coordinate system)
    local xLimit = HALF_W - HALF_SPRITE
    local yLimit = HALF_H - HALF_SPRITE

    local nx = t.x + dx
    local ny = t.y + dy

    if nx < -xLimit then nx = -xLimit end
    if nx >  xLimit then nx =  xLimit end
    if ny < -yLimit then ny = -yLimit end
    if ny >  yLimit then ny =  yLimit end

    ffe.setTransform(entityId, nx, ny, t.rotation, t.scaleX, t.scaleY)
end
