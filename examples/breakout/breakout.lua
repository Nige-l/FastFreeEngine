-- breakout.lua -- Classic Breakout for FFE
--
-- Paddle (A/D or LEFT/RIGHT), ball, destructible bricks.
-- Press SPACE to launch. Clear all bricks to win.
--
-- Demonstrates: mass entity destruction, entity creation, input, audio, HUD,
-- particle effects, ball trail, paddle flash, visual juice.
--
-- Coordinate system: centered origin, x: -640..640, y: -360..360.
-- Window: 1280x720.

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local HALF_W         = 640
local HALF_H         = 360

local PADDLE_W       = 120
local PADDLE_H       = 16
local PADDLE_Y       = -300
local PADDLE_SPEED   = 500.0

local BALL_SIZE      = 12
local BALL_SPEED     = 350.0
local BALL_MAX_SPEED = 600.0

local BRICK_W        = 80
local BRICK_H        = 24
local BRICK_COLS     = 14
local BRICK_ROWS     = 6
local BRICK_GAP      = 4
local BRICK_TOP_Y    = 280

local LIVES_START    = 3

-- Particle constants
local MAX_PARTICLES  = 40
local PARTICLE_SIZE  = 6
local PARTICLE_LIFE  = 0.4

-- Trail constants
local TRAIL_COUNT    = 5
local TRAIL_SIZE     = 8

-- ---------------------------------------------------------------------------
-- State
-- ---------------------------------------------------------------------------
local whiteTex       = nil
local sfxBrick       = nil
local sfxPaddle      = nil
local sfxWall        = nil
local sfxLose        = nil
local musicHandle    = nil
local musicPlaying   = false

local paddle         = nil
local ball           = nil
local bricks         = {}
local brickCount     = 0

local ballVx         = 0
local ballVy         = 0
local ballSpeed      = BALL_SPEED
local ballLaunched   = false
local lives          = LIVES_START
local score          = 0
local gameOver       = false
local gameWon        = false
local gameTime       = 0

local transformBuf   = {}

-- Particle pool
local particles      = {}
local particleCount  = 0

-- Trail pool (fixed entities reused every frame)
local trail          = {}

-- Paddle flash state
local paddleFlashTimer = 0
local paddleBaseColor  = {0.9, 0.9, 0.9}

-- Life indicator entities
local lifeIndicators = {}

-- ---------------------------------------------------------------------------
-- Brick colors by row (top to bottom: red, orange, yellow, green, cyan, blue)
-- ---------------------------------------------------------------------------
local ROW_COLORS = {
    {1.0, 0.3, 0.3},  -- red
    {1.0, 0.6, 0.2},  -- orange
    {1.0, 0.9, 0.3},  -- yellow
    {0.3, 0.9, 0.3},  -- green
    {0.3, 0.8, 0.9},  -- cyan
    {0.4, 0.5, 1.0},  -- blue
}

-- Points per row (top rows worth more)
local ROW_POINTS = {6, 5, 4, 3, 2, 1}

-- ---------------------------------------------------------------------------
-- Helper: update HUD
-- ---------------------------------------------------------------------------
local function updateHud()
    if gameOver then
        if gameWon then
            ffe.setHudText("YOU WIN!  Score: " .. score ..
                           "  |  SPACE restart  |  M music  |  ESC quit")
        else
            ffe.setHudText("GAME OVER  Score: " .. score ..
                           "  |  SPACE restart  |  M music  |  ESC quit")
        end
    elseif not ballLaunched then
        ffe.setHudText("Score: " .. score .. "  Lives: " .. lives ..
                       "  |  SPACE to launch  |  A/D move  |  M music  |  ESC quit")
    else
        ffe.setHudText("Score: " .. score .. "  Lives: " .. lives ..
                       "  |  A/D move  |  M music  |  ESC quit")
    end
end

-- ---------------------------------------------------------------------------
-- Helper: create a colored rectangle
-- ---------------------------------------------------------------------------
local function createRect(x, y, w, h, r, g, b, layer)
    local id = ffe.createEntity()
    if not id then return nil end
    ffe.addTransform(id, x, y, 0, 1, 1)
    ffe.addSprite(id, whiteTex or 1, w, h, r, g, b, 1, layer or 1)
    ffe.addPreviousTransform(id)
    return id
end

-- ---------------------------------------------------------------------------
-- Particle system
-- ---------------------------------------------------------------------------
local function spawnParticles(x, y, r, g, b, count)
    for i = 1, count do
        if particleCount >= MAX_PARTICLES then break end
        local vx = (math.random() - 0.5) * 400
        local vy = (math.random() - 0.5) * 400 + 100  -- bias upward
        local id = createRect(x, y, PARTICLE_SIZE, PARTICLE_SIZE, r, g, b, 4)
        if id then
            particleCount = particleCount + 1
            particles[particleCount] = {
                id = id, vx = vx, vy = vy,
                life = PARTICLE_LIFE, maxLife = PARTICLE_LIFE,
                r = r, g = g, b = b
            }
        end
    end
end

local function updateParticles(dt)
    local i = 1
    while i <= particleCount do
        local p = particles[i]
        p.life = p.life - dt
        if p.life <= 0 then
            ffe.destroyEntity(p.id)
            particles[i] = particles[particleCount]
            particles[particleCount] = nil
            particleCount = particleCount - 1
        else
            -- Move
            if ffe.fillTransform(p.id, transformBuf) then
                local nx = transformBuf.x + p.vx * dt
                local ny = transformBuf.y + p.vy * dt
                p.vy = p.vy - 600 * dt  -- gravity
                ffe.setTransform(p.id, nx, ny, 0, 1, 1)
            end
            -- Fade
            local alpha = p.life / p.maxLife
            ffe.setSpriteColor(p.id, p.r, p.g, p.b, alpha)
            i = i + 1
        end
    end
end

-- ---------------------------------------------------------------------------
-- Trail system (fixed pool, reused every frame)
-- ---------------------------------------------------------------------------
local function initTrail()
    for i = 1, TRAIL_COUNT do
        local id = createRect(-9999, -9999, TRAIL_SIZE, TRAIL_SIZE, 1, 1, 1, 2)
        trail[i] = { id = id, x = -9999, y = -9999 }
    end
end

local trailIndex = 1
local trailTimer = 0

local function updateTrail(bx, by, dt)
    trailTimer = trailTimer + dt
    if trailTimer >= 0.016 then  -- ~60hz trail update
        trailTimer = 0
        trail[trailIndex].x = bx
        trail[trailIndex].y = by
        trailIndex = (trailIndex % TRAIL_COUNT) + 1
    end
    -- Update trail entity positions and alpha
    for i = 1, TRAIL_COUNT do
        local t = trail[i]
        if t.id then
            ffe.setTransform(t.id, t.x, t.y, 0, 1, 1)
            -- Older = more transparent
            local age = ((trailIndex - i - 1) % TRAIL_COUNT) / TRAIL_COUNT
            local alpha = 0.15 + age * 0.2
            local speedRatio = (ballSpeed - BALL_SPEED) / (BALL_MAX_SPEED - BALL_SPEED)
            speedRatio = math.max(0, math.min(1, speedRatio))
            ffe.setSpriteColor(t.id, 1, 1 - speedRatio * 0.6, 1 - speedRatio * 0.8, alpha)
        end
    end
end

-- ---------------------------------------------------------------------------
-- Life indicators
-- ---------------------------------------------------------------------------
local function createLifeIndicators()
    for i = 1, #lifeIndicators do
        ffe.destroyEntity(lifeIndicators[i])
    end
    lifeIndicators = {}
    for i = 1, lives do
        local x = -HALF_W + 20 + (i - 1) * 18
        local y = -HALF_H + 20
        local id = createRect(x, y, 12, 12, 1, 0.4, 0.4, 5)
        lifeIndicators[i] = id
    end
end

-- ---------------------------------------------------------------------------
-- Helper: create all bricks
-- ---------------------------------------------------------------------------
local function createBricks()
    bricks = {}
    brickCount = 0
    local totalW = BRICK_COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP
    local startX = -totalW / 2 + BRICK_W / 2

    for row = 1, BRICK_ROWS do
        local y = BRICK_TOP_Y - (row - 1) * (BRICK_H + BRICK_GAP)
        local c = ROW_COLORS[row] or {1, 1, 1}
        for col = 1, BRICK_COLS do
            local x = startX + (col - 1) * (BRICK_W + BRICK_GAP)
            local id = createRect(x, y, BRICK_W, BRICK_H, c[1], c[2], c[3], 1)
            if id then
                bricks[id] = {row = row, x = x, y = y, r = c[1], g = c[2], b = c[3]}
                brickCount = brickCount + 1
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Helper: reset ball to paddle
-- ---------------------------------------------------------------------------
local function resetBall()
    ballLaunched = false
    ballVx = 0
    ballVy = 0
    ballSpeed = BALL_SPEED
    if ball then
        ffe.setTransform(ball, 0, PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2, 0, 1, 1)
        ffe.setSpriteColor(ball, 1, 1, 1, 1)
    end
    -- Hide trail
    for i = 1, TRAIL_COUNT do
        if trail[i] and trail[i].id then
            ffe.setTransform(trail[i].id, -9999, -9999, 0, 1, 1)
            trail[i].x = -9999
            trail[i].y = -9999
        end
    end
end

-- ---------------------------------------------------------------------------
-- Helper: launch ball
-- ---------------------------------------------------------------------------
local function launchBall()
    local angle = (math.random() * 60 - 30) * math.pi / 180
    ballVx = math.sin(angle) * ballSpeed
    ballVy = math.cos(angle) * ballSpeed
    ballLaunched = true
end

-- ---------------------------------------------------------------------------
-- Helper: lose a life
-- ---------------------------------------------------------------------------
local function loseLife()
    lives = lives - 1
    if sfxLose then ffe.playSound(sfxLose, 0.5) end
    ffe.cameraShake(8, 0.25)

    -- Remove a life indicator
    if #lifeIndicators > 0 then
        ffe.destroyEntity(lifeIndicators[#lifeIndicators])
        lifeIndicators[#lifeIndicators] = nil
    end

    if lives <= 0 then
        gameOver = true
        gameWon  = false
        ffe.log("Game over! Final score: " .. score)
    else
        resetBall()
    end
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Helper: restart the game
-- ---------------------------------------------------------------------------
local function restartGame()
    -- Destroy existing bricks
    for id, _ in pairs(bricks) do
        ffe.destroyEntity(id)
    end
    -- Destroy particles
    for i = 1, particleCount do
        ffe.destroyEntity(particles[i].id)
    end
    particles = {}
    particleCount = 0

    score      = 0
    lives      = LIVES_START
    gameOver   = false
    gameWon    = false
    ballSpeed  = BALL_SPEED

    createBricks()
    createLifeIndicators()
    resetBall()
    updateHud()
end

-- ---------------------------------------------------------------------------
-- Scene init
-- ---------------------------------------------------------------------------
whiteTex    = ffe.loadTexture("textures/white.png")
sfxBrick    = ffe.loadSound("audio/sfx_pong_paddle.wav")
sfxPaddle   = ffe.loadSound("audio/sfx_pong_wall.wav")
sfxWall     = ffe.loadSound("audio/sfx_pong_wall.wav")
sfxLose     = ffe.loadSound("audio/sfx_pong_score.wav")
musicHandle = ffe.loadMusic("audio/music_pixelcrown.ogg")

-- Start background music
if musicHandle then
    ffe.playMusic(musicHandle, true)
    ffe.setMusicVolume(0.15)
    musicPlaying = true
end

-- Create paddle
paddle = createRect(0, PADDLE_Y, PADDLE_W, PADDLE_H, 0.9, 0.9, 0.9, 2)

-- Create ball
ball = createRect(0, PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2,
                  BALL_SIZE, BALL_SIZE, 1, 1, 1, 3)

-- Create walls (visual only — collision handled in code)
createRect(0, HALF_H - 2, HALF_W * 2, 4, 0.3, 0.3, 0.3, 0)    -- top
createRect(-HALF_W + 2, 0, 4, HALF_H * 2, 0.3, 0.3, 0.3, 0)   -- left
createRect(HALF_W - 2, 0, 4, HALF_H * 2, 0.3, 0.3, 0.3, 0)    -- right

-- Create bricks
math.randomseed(42)
createBricks()
initTrail()
createLifeIndicators()

ffe.log("Breakout ready! Press SPACE to launch.")
updateHud()

-- ---------------------------------------------------------------------------
-- update(entityId, dt)
-- ---------------------------------------------------------------------------
function update(entityId, dt)
    gameTime = gameTime + dt

    if ffe.isKeyPressed(ffe.KEY_ESCAPE) then
        ffe.requestShutdown()
        return
    end

    -- Music toggle
    if musicHandle and ffe.isKeyPressed(ffe.KEY_M) then
        if musicPlaying then
            ffe.stopMusic()
            musicPlaying = false
        else
            ffe.playMusic(musicHandle, true)
            musicPlaying = true
        end
    end

    -- Update particles
    updateParticles(dt)

    -- Paddle flash decay
    if paddleFlashTimer > 0 then
        paddleFlashTimer = paddleFlashTimer - dt
        if paddleFlashTimer <= 0 then
            paddleFlashTimer = 0
            if paddle then
                ffe.setSpriteColor(paddle, paddleBaseColor[1], paddleBaseColor[2], paddleBaseColor[3], 1)
            end
        else
            local flash = paddleFlashTimer / 0.12
            if paddle then
                ffe.setSpriteColor(paddle, 1, 1, 1, 0.7 + flash * 0.3)
            end
        end
    end

    -- Ball pulsing before launch
    if not ballLaunched and not gameOver and ball then
        local pulse = 0.9 + 0.1 * math.sin(gameTime * 6)
        ffe.setTransform(ball, transformBuf.x or 0,
                         PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2,
                         0, pulse, pulse)
    end

    -- Restart
    if gameOver and ffe.isKeyPressed(ffe.KEY_SPACE) then
        restartGame()
        return
    end

    -- Launch ball
    if not gameOver and not ballLaunched and ffe.isKeyPressed(ffe.KEY_SPACE) then
        launchBall()
        updateHud()
    end

    -- Move paddle
    if not gameOver and paddle and ffe.fillTransform(paddle, transformBuf) then
        local px = transformBuf.x
        if ffe.isKeyHeld(ffe.KEY_A) or ffe.isKeyHeld(ffe.KEY_LEFT) then
            px = px - PADDLE_SPEED * dt
        end
        if ffe.isKeyHeld(ffe.KEY_D) or ffe.isKeyHeld(ffe.KEY_RIGHT) then
            px = px + PADDLE_SPEED * dt
        end
        -- Clamp
        local halfPad = PADDLE_W / 2
        px = math.max(-HALF_W + halfPad + 4, math.min(HALF_W - halfPad - 4, px))
        ffe.setTransform(paddle, px, PADDLE_Y, 0, 1, 1)

        -- Ball follows paddle before launch
        if not ballLaunched and not gameOver and ball then
            local pulse = 0.9 + 0.1 * math.sin(gameTime * 6)
            ffe.setTransform(ball, px, PADDLE_Y + PADDLE_H / 2 + BALL_SIZE / 2 + 2, 0, pulse, pulse)
        end
    end

    -- Ball physics
    if ballLaunched and not gameOver and ball then
        if not ffe.fillTransform(ball, transformBuf) then return end

        local bx = transformBuf.x + ballVx * dt
        local by = transformBuf.y + ballVy * dt
        local halfBall = BALL_SIZE / 2

        -- Top wall bounce
        if by > HALF_H - halfBall - 4 then
            by = HALF_H - halfBall - 4
            ballVy = -ballVy
            if sfxWall then ffe.playSound(sfxWall, 0.2) end
        end

        -- Side wall bounces
        if bx < -HALF_W + halfBall + 4 then
            bx = -HALF_W + halfBall + 4
            ballVx = -ballVx
            if sfxWall then ffe.playSound(sfxWall, 0.2) end
        elseif bx > HALF_W - halfBall - 4 then
            bx = HALF_W - halfBall - 4
            ballVx = -ballVx
            if sfxWall then ffe.playSound(sfxWall, 0.2) end
        end

        -- Bottom — lose a life
        if by < -HALF_H then
            loseLife()
            return
        end

        -- Paddle collision
        if ballVy < 0 and paddle then
            if ffe.fillTransform(paddle, transformBuf) then
                local px = transformBuf.x
                local halfPW = PADDLE_W / 2
                local halfPH = PADDLE_H / 2
                if bx + halfBall > px - halfPW and
                   bx - halfBall < px + halfPW and
                   by - halfBall < PADDLE_Y + halfPH and
                   by + halfBall > PADDLE_Y - halfPH then
                    by = PADDLE_Y + halfPH + halfBall
                    -- Angle based on hit position
                    local hitPos = (bx - px) / halfPW  -- -1 to 1
                    local angle = hitPos * 60 * math.pi / 180
                    ballVx = math.sin(angle) * ballSpeed
                    ballVy = math.abs(math.cos(angle) * ballSpeed)
                    if sfxPaddle then ffe.playSound(sfxPaddle, 0.4) end
                    -- Flash paddle
                    paddleFlashTimer = 0.12
                    ffe.setSpriteColor(paddle, 1, 1, 1, 1)
                end
            end
        end

        -- Brick collisions
        for id, brick in pairs(bricks) do
            local halfBW = BRICK_W / 2
            local halfBH = BRICK_H / 2
            if bx + halfBall > brick.x - halfBW and
               bx - halfBall < brick.x + halfBW and
               by + halfBall > brick.y - halfBH and
               by - halfBall < brick.y + halfBH then
                -- Destroy brick
                ffe.destroyEntity(id)
                bricks[id] = nil
                brickCount = brickCount - 1
                score = score + (ROW_POINTS[brick.row] or 1)

                -- Spawn particles and screen shake
                spawnParticles(brick.x, brick.y, brick.r, brick.g, brick.b, 5)
                ffe.cameraShake(3, 0.1)

                -- Speed up ball slightly
                ballSpeed = math.min(BALL_MAX_SPEED, ballSpeed + 3)

                -- Determine bounce direction (side vs top/bottom)
                local overlapX = math.min(bx + halfBall - (brick.x - halfBW),
                                          (brick.x + halfBW) - (bx - halfBall))
                local overlapY = math.min(by + halfBall - (brick.y - halfBH),
                                          (brick.y + halfBH) - (by - halfBall))
                if overlapX < overlapY then
                    ballVx = -ballVx
                else
                    ballVy = -ballVy
                end

                -- Normalize velocity to current speed
                local mag = math.sqrt(ballVx * ballVx + ballVy * ballVy)
                if mag > 0 then
                    ballVx = ballVx / mag * ballSpeed
                    ballVy = ballVy / mag * ballSpeed
                end

                if sfxBrick then ffe.playSound(sfxBrick, 0.3) end
                updateHud()

                -- Check win
                if brickCount <= 0 then
                    gameOver = true
                    gameWon  = true
                    -- Victory particle burst
                    for burst = 1, 6 do
                        local c = ROW_COLORS[burst]
                        spawnParticles(math.random(-300, 300), math.random(0, 200),
                                       c[1], c[2], c[3], 5)
                    end
                    ffe.log("You win! Score: " .. score)
                    updateHud()
                end

                break  -- one brick per frame to avoid double-bounce
            end
        end

        ffe.setTransform(ball, bx, by, 0, 1, 1)

        -- Update ball color based on speed
        local speedRatio = (ballSpeed - BALL_SPEED) / (BALL_MAX_SPEED - BALL_SPEED)
        speedRatio = math.max(0, math.min(1, speedRatio))
        ffe.setSpriteColor(ball, 1, 1 - speedRatio * 0.6, 1 - speedRatio * 0.8, 1)

        -- Update trail
        updateTrail(bx, by, dt)
    end
end

-- ---------------------------------------------------------------------------
-- shutdown()
-- ---------------------------------------------------------------------------
function shutdown()
    ffe.log("Breakout final score: " .. score)

    if musicHandle then
        if ffe.isMusicPlaying() then ffe.stopMusic() end
        ffe.unloadSound(musicHandle)
        musicHandle = nil
    end

    if sfxBrick  then ffe.unloadSound(sfxBrick);  sfxBrick  = nil end
    if sfxPaddle then ffe.unloadSound(sfxPaddle); sfxPaddle = nil end
    if sfxWall   then ffe.unloadSound(sfxWall);   sfxWall   = nil end
    if sfxLose   then ffe.unloadSound(sfxLose);   sfxLose   = nil end

    if whiteTex then
        ffe.unloadTexture(whiteTex)
        whiteTex = nil
    end

    ffe.log("Breakout shutdown complete")
end
