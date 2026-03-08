#!/usr/bin/env bash
# Input script for showcase exploration
# Receives window ID as $1
# Sends keyboard inputs to walk around (WASD only — camera is mouse-look only,
# controlled by GLFW cursor capture; xdotool mouse moves don't reach a captured
# cursor, so all navigation is done with WASD movement keys).
#
# Level 1 layout reference:
#   Player spawns at (0, 2.5, -12) facing north (yaw=180).
#   Central fountain at (0, 0, 0) — 12 units north of spawn.
#   Courtyard walls at ±20 east/west, ±20 north/south.
#   Corner pillars at (±15, ±15). Gem collectibles at (12,-12), (-12,-12), (0,8).
#   Torches on east/west walls at x=±19.5, y=3.
#   Walk forward ~4 s to reach the fountain centre, then strafe to see walls+torches.
WID="${1:-}"

focus() {
    [ -n "$WID" ] && xdotool windowfocus "$WID" 2>/dev/null || true
}

focus

# Wait for the scene to load (terrain + geometry takes a moment)
sleep 2

# Activate window so key events reach the GLFW window
xdotool windowactivate --sync "$WID" 2>/dev/null || true

# Walk forward into the courtyard — need ~4 s to reach the fountain at centre
xdotool keydown --window "$WID" w
sleep 4
xdotool keyup --window "$WID" w

# Strafe right to reveal east wall torches and corner pillar
xdotool keydown --window "$WID" d
sleep 2
xdotool keyup --window "$WID" d

# Walk forward a bit more, past the fountain toward north end
xdotool keydown --window "$WID" w
sleep 2
xdotool keyup --window "$WID" w

# Strafe left to show the west wall, torches, and corner pillars
xdotool keydown --window "$WID" a
sleep 3
xdotool keyup --window "$WID" a

# Jump to show 3D movement
xdotool key --window "$WID" space
sleep 0.3

# Attack (F key = attack, works even without captured mouse)
xdotool key --window "$WID" f
sleep 0.2
xdotool key --window "$WID" f
sleep 0.2

# Strafe right briefly to reposition
xdotool keydown --window "$WID" d
sleep 1
xdotool keyup --window "$WID" d

# Final forward push toward the north gate
xdotool keydown --window "$WID" w
sleep 1
xdotool keyup --window "$WID" w

# Attack once more near the gate area (to show combat)
xdotool key --window "$WID" f
sleep 0.2
