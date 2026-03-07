# Multiplayer Basics

In this tutorial you will build **Net Arena** -- a 2D multiplayer game where players move colored squares around a shared arena. Two players connect over the network and see each other move in real time.

Along the way you will learn the core concepts behind networked multiplayer games: the client-server model, authoritative servers, input replication, and client-side prediction.

*(Net Arena preview -- two colored squares moving in a shared 2D arena over the network.)*

---

## Prerequisites

Before starting this tutorial you should:

- Have FFE built and running on your machine (see [Getting Started](../getting-started.md))
- Be comfortable with the FFE Lua API (entities, transforms, sprites, input, and the game loop)
- Have completed [Your First 2D Game](first-2d-game.md) or have equivalent experience

!!! note "Two terminals required"
    Multiplayer means multiple instances of the game running at the same time. You will need **two terminal windows** to test -- one for the server and one for the client. Both run the same Lua script; the player chooses which role to play at startup.

---

## Networking Concepts

Before writing any code, let's cover three ideas that every multiplayer game relies on.

**Client-server model.** One instance of the game acts as the **server** -- it is the authority on the game state. Every other instance is a **client** that connects to the server. Clients send their input to the server; the server processes that input, updates the world, and sends the result back. This is the same model used by most online games, from first-person shooters to MMOs.

**Authoritative server.** The server is always right. If the server says your player is at position (100, 50), that is where your player is -- even if your local screen shows something slightly different. This prevents cheating: a hacked client cannot teleport itself because the server ignores illegal positions.

**Client-side prediction.** If the client waited for the server to confirm every movement before showing it on screen, the game would feel sluggish. Instead, the client *predicts* its own movement immediately based on the input it just sent, then corrects itself when the server's authoritative response arrives. When prediction is accurate (and it usually is), the player sees instant, responsive movement. When it drifts, the engine quietly snaps the player back to the correct position. FFE handles this correction automatically once you tell it which entity is the local player.

---

## Step 1: Constants and State

Create a new file called `net_arena.lua`. Start by defining the constants and state variables the game will use.

```lua
-- net_arena.lua -- Multiplayer Arena Tutorial
-- Controls: S = host server, C = connect as client, WASD = move, ESC = quit

-- Constants
local PORT         = 7777
local PLAYER_SPEED = 250.0
local PLAYER_SIZE  = 40
local HALF_W       = 640   -- half of 1280 window width
local HALF_H       = 360   -- half of 720 window height

-- Input bit flags
local INPUT_UP    = 1
local INPUT_DOWN  = 2
local INPUT_LEFT  = 4
local INPUT_RIGHT = 8

-- A color palette for players -- each client gets one based on their ID
local COLORS = {
    {0.3, 0.7, 1.0},   -- blue
    {1.0, 0.4, 0.3},   -- red
    {0.3, 1.0, 0.5},   -- green
    {1.0, 0.9, 0.2},   -- yellow
    {0.9, 0.4, 0.9},   -- purple
    {1.0, 0.6, 0.2},   -- orange
}

-- Game state
local mode         = "menu"     -- "menu", "server", or "client"
local gameTime     = 0
local transformBuf = {}         -- reusable buffer for fillTransform

-- Server-only state
local players      = {}         -- clientId -> { entity, color }
local serverEntity = nil        -- the host's own player entity

-- Client-only state
local localEntity  = nil        -- our predicted player entity
local connected    = false
local myClientId   = -1
```

A few things to note:

- **Input bit flags** encode which keys are pressed as a single integer. Bit 1 means UP, bit 2 means DOWN, and so on. This is how `ffe.sendInput` expects input to be packed.
- **`mode`** tracks whether this instance is the menu screen, the server, or a client. The same script handles all three roles.
- **`transformBuf`** is a reusable Lua table that `ffe.fillTransform` writes position data into. Reusing it avoids creating garbage every frame.

---

## Step 2: Helper Functions

Add two small helpers -- one to pick a color for a client ID, and one to create a player entity.

```lua
local function colorForId(id)
    local idx = (id % #COLORS) + 1
    return COLORS[idx]
end

local function createPlayerEntity(x, y, r, g, b)
    local id = ffe.createEntity()
    if not id then return nil end
    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addPreviousTransform(id)
    ffe.addSprite(id, 1, PLAYER_SIZE, PLAYER_SIZE, r, g, b, 1.0, 1)
    return id
end

local function clampPos(x, y)
    local limit_x = HALF_W - 30
    local limit_y = HALF_H - 30
    x = math.max(-limit_x, math.min(limit_x, x))
    y = math.max(-limit_y, math.min(limit_y, y))
    return x, y
end
```

`createPlayerEntity` creates an entity with a transform, a previous-transform (needed for smooth rendering interpolation), and a colored sprite. The texture handle `1` is the engine's built-in white texture -- the color tint does all the visual work.

`clampPos` keeps players inside the arena bounds so they cannot wander off-screen.

---

## Step 3: Start the Server

Now write the function that starts hosting a game. This is the heart of the server setup.

```lua
local function startServer()
    -- NOTE: FFE seeds the RNG automatically; no need to call math.randomseed().

    local ok = ffe.startServer(PORT)
    if not ok then
        ffe.log("ERROR: Failed to start server on port " .. PORT)
        return false
    end

    ffe.log("Server started on port " .. PORT)
    mode = "server"

    -- Set the network tick rate (how often the server sends snapshots)
    ffe.setNetworkTickRate(20)

    -- Register callbacks for client lifecycle
    ffe.onClientConnected(function(clientId)
        ffe.log("Server: client " .. clientId .. " connected")

        local color  = colorForId(clientId)
        local spawnX = (math.random() * 2 - 1) * (HALF_W - 100)
        local spawnY = (math.random() * 2 - 1) * (HALF_H - 100)

        local entity = createPlayerEntity(spawnX, spawnY,
                                          color[1], color[2], color[3])
        if entity then
            players[clientId] = { entity = entity, color = color }
        end
    end)

    ffe.onClientDisconnected(function(clientId)
        ffe.log("Server: client " .. clientId .. " disconnected")
        local info = players[clientId]
        if info and info.entity then
            ffe.destroyEntity(info.entity)
        end
        players[clientId] = nil
    end)

    -- Create the server host's own player (a white marker)
    serverEntity = createPlayerEntity(0, 0, 0.9, 0.9, 0.9)

    ffe.setBackgroundColor(0.05, 0.08, 0.12)
    return true
end
```

Let's break down what happens here:

1. **`ffe.startServer(PORT)`** opens a UDP socket on port 7777 and begins listening for connections.
2. **`ffe.setNetworkTickRate(20)`** tells the engine to send world snapshots to clients 20 times per second. This is separate from the rendering frame rate -- the game still renders at full speed, but network updates happen at this fixed rate.
3. **`ffe.onClientConnected`** fires whenever a new client successfully connects. We create a player entity for them at a random position and store it in the `players` table.
4. **`ffe.onClientDisconnected`** fires when a client drops. We destroy their entity and remove them from our tracking table.

!!! info "Network tick rate"
    20 Hz is a good default for most games. Higher rates (e.g., 60 Hz) send more data and use more bandwidth. Lower rates (e.g., 10 Hz) feel less responsive. FFE's snapshot interpolation smooths the gaps between ticks on the client side, so 20 Hz looks smooth even at 60+ fps rendering.

---

## Step 4: Connect as a Client

The client side mirrors the server setup, but from the other direction.

```lua
local function startClient()
    -- Register callbacks BEFORE connecting
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
```

Key points:

1. **Register callbacks before connecting.** `ffe.onConnected` and `ffe.onDisconnected` must be set up before `ffe.connectToServer` is called, otherwise you might miss the connection event.
2. **`ffe.getClientId()`** returns the unique ID the server assigned to this client. We use it to pick our player color.
3. **`ffe.setLocalPlayer(localEntity)`** is the crucial call that enables client-side prediction. It tells the engine: "This entity is mine -- apply my inputs locally for instant feedback, and reconcile with the server when snapshots arrive."

!!! warning "Register callbacks before connecting"
    Always call `ffe.onConnected()` and `ffe.onDisconnected()` **before** `ffe.connectToServer()`. If the connection succeeds very quickly (e.g., localhost), the callback might fire before you register it, and you will miss the event entirely.

---

## Step 5: Send Input from the Client

Each frame, the client reads which keys are pressed, packs them into a bit field, and sends them to the server.

This code goes inside the `update` function (which we will assemble in full shortly). Here is the client input section:

```lua
-- Inside update(), when mode == "client" and connected:
if connected and localEntity then
    -- Pack WASD into a bitfield
    local bits = 0
    if ffe.isKeyHeld(ffe.KEY_W) then bits = bits + INPUT_UP    end
    if ffe.isKeyHeld(ffe.KEY_S) then bits = bits + INPUT_DOWN   end
    if ffe.isKeyHeld(ffe.KEY_A) then bits = bits + INPUT_LEFT   end
    if ffe.isKeyHeld(ffe.KEY_D) then bits = bits + INPUT_RIGHT  end

    -- Send input to the server
    ffe.sendInput({bits = bits, aimX = 0, aimY = 0})

    -- Client-side prediction: move locally for instant feedback
    if bits ~= 0 then
        if ffe.fillTransform(localEntity, transformBuf) then
            local dx, dy = 0, 0
            if (bits % (INPUT_RIGHT * 2)) >= INPUT_RIGHT then dx = dx + PLAYER_SPEED * dt end
            if (bits % (INPUT_LEFT * 2))  >= INPUT_LEFT  then dx = dx - PLAYER_SPEED * dt end
            if (bits % (INPUT_UP * 2))    >= INPUT_UP    then dy = dy + PLAYER_SPEED * dt end
            if (bits % (INPUT_DOWN * 2))  >= INPUT_DOWN  then dy = dy - PLAYER_SPEED * dt end

            local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
            ffe.setTransform(localEntity, nx, ny,
                             transformBuf.rotation,
                             transformBuf.scaleX, transformBuf.scaleY)
        end
    end
end
```

**`ffe.sendInput`** does two things at once:

1. It sends the input to the server over the network.
2. It records the input in the local prediction buffer so the engine can replay inputs during reconciliation.

The `bits` field is a bitfield -- each bit represents one button. The `aimX` and `aimY` fields are optional and used for aiming in shooter-style games (we set them to 0 here).

After sending the input, the client immediately applies the same movement locally. This is the "prediction" -- the client assumes the server will process the input the same way.

!!! warning "Don't call sendInput on the server"
    `ffe.sendInput` is a client-only function. The server processes input directly -- it does not send input to itself. Calling `sendInput` on the server will return `false` and do nothing.

---

## Step 6: Process Input on the Server

The server receives input from each client via a callback. It applies movement authoritatively -- the server's position is the truth.

Add this inside `startServer()`, right after the `onClientDisconnected` callback:

```lua
    -- Handle input from connected clients
    ffe.onServerInput(function(clientId, inputTable)
        local info = players[clientId]
        if not info or not info.entity then return end

        local bits = inputTable.bits or 0
        local dt   = inputTable.dt or (1.0 / 60.0)

        local dx, dy = 0, 0
        if (bits % (INPUT_UP * 2))    >= INPUT_UP    then dy = dy + PLAYER_SPEED * dt end
        if (bits % (INPUT_DOWN * 2))  >= INPUT_DOWN  then dy = dy - PLAYER_SPEED * dt end
        if (bits % (INPUT_LEFT * 2))  >= INPUT_LEFT  then dx = dx - PLAYER_SPEED * dt end
        if (bits % (INPUT_RIGHT * 2)) >= INPUT_RIGHT then dx = dx + PLAYER_SPEED * dt end

        if dx ~= 0 or dy ~= 0 then
            if ffe.fillTransform(info.entity, transformBuf) then
                local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
                ffe.setTransform(info.entity, nx, ny,
                                 transformBuf.rotation,
                                 transformBuf.scaleX, transformBuf.scaleY)
            end
        end
    end)
```

The `inputTable` that `ffe.onServerInput` receives contains:

| Field | Type | Description |
|-------|------|-------------|
| `bits` | integer | Bitfield of pressed buttons |
| `aimX` | number | Aim direction X |
| `aimY` | number | Aim direction Y |
| `tick` | integer | The client tick when this input was recorded |
| `dt` | number | Delta time for this tick |

Notice that the movement logic here is **identical** to the client prediction code in Step 5. This is essential -- if the server and client apply different movement rules, prediction will constantly disagree with the server, causing visible rubber-banding.

!!! tip "Keep movement logic identical"
    The server and client must use exactly the same movement formula. If you change `PLAYER_SPEED`, clamp bounds, or any other movement parameter, change it in both places. A common pattern is to extract the movement logic into a shared function.

---

## Step 7: Client-Side Prediction

We already did the hard work in Steps 4 and 5. Let's recap what makes prediction work:

1. **`ffe.setLocalPlayer(entityId)`** -- called once after creating the local entity (Step 4). This tells the engine which entity to predict and reconcile.
2. **`ffe.sendInput({bits=..., aimX=..., aimY=...})`** -- called every frame (Step 5). This records the input for replay during reconciliation.
3. **Identical movement logic** -- the client applies the same movement as the server (Steps 5 and 6).

When the server sends a snapshot back, the engine compares the server's authoritative position with where the client predicted the player would be. If the difference is small (less than 0.1 units), nothing happens -- the prediction was accurate. If the difference is larger, the engine snaps to the server position and replays any inputs that happened after the server's snapshot tick. This correction is usually invisible to the player.

You can monitor prediction accuracy using:

```lua
local predictionError = ffe.getPredictionError()
```

This returns the distance (in world units) between the last predicted position and the server's authoritative position. In normal gameplay over localhost, this will be very close to zero.

---

## Step 8: HUD and Status Display

Add visual feedback so players know what is happening. This goes in the `update` function for each mode.

**Server HUD:**

```lua
-- Count connected players
local playerCount = 0
for _ in pairs(players) do playerCount = playerCount + 1 end

-- Background panel
ffe.drawRect(8, 8, 360, 60, 0, 0, 0, 0.6)
ffe.drawText("SERVER - Port " .. PORT, 16, 14, 2, 0.3, 1.0, 0.5, 1)
ffe.drawText("Players: " .. playerCount, 16, 38, 2, 0.8, 0.8, 0.8, 1)

-- Footer with tick info
local tick = ffe.getNetworkTick()
ffe.drawRect(8, 680, 300, 28, 0, 0, 0, 0.4)
ffe.drawText("Tick: " .. tick .. "  |  WASD move  |  ESC quit",
             16, 686, 2, 0.4, 0.4, 0.5, 0.7)
```

**Client HUD:**

```lua
ffe.drawRect(8, 8, 360, 60, 0, 0, 0, 0.6)
if connected then
    ffe.drawText("CLIENT - Connected", 16, 14, 2, 0.3, 0.7, 1.0, 1)
    ffe.drawText("My ID: " .. myClientId, 16, 38, 2, 0.8, 0.8, 0.8, 1)
else
    local alpha = 0.5 + 0.5 * math.sin(gameTime * 3)
    ffe.drawText("CLIENT - Connecting...", 16, 14, 2, 1.0, 0.9, 0.3, alpha)
end

-- Footer with tick and prediction error
local tick    = ffe.getNetworkTick()
local predErr = ffe.getPredictionError()
ffe.drawRect(8, 680, 460, 28, 0, 0, 0, 0.4)
ffe.drawText("Tick: " .. tick ..
             "  Pred err: " .. string.format("%.1f", predErr) ..
             "  |  WASD move  |  ESC quit",
             16, 686, 2, 0.4, 0.4, 0.5, 0.7)
```

The "Pred err" display is useful during development -- it shows you how far off client prediction is from the server's truth. On localhost it should hover near 0.0. Over a real network with latency, small values (under 5.0) indicate healthy prediction.

---

## Step 9: Testing It Out

To test multiplayer locally:

**Terminal 1 -- start the server:**

```bash
cd your-ffe-project/
./build/ffe_runner examples/net_demo/net_arena.lua
```

Press ++s++ to host a server.

**Terminal 2 -- connect as a client:**

```bash
cd your-ffe-project/
./build/ffe_runner examples/net_demo/net_arena.lua
```

Press ++c++ to connect as a client.

You should see:

1. The server window shows "SERVER - Port 7777" and a white square (the host marker).
2. The client window shows "CLIENT - Connected" and a colored square.
3. Moving with WASD on the client moves the colored square on **both** windows.
4. The server can also move its white marker with WASD.

Press ++esc++ in either window to quit cleanly.

!!! tip "Troubleshooting"
    **"Failed to start server"** -- another process is using port 7777. Close it or change `PORT` in the script.

    **"Failed to connect"** -- make sure the server is running first. The client cannot connect if there is nothing listening.

    **Client connects but no movement appears on server** -- check that `ffe.onServerInput` is registered. Without it, the server receives input packets but ignores them.

---

## Complete Code

Here is the full working script. Save it as `net_arena.lua` and run it in two terminal windows.

```lua
-- net_arena.lua -- Multiplayer Arena Tutorial
--
-- A 2D networked arena: colored squares moving in a shared space.
-- Controls: S = host server, C = connect as client, WASD = move, ESC = quit
-- Window: 1280x720, centered origin (-640..640, -360..360).

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local PORT         = 7777
local PLAYER_SPEED = 250.0
local PLAYER_SIZE  = 40
local HALF_W       = 640
local HALF_H       = 360

local INPUT_UP    = 1
local INPUT_DOWN  = 2
local INPUT_LEFT  = 4
local INPUT_RIGHT = 8

local COLORS = {
    {0.3, 0.7, 1.0},   -- blue
    {1.0, 0.4, 0.3},   -- red
    {0.3, 1.0, 0.5},   -- green
    {1.0, 0.9, 0.2},   -- yellow
    {0.9, 0.4, 0.9},   -- purple
    {1.0, 0.6, 0.2},   -- orange
}

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local mode         = "menu"
local gameTime     = 0
local transformBuf = {}

-- Server state
local players      = {}
local serverEntity = nil

-- Client state
local localEntity  = nil
local connected    = false
local myClientId   = -1

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
local function colorForId(id)
    local idx = (id % #COLORS) + 1
    return COLORS[idx]
end

local function createPlayerEntity(x, y, r, g, b)
    local id = ffe.createEntity()
    if not id then return nil end
    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addPreviousTransform(id)
    ffe.addSprite(id, 1, PLAYER_SIZE, PLAYER_SIZE, r, g, b, 1.0, 1)
    return id
end

local function clampPos(x, y)
    local limit_x = HALF_W - 30
    local limit_y = HALF_H - 30
    x = math.max(-limit_x, math.min(limit_x, x))
    y = math.max(-limit_y, math.min(limit_y, y))
    return x, y
end

-- ---------------------------------------------------------------------------
-- Server setup
-- ---------------------------------------------------------------------------
local function startServer()
    -- NOTE: FFE seeds the RNG automatically; no need to call math.randomseed().

    local ok = ffe.startServer(PORT)
    if not ok then
        ffe.log("ERROR: Failed to start server on port " .. PORT)
        return false
    end

    ffe.log("Server started on port " .. PORT)
    mode = "server"
    ffe.setNetworkTickRate(20)

    ffe.onClientConnected(function(clientId)
        ffe.log("Server: client " .. clientId .. " connected")
        local color  = colorForId(clientId)
        local spawnX = (math.random() * 2 - 1) * (HALF_W - 100)
        local spawnY = (math.random() * 2 - 1) * (HALF_H - 100)
        local entity = createPlayerEntity(spawnX, spawnY,
                                          color[1], color[2], color[3])
        if entity then
            players[clientId] = { entity = entity, color = color }
        end
    end)

    ffe.onClientDisconnected(function(clientId)
        ffe.log("Server: client " .. clientId .. " disconnected")
        local info = players[clientId]
        if info and info.entity then
            ffe.destroyEntity(info.entity)
        end
        players[clientId] = nil
    end)

    ffe.onServerInput(function(clientId, inputTable)
        local info = players[clientId]
        if not info or not info.entity then return end

        local bits = inputTable.bits or 0
        local dt   = inputTable.dt or (1.0 / 60.0)

        local dx, dy = 0, 0
        if (bits % (INPUT_UP * 2))    >= INPUT_UP    then dy = dy + PLAYER_SPEED * dt end
        if (bits % (INPUT_DOWN * 2))  >= INPUT_DOWN  then dy = dy - PLAYER_SPEED * dt end
        if (bits % (INPUT_LEFT * 2))  >= INPUT_LEFT  then dx = dx - PLAYER_SPEED * dt end
        if (bits % (INPUT_RIGHT * 2)) >= INPUT_RIGHT then dx = dx + PLAYER_SPEED * dt end

        if dx ~= 0 or dy ~= 0 then
            if ffe.fillTransform(info.entity, transformBuf) then
                local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
                ffe.setTransform(info.entity, nx, ny,
                                 transformBuf.rotation,
                                 transformBuf.scaleX, transformBuf.scaleY)
            end
        end
    end)

    serverEntity = createPlayerEntity(0, 0, 0.9, 0.9, 0.9)
    ffe.setBackgroundColor(0.05, 0.08, 0.12)
    return true
end

-- ---------------------------------------------------------------------------
-- Client setup
-- ---------------------------------------------------------------------------
local function startClient()
    ffe.onConnected(function()
        connected  = true
        myClientId = ffe.getClientId()
        ffe.log("Client: connected with ID " .. myClientId)

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
ffe.log("Net Arena loaded. Press S to host, C to connect.")

-- ---------------------------------------------------------------------------
-- Main update loop
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    gameTime = gameTime + dt

    -- ESC to quit from any state
    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        if mode == "server" then ffe.stopServer()
        elseif mode == "client" then ffe.disconnect()
        end
        ffe.requestShutdown()
        return
    end

    -- -------------------------------------------------------------------
    -- Menu: choose server or client
    -- -------------------------------------------------------------------
    if mode == "menu" then
        local sw = ffe.getScreenWidth()

        ffe.drawText("NET ARENA", sw / 2 - 144, 120, 4, 0.3, 0.7, 1.0, 1)
        ffe.drawText("A FastFreeEngine Multiplayer Demo",
                     sw / 2 - 264, 190, 2, 0.5, 0.5, 0.6, 0.8)

        ffe.drawText("Press S to HOST a server",
                     sw / 2 - 192, 300, 2, 0.3, 1.0, 0.5, 1)
        ffe.drawText("Press C to CONNECT as client",
                     sw / 2 - 224, 340, 2, 0.3, 0.7, 1.0, 1)

        local alpha = 0.4 + 0.3 * math.sin(gameTime * 2)
        ffe.drawText("(Run two instances for multiplayer)",
                     sw / 2 - 280, 420, 2, 0.5, 0.5, 0.5, alpha)

        ffe.drawText("ESC to quit", sw / 2 - 88, 520, 2, 0.4, 0.4, 0.4, 0.6)

        if ffe.isKeyPressed(ffe.KEY_S) then startServer()
        elseif ffe.isKeyPressed(ffe.KEY_C) then startClient()
        end
        return
    end

    -- -------------------------------------------------------------------
    -- Helper: draw the arena border
    -- -------------------------------------------------------------------
    local sw = ffe.getScreenWidth()
    ffe.drawRect(0, 0, sw, 4, 0.2, 0.3, 0.4, 0.6)
    ffe.drawRect(0, 716, sw, 4, 0.2, 0.3, 0.4, 0.6)
    ffe.drawRect(0, 0, 4, 720, 0.2, 0.3, 0.4, 0.6)
    ffe.drawRect(1276, 0, 4, 720, 0.2, 0.3, 0.4, 0.6)

    -- -------------------------------------------------------------------
    -- Server mode
    -- -------------------------------------------------------------------
    if mode == "server" then
        -- Move the host's own marker with WASD
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

        local playerCount = 0
        for _ in pairs(players) do playerCount = playerCount + 1 end

        ffe.drawRect(8, 8, 360, 60, 0, 0, 0, 0.6)
        ffe.drawText("SERVER - Port " .. PORT, 16, 14, 2, 0.3, 1.0, 0.5, 1)
        ffe.drawText("Players: " .. playerCount, 16, 38, 2, 0.8, 0.8, 0.8, 1)

        local tick = ffe.getNetworkTick()
        ffe.drawRect(8, 680, 300, 28, 0, 0, 0, 0.4)
        ffe.drawText("Tick: " .. tick .. "  |  WASD move  |  ESC quit",
                     16, 686, 2, 0.4, 0.4, 0.5, 0.7)
        return
    end

    -- -------------------------------------------------------------------
    -- Client mode
    -- -------------------------------------------------------------------
    if mode == "client" then
        if connected and localEntity then
            local bits = 0
            if ffe.isKeyHeld(ffe.KEY_W) then bits = bits + INPUT_UP    end
            if ffe.isKeyHeld(ffe.KEY_S) then bits = bits + INPUT_DOWN   end
            if ffe.isKeyHeld(ffe.KEY_A) then bits = bits + INPUT_LEFT   end
            if ffe.isKeyHeld(ffe.KEY_D) then bits = bits + INPUT_RIGHT  end

            ffe.sendInput({bits = bits, aimX = 0, aimY = 0})

            if bits ~= 0 then
                if ffe.fillTransform(localEntity, transformBuf) then
                    local dx, dy = 0, 0
                    if (bits % (INPUT_RIGHT * 2)) >= INPUT_RIGHT then
                        dx = dx + PLAYER_SPEED * dt
                    end
                    if (bits % (INPUT_LEFT * 2)) >= INPUT_LEFT then
                        dx = dx - PLAYER_SPEED * dt
                    end
                    if (bits % (INPUT_UP * 2)) >= INPUT_UP then
                        dy = dy + PLAYER_SPEED * dt
                    end
                    if (bits % (INPUT_DOWN * 2)) >= INPUT_DOWN then
                        dy = dy - PLAYER_SPEED * dt
                    end

                    local nx, ny = clampPos(transformBuf.x + dx, transformBuf.y + dy)
                    ffe.setTransform(localEntity, nx, ny,
                                     transformBuf.rotation,
                                     transformBuf.scaleX, transformBuf.scaleY)
                end
            end
        end

        ffe.drawRect(8, 8, 360, 60, 0, 0, 0, 0.6)
        if connected then
            ffe.drawText("CLIENT - Connected", 16, 14, 2, 0.3, 0.7, 1.0, 1)
            ffe.drawText("My ID: " .. myClientId, 16, 38, 2, 0.8, 0.8, 0.8, 1)
        else
            local alpha = 0.5 + 0.5 * math.sin(gameTime * 3)
            ffe.drawText("CLIENT - Connecting...", 16, 14, 2, 1.0, 0.9, 0.3, alpha)
        end

        local tick    = ffe.getNetworkTick()
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
-- Clean shutdown
-- ---------------------------------------------------------------------------
function shutdown()
    if mode == "server" then ffe.stopServer()
    elseif mode == "client" then ffe.disconnect()
    end
    ffe.log("Net Arena shutdown complete")
end
```

---

## What You Learned

In this tutorial you built a complete networked multiplayer game. Here is a summary of the networking concepts and API calls you used:

| Concept | FFE API | Purpose |
|---------|---------|---------|
| Start a server | `ffe.startServer(port)` | Listen for client connections |
| Connect to a server | `ffe.connectToServer(host, port)` | Join a running game |
| Client lifecycle (server) | `ffe.onClientConnected(cb)`, `ffe.onClientDisconnected(cb)` | Track who is in the game |
| Client lifecycle (client) | `ffe.onConnected(cb)`, `ffe.onDisconnected(cb)` | React to connection state |
| Send input | `ffe.sendInput({bits, aimX, aimY})` | Replicate player input to the server |
| Receive input (server) | `ffe.onServerInput(cb)` | Process client input authoritatively |
| Client-side prediction | `ffe.setLocalPlayer(entityId)` | Enable instant local movement |
| Monitor prediction | `ffe.getPredictionError()` | Debug prediction accuracy |
| Network tick | `ffe.setNetworkTickRate(hz)`, `ffe.getNetworkTick()` | Control snapshot frequency |
| Clean shutdown | `ffe.stopServer()`, `ffe.disconnect()` | Graceful cleanup |

---

## Next Steps

You have a working multiplayer foundation. Here are some ideas to extend it:

- **Add a lobby system.** Use `ffe.createLobby()`, `ffe.joinLobby()`, and `ffe.setReady()` to let players gather and ready up before the game starts. See the [Networking API reference](../api/networking.md) for the full lobby API.
- **Send game messages.** Use `ffe.sendMessage(msgType, data)` to implement chat, score updates, or custom game events.
- **Add shooting with lag compensation.** Use `ffe.performHitCheck()` to implement server-authoritative hit detection that accounts for network latency.
- **Support more players.** The server already handles up to 32 clients. Try connecting 3-4 instances and watch them all move in the same arena.
- **Connect over a real network.** Change `"127.0.0.1"` to the server machine's IP address and test across two computers on the same LAN.
