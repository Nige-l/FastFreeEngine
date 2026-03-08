#!/usr/bin/env bash
# Input script for 3D Feature Test demo -- GIF recording
# Shows auto-orbit + feature toggles over ~15 seconds
WID="${1:-}"
[ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null || true

# Wait for scene to load and cubes to fall
sleep 2

# Activate window so key events reach the GLFW window
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Let auto-orbit run, showing all zones (~5s)
sleep 5

# Toggle shadows off then back on
xdotool key --window "$WID" 1
sleep 1
xdotool key --window "$WID" 1
sleep 0.5

# Toggle bloom off then back on
xdotool key --window "$WID" 2
sleep 1
xdotool key --window "$WID" 2
sleep 0.5

# Toggle SSAO off then back on
xdotool key --window "$WID" 3
sleep 1
xdotool key --window "$WID" 3
sleep 0.5

# Toggle fog off then back on
xdotool key --window "$WID" 4
sleep 1
xdotool key --window "$WID" 4
sleep 0.5

# Fire a raycast
xdotool key --window "$WID" f
sleep 1

# Let auto-orbit continue for final shot
sleep 1
