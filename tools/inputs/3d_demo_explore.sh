#!/usr/bin/env bash
# Input script for 3D demo exploration
# Orbit camera, cast ray, toggle features
WID="${1:-}"
[ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null || true

# Activate window so key events reach the GLFW window
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Wait for 3D demo to finish loading meshes, textures, shadows, SSAO, bloom
sleep 2.5

# Orbit camera left
xdotool keydown --window "$WID" a
sleep 2
xdotool keyup --window "$WID" a

# Zoom in
xdotool keydown --window "$WID" w
sleep 1
xdotool keyup --window "$WID" w

# Cast a ray
xdotool key --window "$WID" f
sleep 0.5

# Orbit right
xdotool keydown --window "$WID" d
sleep 2
xdotool keyup --window "$WID" d

# Elevation up
xdotool keydown --window "$WID" Up
sleep 1
xdotool keyup --window "$WID" Up

# Orbit left again
xdotool keydown --window "$WID" a
sleep 1.5
xdotool keyup --window "$WID" a
