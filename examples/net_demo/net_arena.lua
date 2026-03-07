-- net_arena.lua -- "Net Arena" multiplayer demo for FFE
--
-- A simple 2D networked arena: colored squares moving in a shared space.
-- Demonstrates the full networking stack: server hosting, client connection,
-- game messaging, input replication, and snapshot interpolation.
--
-- Controls:
--   S          host a server on port 7777
--   C          connect to localhost:7777
--   WASD       move your player
--   ESC        quit
--
-- Coordinate system: centered origin, x: -640..640, y: -360..360.
-- Window: 1280x720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local PORT         = 7777
local PLAYER_SPEED = 250.0
local PLAYER_SIZE  = 40
local HALF_W       = 640
local HALF_H       = 360
local ARENA_MARGIN = 30

-- Message types for game messages
local MSG_PLAYER_SPAWN   = 1   -- server -> client: spawn a player entity
local MSG_PLAYER_DESPAWN = 2   -- server -> client: remove a player entity

-- Input bit flags (must match what sendInput expects)
local INPUT_UP    = 1
local INPUT_DOWN  = 2
local INPUT_LEFT  = 4
local INPUT_RIGHT = 8

-- Player colors: each client gets a color based on their ID
local COLORS = {
    {0.3, 0.7, 1.0},   -- blue
    {1.0, 0.4, 0.3},   -- red
    {0.3, 1.0, 0.5},   -- green
    {1.0, 0.9, 0.2},   -- yellow
    {0.9, 0.4, 0.9},   -- purple
    {1.0, 0.6, 0.2},   -- orange
    {0.2, 0.9, 0.9},   -- cyan
    {1.0, 0.7, 0.8},   -- pink
}

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local mode            = "menu"      -- "menu", "server", "client"
local transformBuf    = {}
local gameTime        = 0

-- Server state
local players         = {}          -- clientId -> { entity=entityId, color={r,g,b} }
local serverEntity    = nil         -- server's own player entity

-- Client state
local localEntity     = nil         -- our predicted entity
local remoteEntities  = {}          -- remote player entities (from snapshots)
local connected       = false
local myClientId      = -1

-- ---------------------------------------------------------------------------
-- Helper: pick a color for a client ID
-- ---------------------------------------------------------------------------
local function colorForId(id)
    local idx = (id % #COLORS) + 1
    return COLORS[idx]
end

-- ---------------------------------------------------------------------------
-- Helper: create a player entity with given color
-- ---------------------------------------------------------------------------
local function createPlayerEntity(x, y, r, g, b)
    local id = ffe.createEntity()
    if not id then return nil end
    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addPreviousTransform(id)
    -- Use a solid-colored sprite (texture handle 1 = white/default)
    ffe.addSprite(id, 1, PLAYER_SIZE, PLAYER_SIZE, r, g, b, 1.0, 1)
    return id
end

-- ---------------------------------------------------------------------------
-- Helper: clamp position to arena bounds
-- ---------------------------------------------------------------------------
local function clampPos(x, y)
    local limit_x = HALF_W - ARENA_MARGIN
    local limit_y = HALF_H - ARENA_MARGIN
    x = math.max(-limit_x, math.min(limit_x, x))
    y = math.max(-limit_y, math.min(limit_y, y))
    return x, y
end

-- ---------------------------------------------------------------------------
-- Server: handle client connections
-- ---------------------------------------------------------------------------
local function serverOnClientConnected(clientId)
    ffe.log("Server: client " .. clientId .. " connected")

    local color = colorForId(clientId)
    local spawnX = (math.random() * 2 - 1) * (HALF_W - 100)
    local spawnY = (math.random() * 2 - 1) * (HALF_H - 100)

    local entity = createPlayerEntity(spawnX, spawnY, color[1], color[2], color[3])
    if entity then
        players[clientId] = {
            entity = entity,
            color  = color,
        }
    end
end

local function serverOnClientDisconnected(clientId)
    ffe.log("Server: client " .. clientId .. " disconnected")

    local info = players[clientId]
    if info and info.entity then
        ffe.destroyEntity(info.entity)
    end
    players[clientId] = nil
end

-- ---------------------------------------------------------------------------
-- Server: handle input from clients
-- ---------------------------------------------------------------------------
local function serverOnInput(clientId, inputTable)
    local info = players[clientId]
    if not info or not info.entity then return end

    local bits = inputTable.bits or 0
    local dt   = inputTable.dt or (1.0 / 60.0)

    local dx, dy = 0, 0
    if (bits % (INPUT_UP * 2)) >= INPUT_UP       then dy = dy + PLAYER_SPEED * dt end
    if (bits % (INPUT_DOWN * 2)) >= INPUT_DOWN    then dy = dy - PLAYER_SPEED * dt end
    if (bits % (INPUT_LEFT * 2)) >= INPUT_LEFT    then dx = dx - PLAYER_SPEED * dt end
    if (bits % (INPUT_RIGHT * 2)) >= INPUT_RIGHT  then dx = dx + PLAYER_SPEED * dt end

    if dx ~= 0 or dy ~= 0 then
        if ffe.fillTransform(info.entity, transformBuf) then
            local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
            ffe.setTransform(info.entity, nx, ny,
                             transformBuf.rotation,
                             transformBuf.scaleX, transformBuf.scaleY)
        end
    end
end

-- ---------------------------------------------------------------------------
-- Server: start hosting
-- ---------------------------------------------------------------------------
local function startServer()
    math.randomseed(os.time())

    local ok = ffe.startServer(PORT)
    if not ok then
        ffe.log("ERROR: Failed to start server on port " .. PORT)
        return false
    end

    ffe.log("Server started on port " .. PORT)
    mode = "server"

    ffe.setNetworkTickRate(20)

    -- Register callbacks
    ffe.onClientConnected(serverOnClientConnected)
    ffe.onClientDisconnected(serverOnClientDisconnected)
    ffe.onServerInput(serverOnInput)

    -- Create the server's own "player" for visual reference (stationary host marker)
    local hostColor = {0.9, 0.9, 0.9}
    serverEntity = createPlayerEntity(0, 0, hostColor[1], hostColor[2], hostColor[3])

    ffe.setBackgroundColor(0.05, 0.08, 0.12)
    return true
end

-- ---------------------------------------------------------------------------
-- Client: start connecting
-- ---------------------------------------------------------------------------
local function startClient()
    ffe.onConnected(function()
        connected  = true
        myClientId = ffe.getClientId()
        ffe.log("Client: connected with ID " .. myClientId)

        -- Create our local predicted entity
        local color = colorForId(myClientId)
        localEntity = createPlayerEntity(0, 0, color[1], color[2], color[3])

        if localEntity then
            ffe.setLocalPlayer(localEntity)
        end
    end)

    ffe.onDisconnected(function()
        ffe.log("Client: disconnected")
        connected  = false
        myClientId = -1
        if localEntity then
            ffe.destroyEntity(localEntity)
            localEntity = nil
        end
    end)

    local ok = ffe.connectToServer("127.0.0.1", PORT)
    if not ok then
        ffe.log("ERROR: Failed to connect to 127.0.0.1:" .. PORT)
        return false
    end

    ffe.log("Client: connecting to 127.0.0.1:" .. PORT .. " ...")
    mode = "client"

    ffe.setBackgroundColor(0.05, 0.08, 0.12)
    return true
end

-- ---------------------------------------------------------------------------
-- Init
-- ---------------------------------------------------------------------------
ffe.setBackgroundColor(0.06, 0.06, 0.10)
ffe.log("Net Arena demo loaded. Press S to host, C to connect.")

-- ---------------------------------------------------------------------------
-- update(entityId, dt) -- called per tick by the C++ host
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    gameTime = gameTime + dt

    -- ESC to quit from any state
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        if mode == "server" then
            ffe.stopServer()
        elseif mode == "client" then
            ffe.disconnect()
        end
        ffe.requestShutdown()
        return
    end

    -- -----------------------------------------------------------------------
    -- Menu mode: waiting for user to choose server or client
    -- -----------------------------------------------------------------------
    if mode == "menu" then
        local sw = ffe.getScreenWidth()
        local sh = ffe.getScreenHeight()

        ffe.drawText("NET ARENA", sw / 2 - 144, 120, 4, 0.3, 0.7, 1.0, 1)
        ffe.drawText("A FastFreeEngine Multiplayer Demo", sw / 2 - 264, 190, 2, 0.5, 0.5, 0.6, 0.8)

        ffe.drawText("Press S to HOST a server", sw / 2 - 192, 300, 2, 0.3, 1.0, 0.5, 1)
        ffe.drawText("Press C to CONNECT as client", sw / 2 - 224, 340, 2, 0.3, 0.7, 1.0, 1)

        local alpha = 0.4 + 0.3 * math.sin(gameTime * 2)
        ffe.drawText("(Run two instances for multiplayer)", sw / 2 - 280, 420, 2, 0.5, 0.5, 0.5, alpha)

        ffe.drawText("ESC to quit", sw / 2 - 88, 520, 2, 0.4, 0.4, 0.4, 0.6)

        if ffe.isKeyPressed(ffe.KEY_S) then
            startServer()
        elseif ffe.isKeyPressed(ffe.KEY_C) then
            startClient()
        end

        return
    end

    -- -----------------------------------------------------------------------
    -- Server mode
    -- -----------------------------------------------------------------------
    if mode == "server" then
        local sw = ffe.getScreenWidth()

        -- Server host can move its own marker with WASD
        if serverEntity then
            if ffe.fillTransform(serverEntity, transformBuf) then
                local dx, dy = 0, 0
                if ffe.isKeyHeld(ffe.KEY_W) then dy = dy + PLAYER_SPEED * dt end
                if ffe.isKeyHeld(ffe.KEY_S) then dy = dy - PLAYER_SPEED * dt end
                if ffe.isKeyHeld(ffe.KEY_A) then dx = dx - PLAYER_SPEED * dt end
                if ffe.isKeyHeld(ffe.KEY_D) then dx = dx + PLAYER_SPEED * dt end

                if dx ~= 0 or dy ~= 0 then
                    local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
                    ffe.setTransform(serverEntity, nx, ny,
                                     transformBuf.rotation,
                                     transformBuf.scaleX, transformBuf.scaleY)
                end
            end
        end

        -- Count connected players
        local playerCount = 0
        for _ in pairs(players) do playerCount = playerCount + 1 end

        -- Draw arena border
        ffe.drawRect(0, 0, sw, 4, 0.2, 0.3, 0.4, 0.6)
        ffe.drawRect(0, 716, sw, 4, 0.2, 0.3, 0.4, 0.6)
        ffe.drawRect(0, 0, 4, 720, 0.2, 0.3, 0.4, 0.6)
        ffe.drawRect(1276, 0, 4, 720, 0.2, 0.3, 0.4, 0.6)

        -- HUD
        ffe.drawRect(8, 8, 360, 60, 0, 0, 0, 0.6)
        ffe.drawText("SERVER - Port " .. PORT, 16, 14, 2, 0.3, 1.0, 0.5, 1)
        ffe.drawText("Players: " .. playerCount, 16, 38, 2, 0.8, 0.8, 0.8, 1)

        -- Debug info
        local tick = ffe.getNetworkTick()
        ffe.drawRect(8, 680, 300, 28, 0, 0, 0, 0.4)
        ffe.drawText("Tick: " .. tick .. "  |  WASD move  |  ESC quit", 16, 686, 2, 0.4, 0.4, 0.5, 0.7)

        return
    end

    -- -----------------------------------------------------------------------
    -- Client mode
    -- -----------------------------------------------------------------------
    if mode == "client" then
        local sw = ffe.getScreenWidth()

        if connected and localEntity then
            -- Build input bits from WASD
            local bits = 0
            if ffe.isKeyHeld(ffe.KEY_W) then bits = bits + INPUT_UP end
            if ffe.isKeyHeld(ffe.KEY_S) then bits = bits + INPUT_DOWN end
            if ffe.isKeyHeld(ffe.KEY_A) then bits = bits + INPUT_LEFT end
            if ffe.isKeyHeld(ffe.KEY_D) then bits = bits + INPUT_RIGHT end

            -- Send input to server
            ffe.sendInput({bits = bits, aimX = 0, aimY = 0})

            -- Client-side prediction: move local entity immediately
            if bits ~= 0 then
                if ffe.fillTransform(localEntity, transformBuf) then
                    local dx, dy = 0, 0
                    if bits >= INPUT_RIGHT and (bits % (INPUT_RIGHT * 2)) >= INPUT_RIGHT then
                        dx = dx + PLAYER_SPEED * dt
                    end
                    if bits >= INPUT_LEFT and (bits % (INPUT_LEFT * 2)) >= INPUT_LEFT then
                        dx = dx - PLAYER_SPEED * dt
                    end
                    if bits >= INPUT_UP and (bits % (INPUT_UP * 2)) >= INPUT_UP then
                        dy = dy + PLAYER_SPEED * dt
                    end
                    if bits >= INPUT_DOWN and (bits % (INPUT_DOWN * 2)) >= INPUT_DOWN then
                        dy = dy - PLAYER_SPEED * dt
                    end

                    local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
                    ffe.setTransform(localEntity, nx, ny,
                                     transformBuf.rotation,
                                     transformBuf.scaleX, transformBuf.scaleY)
                end
            end
        end

        -- Draw arena border
        ffe.drawRect(0, 0, sw, 4, 0.2, 0.3, 0.4, 0.6)
        ffe.drawRect(0, 716, sw, 4, 0.2, 0.3, 0.4, 0.6)
        ffe.drawRect(0, 0, 4, 720, 0.2, 0.3, 0.4, 0.6)
        ffe.drawRect(1276, 0, 4, 720, 0.2, 0.3, 0.4, 0.6)

        -- HUD
        ffe.drawRect(8, 8, 360, 60, 0, 0, 0, 0.6)
        if connected then
            ffe.drawText("CLIENT - Connected", 16, 14, 2, 0.3, 0.7, 1.0, 1)
            ffe.drawText("My ID: " .. myClientId, 16, 38, 2, 0.8, 0.8, 0.8, 1)
        else
            local alpha = 0.5 + 0.5 * math.sin(gameTime * 3)
            ffe.drawText("CLIENT - Connecting...", 16, 14, 2, 1.0, 0.9, 0.3, alpha)
        end

        -- Debug info
        local tick = ffe.getNetworkTick()
        local predErr = ffe.getPredictionError()
        ffe.drawRect(8, 680, 460, 28, 0, 0, 0, 0.4)
        ffe.drawText("Tick: " .. tick ..
                     "  Pred err: " .. string.format("%.1f", predErr) ..
                     "  |  WASD move  |  ESC quit",
                     16, 686, 2, 0.4, 0.4, 0.5, 0.7)

        return
    end
end

-- ---------------------------------------------------------------------------
-- shutdown() -- called by ScriptEngine before lua_close()
-- ---------------------------------------------------------------------------
function shutdown()
    if mode == "server" then
        ffe.stopServer()
    elseif mode == "client" then
        ffe.disconnect()
    end
    ffe.log("Net Arena shutdown complete")
end
