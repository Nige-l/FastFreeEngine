#!/usr/bin/env bash
# Input script for Pong — start game and move paddles
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

# Wait for the game to fully load and show the menu
sleep 2.5

# Activate window so it receives key events
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Start the game (press space to begin — transitions from menu to playing state)
if [ -n "$WID" ]; then
    xdotool windowfocus --sync "$WID" 2>/dev/null || true
fi
xdotool key --window "$WID" space

# Give the game a moment to transition from menu to playing state (ballLaunched=false)
sleep 1.0

# Activate window again before the second space press (belt-and-suspenders)
xdotool windowactivate --sync "$WID" 2>/dev/null || true
if [ -n "$WID" ]; then
    xdotool windowfocus --sync "$WID" 2>/dev/null || true
fi

# Serve the ball (second and final space press — "SPACE TO SERVE" prompt)
xdotool key --window "$WID" space

# Brief pause before paddle movement begins
sleep 0.3

# Move left paddle up
xdotool keydown --window "$WID" w
sleep 1.0
xdotool keyup --window "$WID" w

# Move right paddle down
xdotool keydown --window "$WID" Down
sleep 0.8
xdotool keyup --window "$WID" Down

# Move left paddle down
xdotool keydown --window "$WID" s
sleep 1.0
xdotool keyup --window "$WID" s

# Move right paddle up
xdotool keydown --window "$WID" Up
sleep 0.8
xdotool keyup --window "$WID" Up

# Move left paddle up again (keep the rally going)
xdotool keydown --window "$WID" w
sleep 1.0
xdotool keyup --window "$WID" w

# Right paddle follows ball down
xdotool keydown --window "$WID" Down
sleep 0.7
xdotool keyup --window "$WID" Down

# Left paddle tracks down
xdotool keydown --window "$WID" s
sleep 0.8
xdotool keyup --window "$WID" s

# Right paddle up to return
xdotool keydown --window "$WID" Up
sleep 0.9
xdotool keyup --window "$WID" Up

# Final left paddle up
xdotool keydown --window "$WID" w
sleep 0.8
xdotool keyup --window "$WID" w
