#!/usr/bin/env bash
# Input script for showcase Level 3 — "The Summit" (open outdoor, elevated)
# Receives window ID as $1
#
# Level 3 layout reference:
#   Player spawns at (0, ~2.5, -11) on a wide spawn platform at z=-15.
#   Stepping stones descend toward the central arena at origin.
#   Central altar at (0, 0.9, 0). Final artifact above it at y=1.8.
#   Floating platforms in a ring: NE (10,3,-8), NW (-10,4,-8),
#   SE (12,2,6), SW (-11,5,7), far north (0,6,14).
#   Boss guardian patrols the central arena. Open sky — no walls overhead.
#   3s walk forward reaches the stepping stones; no clipping risk.
#
# Camera is mouse-look only (GLFW cursor capture); xdotool mouse moves
# don't reach a captured cursor, so all navigation uses WASD movement keys.
WID="${1:-}"

focus() {
    [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null || true
}

focus

# Wait for scene to load (terrain heightmap + floating platforms)
sleep 2

# Activate window so key events reach the GLFW window
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Walk forward toward the central altar along the stepping stones
xdotool keydown --window "$WID" w
sleep 3
xdotool keyup --window "$WID" w

# Press F (attack) for combat near the first stepping stones
xdotool key --window "$WID" f
sleep 0.3

# Strafe right to show the summit layout and NE floating platform
xdotool keydown --window "$WID" d
sleep 1.5
xdotool keyup --window "$WID" d

# Walk forward 2s toward the altar
xdotool keydown --window "$WID" w
sleep 2
xdotool keyup --window "$WID" w

# Press F again near the altar / boss guardian
xdotool key --window "$WID" f
sleep 0.3
