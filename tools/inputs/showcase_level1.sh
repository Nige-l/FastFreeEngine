#!/usr/bin/env bash
# Input script for showcase Level 1 — "The Courtyard" (open outdoor)
# Receives window ID as $1
#
# Level 1 layout reference:
#   Player spawns at (0, 2.5, -12) facing north (yaw=180).
#   Central fountain at (0, 0, 0) — 12 units north of spawn.
#   Courtyard walls at ±20 east/west, ±20 north/south.
#   Corner pillars at (±15, ±15). Gem collectibles at (12,-12), (-12,-12), (0,8).
#   Torches on east/west walls at x=±19.5, y=3.
#   Walk forward ~4s to reach the fountain centre, then strafe to see walls+torches.
#
# Camera is mouse-look only (GLFW cursor capture); xdotool mouse moves
# don't reach a captured cursor, so all navigation uses WASD movement keys.
WID="${1:-}"

focus() {
    [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null || true
}

focus

# Wait for scene to load (terrain + geometry)
sleep 2

# Activate window so key events reach the GLFW window
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Walk forward into the courtyard — ~4s to reach the fountain at centre
xdotool keydown --window "$WID" w
sleep 4
xdotool keyup --window "$WID" w

# Strafe right to reveal east wall torches and corner pillar
xdotool keydown --window "$WID" d
sleep 1.5
xdotool keyup --window "$WID" d

# Press F (attack) to show combat near east side
xdotool key --window "$WID" f
sleep 0.3

# Walk forward more toward the north gate
xdotool keydown --window "$WID" w
sleep 2
xdotool keyup --window "$WID" w

# Press F again near the gate area
xdotool key --window "$WID" f
sleep 0.3

# Jump to show 3D movement
xdotool key --window "$WID" space
sleep 0.4
