#!/usr/bin/env bash
# Input script for Breakout — launch ball and play for GIF recording
# Controls: SPACE to start/launch, A/D or LEFT/RIGHT to move paddle
#
# Strategy: ball launches from center at ±30° max, so it returns near center.
# Keep the paddle oscillating tightly around center with short rapid taps.
# BALL_SPEED=350, PADDLE_SPEED=500, PADDLE_W=120 — 0.25s tap = 125px, just
# over one paddle width, keeping coverage of the center return zone.
WID="${1:-}"

# If WID not provided or invalid, retry finding the window by name for up to 5 seconds
if [ -z "$WID" ]; then
    for i in $(seq 1 10); do
        WID=$(xdotool search --onlyvisible --name "ffe_" 2>/dev/null | head -1)
        [ -n "$WID" ] && break
        sleep 0.5
    done
fi

# Focus the window early so the OS routes input to it
if [ -n "$WID" ]; then
    xdotool windowfocus --sync "$WID" 2>/dev/null || true
fi

# Wait for the game to fully load and show the title screen
sleep 2.5

# Activate window and press space to leave title screen ("PRESS SPACE TO START")
xdotool windowactivate --sync "$WID" 2>/dev/null || true
if [ -n "$WID" ]; then
    xdotool windowfocus --sync "$WID" 2>/dev/null || true
fi
xdotool key --window "$WID" space

# Wait for title → playing state transition (ballLaunched=false, "SPACE TO LAUNCH" shown)
sleep 0.8

# Activate window again and press space to launch the ball
xdotool windowactivate --sync "$WID" 2>/dev/null || true
if [ -n "$WID" ]; then
    xdotool windowfocus --sync "$WID" 2>/dev/null || true
fi
xdotool key --window "$WID" space

# Ball travels upward ~1.9s to reach bricks and return — hold still at center
# so the paddle is in the default intercept position for the first return.
sleep 0.5

# Rapid short alternating taps to keep the paddle oscillating near center.
# Each 0.25s tap at PADDLE_SPEED=500 moves the paddle ~125px — about one
# paddle width — so we stay in the center zone where the ball returns.
# 18 taps × ~0.4s each covers the remaining ~7s of the 12s recording window.

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.30
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.30
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.30
xdotool keyup --window "$WID" a
sleep 0.15

xdotool keydown --window "$WID" d
sleep 0.25
xdotool keyup --window "$WID" d
sleep 0.15

xdotool keydown --window "$WID" a
sleep 0.25
xdotool keyup --window "$WID" a
