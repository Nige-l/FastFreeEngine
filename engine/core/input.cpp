#include "core/input.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ffe {

// --- Internal state structs --------------------------------------------------

struct KeyboardState {
    bool current[MAX_KEYS];
    bool previous[MAX_KEYS];
    // Latched press/release flags — set true if ANY press (or release) event
    // arrived in the pending queue this tick. Prevents a quick tap (press+release
    // within a single glfwPollEvents() batch) from swallowing the press edge.
    // The last event still determines current[] (for "held" queries), but the
    // latched flags ensure isKeyPressed/Released always detect the edge.
    bool pressedThisTick[MAX_KEYS];
    bool releasedThisTick[MAX_KEYS];
};

struct MouseState {
    f64 x = 0.0;
    f64 y = 0.0;
    f64 deltaX = 0.0;
    f64 deltaY = 0.0;
    f64 prevX = 0.0;
    f64 prevY = 0.0;
    f64 scrollX = 0.0;
    f64 scrollY = 0.0;
    bool currentButtons[MAX_MOUSE_BUTTONS];
    bool previousButtons[MAX_MOUSE_BUTTONS];
    // Latched press/release flags — set true if ANY press (or release) event
    // arrived in the pending queue this tick. Prevents quick click-release
    // within a single glfwPollEvents() batch from swallowing the press edge.
    bool pressedThisTick[MAX_MOUSE_BUTTONS];
    bool releasedThisTick[MAX_MOUSE_BUTTONS];
    bool cursorCaptured = false;
    bool firstMouseInput = true;
    // Deferred cursor capture: set to true when setCursorCaptured(true) is called
    // but the window does not yet have focus (GLFW requires focus before CURSOR_DISABLED
    // takes effect on some platforms, e.g. X11/Wayland). updateInput() retries the
    // capture each tick until the window gains focus.
    bool pendingCapture = false;
};

struct InputBinding {
    enum class Type : u8 { NONE = 0, KEY, MOUSE_BUTTON };
    Type type = Type::NONE;
    i32  code = 0;
};

struct Action {
    char name[32];
    InputBinding bindings[MAX_BINDINGS_PER_ACTION];
    u8 bindingCount = 0;
};

struct ActionMap {
    Action actions[MAX_ACTIONS];
    i32 actionCount = 0;
};

// --- Pending event types -----------------------------------------------------

inline constexpr i32 MAX_PENDING_KEY_EVENTS = 64;
inline constexpr i32 MAX_PENDING_MOUSE_BUTTON_EVENTS = 16;

struct PendingKeyEvent {
    i16 keyCode;
    bool down;
};

struct PendingMouseButtonEvent {
    i8 button;
    bool down;
};

// --- File-scope globals ------------------------------------------------------

static GLFWwindow* g_window = nullptr;
static KeyboardState g_keyboard = {};
static MouseState g_mouse = {};
static ActionMap g_actionMap = {};

static PendingKeyEvent g_pendingKeyEvents[MAX_PENDING_KEY_EVENTS];
static i32 g_pendingKeyCount = 0;

static PendingMouseButtonEvent g_pendingMouseButtonEvents[MAX_PENDING_MOUSE_BUTTON_EVENTS];
static i32 g_pendingMouseButtonCount = 0;

static f64 g_pendingScrollX = 0.0;
static f64 g_pendingScrollY = 0.0;

static constexpr f64 MAX_SCROLL_ACCUMULATOR = 1000.0;

// --- Pending gamepad button events (for test hooks in headless mode) ---

inline constexpr i32 MAX_PENDING_GAMEPAD_BUTTON_EVENTS = 32;

struct PendingGamepadButtonEvent {
    i8 gamepadId;
    i8 button;
    bool down;
};

static PendingGamepadButtonEvent g_pendingGamepadButtonEvents[MAX_PENDING_GAMEPAD_BUTTON_EVENTS];
static i32 g_pendingGamepadButtonCount = 0;

// --- Gamepad state ---

struct GamepadState {
    bool connected = false;
    bool currentButtons[MAX_GAMEPAD_BUTTONS];
    bool previousButtons[MAX_GAMEPAD_BUTTONS];
    f32  axes[MAX_GAMEPAD_AXES];
    char name[64];
};

static GamepadState g_gamepads[MAX_GAMEPADS] = {};
static f32 g_gamepadDeadzone = 0.15f;

// --- Input event forwarder (set by editor to forward events to ImGui) --------

static InputEventForwarder g_eventForwarder = {};

void setInputEventForwarder(const InputEventForwarder& forwarder) {
    g_eventForwarder = forwarder;
}

// --- GLFW callbacks ----------------------------------------------------------

static void glfwKeyCallback(GLFWwindow* window, const int key, const int scancode,
                             const int action, const int mods) {
    // Forward to editor/ImGui if a forwarder is registered.
    if (g_eventForwarder.keyCallback != nullptr) {
        g_eventForwarder.keyCallback(window, key, scancode, action, mods);
    }

    if (key < 0 || key >= MAX_KEYS) return;
    if (action == GLFW_PRESS) {
        if (g_pendingKeyCount < MAX_PENDING_KEY_EVENTS) {
            g_pendingKeyEvents[g_pendingKeyCount++] = {static_cast<i16>(key), true};
        }
    } else if (action == GLFW_RELEASE) {
        if (g_pendingKeyCount < MAX_PENDING_KEY_EVENTS) {
            g_pendingKeyEvents[g_pendingKeyCount++] = {static_cast<i16>(key), false};
        }
    }
    // GLFW_REPEAT is ignored -- held state is derived from current vs previous
}

static void glfwMouseButtonCallback(GLFWwindow* window, const int button,
                                     const int action, const int mods) {
    if (g_eventForwarder.mouseButtonCallback != nullptr) {
        g_eventForwarder.mouseButtonCallback(window, button, action, mods);
    }

    if (button < 0 || button >= MAX_MOUSE_BUTTONS) return;
    if (action == GLFW_PRESS) {
        if (g_pendingMouseButtonCount < MAX_PENDING_MOUSE_BUTTON_EVENTS) {
            g_pendingMouseButtonEvents[g_pendingMouseButtonCount++] =
                {static_cast<i8>(button), true};
        }
    } else if (action == GLFW_RELEASE) {
        if (g_pendingMouseButtonCount < MAX_PENDING_MOUSE_BUTTON_EVENTS) {
            g_pendingMouseButtonEvents[g_pendingMouseButtonCount++] =
                {static_cast<i8>(button), false};
        }
    }
}

static void glfwCursorPosCallback(GLFWwindow* window, const double xpos, const double ypos) {
    if (g_eventForwarder.cursorPosCallback != nullptr) {
        g_eventForwarder.cursorPosCallback(window, xpos, ypos);
    }

    if (g_mouse.firstMouseInput) {
        g_mouse.prevX = xpos;
        g_mouse.prevY = ypos;
        g_mouse.firstMouseInput = false;
    }
    g_mouse.x = xpos;
    g_mouse.y = ypos;
}

static void glfwScrollCallback(GLFWwindow* window, const double xoffset, const double yoffset) {
    if (g_eventForwarder.scrollCallback != nullptr) {
        g_eventForwarder.scrollCallback(window, xoffset, yoffset);
    }

    if (!std::isfinite(xoffset) || !std::isfinite(yoffset)) return;

    g_pendingScrollX += xoffset;
    g_pendingScrollY += yoffset;

    g_pendingScrollX = std::clamp(g_pendingScrollX, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
    g_pendingScrollY = std::clamp(g_pendingScrollY, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
}

// --- Lifecycle ---------------------------------------------------------------

void initInput(GLFWwindow* window) {
    std::memset(&g_keyboard, 0, sizeof(g_keyboard));
    g_mouse = MouseState{};
    g_actionMap = ActionMap{};
    std::memset(g_pendingKeyEvents, 0, sizeof(g_pendingKeyEvents));
    std::memset(g_pendingMouseButtonEvents, 0, sizeof(g_pendingMouseButtonEvents));
    g_pendingKeyCount = 0;
    g_pendingMouseButtonCount = 0;
    g_pendingScrollX = 0.0;
    g_pendingScrollY = 0.0;
    g_mouse.firstMouseInput = true;
    for (auto& gp : g_gamepads) { gp = GamepadState{}; }
    g_gamepadDeadzone = 0.15f;
    std::memset(g_pendingGamepadButtonEvents, 0, sizeof(g_pendingGamepadButtonEvents));
    g_pendingGamepadButtonCount = 0;

    g_window = window;
    if (window != nullptr) {
        glfwSetKeyCallback(window, glfwKeyCallback);
        glfwSetMouseButtonCallback(window, glfwMouseButtonCallback);
        glfwSetCursorPosCallback(window, glfwCursorPosCallback);
        glfwSetScrollCallback(window, glfwScrollCallback);
    }
}

void shutdownInput() {
    // Clear the event forwarder to avoid dangling calls after shutdown.
    g_eventForwarder = {};

    if (g_window != nullptr) {
        glfwSetKeyCallback(g_window, nullptr);
        glfwSetMouseButtonCallback(g_window, nullptr);
        glfwSetCursorPosCallback(g_window, nullptr);
        glfwSetScrollCallback(g_window, nullptr);
    }
    g_window = nullptr;
    std::memset(&g_keyboard, 0, sizeof(g_keyboard));
    g_mouse = MouseState{};
    g_actionMap = ActionMap{};
    for (auto& gp : g_gamepads) { gp = GamepadState{}; }
    g_gamepadDeadzone = 0.15f;
    g_pendingKeyCount = 0;
    g_pendingMouseButtonCount = 0;
    g_pendingScrollX = 0.0;
    g_pendingScrollY = 0.0;
    g_pendingGamepadButtonCount = 0;
}

void updateInput() {
    // 1. Keyboard: copy current -> previous, clear latched flags
    std::memcpy(g_keyboard.previous, g_keyboard.current, sizeof(g_keyboard.current));
    std::memset(g_keyboard.pressedThisTick,  0, sizeof(g_keyboard.pressedThisTick));
    std::memset(g_keyboard.releasedThisTick, 0, sizeof(g_keyboard.releasedThisTick));

    // 2. Process pending key events.
    // Latch press/release flags so that a quick tap (press+release within a
    // single glfwPollEvents() batch) does not swallow the press edge. The last
    // event still determines current[] (for "held" queries), but the latched
    // flags ensure isKeyPressed/Released always detect the edge.
    for (i32 i = 0; i < g_pendingKeyCount; ++i) {
        const auto& event = g_pendingKeyEvents[i];
        if (event.down) {
            g_keyboard.pressedThisTick[event.keyCode] = true;
        } else {
            g_keyboard.releasedThisTick[event.keyCode] = true;
        }
        g_keyboard.current[event.keyCode] = event.down;
    }
    g_pendingKeyCount = 0;

    // 3. Mouse buttons: copy current -> previous, clear latched flags
    std::memcpy(g_mouse.previousButtons, g_mouse.currentButtons, sizeof(g_mouse.currentButtons));
    std::memset(g_mouse.pressedThisTick,  0, sizeof(g_mouse.pressedThisTick));
    std::memset(g_mouse.releasedThisTick, 0, sizeof(g_mouse.releasedThisTick));

    // 4. Process pending mouse button events.
    // Latch press/release flags so that a quick click-release within a single
    // glfwPollEvents() batch does not swallow the press edge. The last event
    // still determines currentButtons (for "held" queries), but the latched
    // flags ensure isMouseButtonPressed/Released detect the edge.
    for (i32 i = 0; i < g_pendingMouseButtonCount; ++i) {
        const auto& event = g_pendingMouseButtonEvents[i];
        if (event.down) {
            g_mouse.pressedThisTick[event.button] = true;
        } else {
            g_mouse.releasedThisTick[event.button] = true;
        }
        g_mouse.currentButtons[event.button] = event.down;
    }
    g_pendingMouseButtonCount = 0;

    // 5. Mouse position delta
    g_mouse.deltaX = g_mouse.x - g_mouse.prevX;
    g_mouse.deltaY = g_mouse.y - g_mouse.prevY;
    g_mouse.prevX  = g_mouse.x;
    g_mouse.prevY  = g_mouse.y;

    // 5b. Deferred cursor capture retry: if setCursorCaptured(true) was called
    // before the window had focus (e.g. on X11/Wayland), apply the mode now
    // that the window may have gained focus.
    if (g_mouse.pendingCapture && g_window != nullptr
        && glfwGetWindowAttrib(g_window, GLFW_FOCUSED)) {
        glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        g_mouse.pendingCapture = false;
        g_mouse.firstMouseInput = true; // suppress cursor-warp jump
    }

    // 6. Scroll delta
    g_mouse.scrollX = g_pendingScrollX;
    g_mouse.scrollY = g_pendingScrollY;
    g_pendingScrollX = 0.0;
    g_pendingScrollY = 0.0;

    // 7. Gamepads: copy current -> previous, then poll GLFW
    for (i32 i = 0; i < MAX_GAMEPADS; ++i) {
        auto& gp = g_gamepads[i];
        std::memcpy(gp.previousButtons, gp.currentButtons, sizeof(gp.currentButtons));

        if (g_window != nullptr) {
            const bool wasConnected = gp.connected;
            // GLFW joystick IDs map directly to our gamepad IDs (0..3)
            if (glfwJoystickIsGamepad(i)) {
                GLFWgamepadstate state;
                if (glfwGetGamepadState(i, &state)) {
                    gp.connected = true;

                    // Copy button states
                    for (i32 b = 0; b < MAX_GAMEPAD_BUTTONS; ++b) {
                        gp.currentButtons[b] = (state.buttons[b] == GLFW_PRESS);
                    }

                    // Copy axis values with deadzone
                    for (i32 a = 0; a < MAX_GAMEPAD_AXES; ++a) {
                        f32 val = state.axes[a];
                        // Triggers (axes 4 and 5) go from -1 to 1 in GLFW,
                        // but we want 0..1 for triggers
                        if (a == static_cast<i32>(GamepadAxis::LEFT_TRIGGER) ||
                            a == static_cast<i32>(GamepadAxis::RIGHT_TRIGGER)) {
                            val = (val + 1.0f) * 0.5f; // remap -1..1 to 0..1
                            // Apply deadzone for triggers (only zero below deadzone)
                            if (val < g_gamepadDeadzone) {
                                val = 0.0f;
                            }
                        } else {
                            // Stick axes: apply deadzone symmetrically
                            if (val > -g_gamepadDeadzone && val < g_gamepadDeadzone) {
                                val = 0.0f;
                            }
                        }
                        // Clamp
                        if (a == static_cast<i32>(GamepadAxis::LEFT_TRIGGER) ||
                            a == static_cast<i32>(GamepadAxis::RIGHT_TRIGGER)) {
                            val = std::clamp(val, 0.0f, 1.0f);
                        } else {
                            val = std::clamp(val, -1.0f, 1.0f);
                        }
                        gp.axes[a] = val;
                    }

                    // Store name only on connection transition to avoid snprintf every tick.
                    if (!wasConnected) {
                        const char* gpName = glfwGetGamepadName(i);
                        if (gpName != nullptr) {
                            std::snprintf(gp.name, sizeof(gp.name), "%s", gpName);
                        }
                    }
                } else {
                    gp.connected = false;
                    std::memset(gp.currentButtons, 0, sizeof(gp.currentButtons));
                    std::memset(gp.axes, 0, sizeof(gp.axes));
                }
            } else {
                gp.connected = false;
                std::memset(gp.currentButtons, 0, sizeof(gp.currentButtons));
                std::memset(gp.axes, 0, sizeof(gp.axes));
            }
        }
        // In headless mode (g_window == nullptr), gamepad state is only changed
        // via test hooks — no polling needed.
    }

    // 8. Apply pending gamepad button events (from test hooks)
    for (i32 i = 0; i < g_pendingGamepadButtonCount; ++i) {
        const auto& event = g_pendingGamepadButtonEvents[i];
        g_gamepads[event.gamepadId].currentButtons[event.button] = event.down;
    }
    g_pendingGamepadButtonCount = 0;
}

// --- Keyboard queries --------------------------------------------------------

bool isKeyPressed(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    // Use latched flag: true if a press event arrived this tick AND the key was
    // not already down last tick. This handles the case where press+release both
    // arrive in the same glfwPollEvents() batch — without the latch, current[k]
    // would be false and the press edge would be silently lost.
    return g_keyboard.pressedThisTick[k] && !g_keyboard.previous[k];
}

bool isKeyHeld(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return g_keyboard.current[k] && g_keyboard.previous[k];
}

bool isKeyReleased(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    // Use latched flag: true if a release event arrived this tick AND the key
    // was down last tick. Handles quick tap (press+release in one batch).
    return g_keyboard.releasedThisTick[k] && g_keyboard.previous[k];
}

bool isKeyUp(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return !g_keyboard.current[k] && !g_keyboard.previous[k];
}

bool isShiftDown() {
    return g_keyboard.current[static_cast<i32>(Key::LEFT_SHIFT)]
        || g_keyboard.current[static_cast<i32>(Key::RIGHT_SHIFT)];
}

bool isCtrlDown() {
    return g_keyboard.current[static_cast<i32>(Key::LEFT_CTRL)]
        || g_keyboard.current[static_cast<i32>(Key::RIGHT_CTRL)];
}

bool isAltDown() {
    return g_keyboard.current[static_cast<i32>(Key::LEFT_ALT)]
        || g_keyboard.current[static_cast<i32>(Key::RIGHT_ALT)];
}

// --- Mouse queries -----------------------------------------------------------

f64 mouseX()      { return g_mouse.x; }
f64 mouseY()      { return g_mouse.y; }
f64 mouseDeltaX() { return g_mouse.deltaX; }
f64 mouseDeltaY() { return g_mouse.deltaY; }
f64 scrollDeltaX() { return g_mouse.scrollX; }
f64 scrollDeltaY() { return g_mouse.scrollY; }

bool isMouseButtonPressed(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    // Use latched flag: true if a press event arrived this tick, AND the
    // button was not already down last tick (avoids re-triggering on hold).
    // The latch handles the case where both press+release arrive in one
    // glfwPollEvents() batch — without it, currentButtons ends up false
    // and the press edge is silently lost.
    return g_mouse.pressedThisTick[b] && !g_mouse.previousButtons[b];
}

bool isMouseButtonHeld(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return g_mouse.currentButtons[b] && g_mouse.previousButtons[b];
}

bool isMouseButtonReleased(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    // Use latched flag: true if a release event arrived this tick, AND the
    // button was down last tick. Handles quick click-release in one batch.
    return g_mouse.releasedThisTick[b] && g_mouse.previousButtons[b];
}

bool isMouseButtonUp(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return !g_mouse.currentButtons[b] && !g_mouse.previousButtons[b];
}

void setCursorCaptured(const bool captured) {
    g_mouse.cursorCaptured = captured;
    if (g_window == nullptr) return;

    if (captured) {
        // Request focus first. On X11/Wayland the focus grant may be
        // asynchronous, so we also set pendingCapture so that updateInput()
        // retries every tick until glfwGetWindowAttrib confirms GLFW_FOCUSED.
        glfwFocusWindow(g_window);
        if (glfwGetWindowAttrib(g_window, GLFW_FOCUSED)) {
            glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported()) {
                glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
            g_mouse.pendingCapture = false;
            g_mouse.firstMouseInput = true; // suppress jump after re-capture
        } else {
            g_mouse.pendingCapture = true;
        }
    } else {
        g_mouse.pendingCapture = false;
        glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
}

bool isCursorCaptured() { return g_mouse.cursorCaptured; }

// --- Gamepad queries ---------------------------------------------------------

bool isGamepadConnected(const i32 id) {
    if (id < 0 || id >= MAX_GAMEPADS) return false;
    return g_gamepads[id].connected;
}

bool isGamepadButtonPressed(const i32 id, const GamepadButton btn) {
    if (id < 0 || id >= MAX_GAMEPADS) return false;
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_GAMEPAD_BUTTONS) return false;
    return g_gamepads[id].currentButtons[b] && !g_gamepads[id].previousButtons[b];
}

bool isGamepadButtonHeld(const i32 id, const GamepadButton btn) {
    if (id < 0 || id >= MAX_GAMEPADS) return false;
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_GAMEPAD_BUTTONS) return false;
    return g_gamepads[id].currentButtons[b] && g_gamepads[id].previousButtons[b];
}

bool isGamepadButtonReleased(const i32 id, const GamepadButton btn) {
    if (id < 0 || id >= MAX_GAMEPADS) return false;
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_GAMEPAD_BUTTONS) return false;
    return !g_gamepads[id].currentButtons[b] && g_gamepads[id].previousButtons[b];
}

f32 getGamepadAxis(const i32 id, const GamepadAxis axis) {
    if (id < 0 || id >= MAX_GAMEPADS) return 0.0f;
    if (!g_gamepads[id].connected) return 0.0f;
    const i32 a = static_cast<i32>(axis);
    if (a < 0 || a >= MAX_GAMEPAD_AXES) return 0.0f;
    return g_gamepads[id].axes[a];
}

const char* getGamepadName(const i32 id) {
    if (id < 0 || id >= MAX_GAMEPADS) return "";
    if (!g_gamepads[id].connected) return "";
    return g_gamepads[id].name;
}

void setGamepadDeadzone(const f32 deadzone) {
    g_gamepadDeadzone = std::clamp(deadzone, 0.0f, 1.0f);
}

f32 getGamepadDeadzone() {
    return g_gamepadDeadzone;
}

// --- Action mapping ----------------------------------------------------------

i32 registerAction(const char* name) {
    if (g_actionMap.actionCount >= MAX_ACTIONS) return -1;
    const i32 idx = g_actionMap.actionCount++;
    Action& action = g_actionMap.actions[idx];
    std::snprintf(action.name, sizeof(action.name), "%s", name);
    action.bindingCount = 0;
    for (auto& b : action.bindings) {
        b.type = InputBinding::Type::NONE;
        b.code = 0;
    }
    return idx;
}

bool bindActionKey(const i32 actionIndex, const Key key) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    Action& action = g_actionMap.actions[actionIndex];
    if (action.bindingCount >= MAX_BINDINGS_PER_ACTION) return false;
    auto& b = action.bindings[action.bindingCount++];
    b.type = InputBinding::Type::KEY;
    b.code = static_cast<i32>(key);
    return true;
}

bool bindActionMouseButton(const i32 actionIndex, const MouseButton btn) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    Action& action = g_actionMap.actions[actionIndex];
    if (action.bindingCount >= MAX_BINDINGS_PER_ACTION) return false;
    auto& b = action.bindings[action.bindingCount++];
    b.type = InputBinding::Type::MOUSE_BUTTON;
    b.code = static_cast<i32>(btn);
    return true;
}

void clearActionBindings(const i32 actionIndex) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return;
    Action& action = g_actionMap.actions[actionIndex];
    action.bindingCount = 0;
    for (auto& b : action.bindings) {
        b.type = InputBinding::Type::NONE;
        b.code = 0;
    }
}

i32 findAction(const char* name) {
    for (i32 i = 0; i < g_actionMap.actionCount; ++i) {
        if (std::strcmp(g_actionMap.actions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool isActionPressed(const i32 actionIndex) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    const Action& action = g_actionMap.actions[actionIndex];
    for (i32 i = 0; i < action.bindingCount; ++i) {
        const auto& b = action.bindings[i];
        if (b.type == InputBinding::Type::KEY
            && isKeyPressed(static_cast<Key>(b.code))) return true;
        if (b.type == InputBinding::Type::MOUSE_BUTTON
            && isMouseButtonPressed(static_cast<MouseButton>(b.code))) return true;
    }
    return false;
}

bool isActionHeld(const i32 actionIndex) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    const Action& action = g_actionMap.actions[actionIndex];
    for (i32 i = 0; i < action.bindingCount; ++i) {
        const auto& b = action.bindings[i];
        if (b.type == InputBinding::Type::KEY
            && isKeyHeld(static_cast<Key>(b.code))) return true;
        if (b.type == InputBinding::Type::MOUSE_BUTTON
            && isMouseButtonHeld(static_cast<MouseButton>(b.code))) return true;
    }
    return false;
}

bool isActionReleased(const i32 actionIndex) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    const Action& action = g_actionMap.actions[actionIndex];
    for (i32 i = 0; i < action.bindingCount; ++i) {
        const auto& b = action.bindings[i];
        if (b.type == InputBinding::Type::KEY
            && isKeyReleased(static_cast<Key>(b.code))) return true;
        if (b.type == InputBinding::Type::MOUSE_BUTTON
            && isMouseButtonReleased(static_cast<MouseButton>(b.code))) return true;
    }
    return false;
}

bool isActionUp(const i32 actionIndex) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    const Action& action = g_actionMap.actions[actionIndex];
    // An action is "up" only if ALL bindings are up
    for (i32 i = 0; i < action.bindingCount; ++i) {
        const auto& b = action.bindings[i];
        if (b.type == InputBinding::Type::KEY
            && !isKeyUp(static_cast<Key>(b.code))) return false;
        if (b.type == InputBinding::Type::MOUSE_BUTTON
            && !isMouseButtonUp(static_cast<MouseButton>(b.code))) return false;
    }
    return true;
}

// --- Test hooks --------------------------------------------------------------

#ifdef FFE_TEST

namespace test {

void simulateKeyPress(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return;
    if (g_pendingKeyCount < MAX_PENDING_KEY_EVENTS) {
        g_pendingKeyEvents[g_pendingKeyCount++] = {static_cast<i16>(k), true};
    }
}

void simulateKeyRelease(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return;
    if (g_pendingKeyCount < MAX_PENDING_KEY_EVENTS) {
        g_pendingKeyEvents[g_pendingKeyCount++] = {static_cast<i16>(k), false};
    }
}

void simulateKeyEvent(const Key key, const int glfwAction) {
    // glfwAction: GLFW_PRESS (1) enqueues a press; GLFW_RELEASE (0) enqueues a release.
    // GLFW_REPEAT is ignored, matching the behaviour of the real GLFW callback.
    if (glfwAction == GLFW_PRESS) {
        simulateKeyPress(key);
    } else if (glfwAction == GLFW_RELEASE) {
        simulateKeyRelease(key);
    }
}

void simulateMouseButtonPress(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return;
    if (g_pendingMouseButtonCount < MAX_PENDING_MOUSE_BUTTON_EVENTS) {
        g_pendingMouseButtonEvents[g_pendingMouseButtonCount++] =
            {static_cast<i8>(b), true};
    }
}

void simulateMouseButtonRelease(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return;
    if (g_pendingMouseButtonCount < MAX_PENDING_MOUSE_BUTTON_EVENTS) {
        g_pendingMouseButtonEvents[g_pendingMouseButtonCount++] =
            {static_cast<i8>(b), false};
    }
}

void simulateMouseMove(const f64 x, const f64 y) {
    if (g_mouse.firstMouseInput) {
        g_mouse.prevX = x;
        g_mouse.prevY = y;
        g_mouse.firstMouseInput = false;
    }
    g_mouse.x = x;
    g_mouse.y = y;
}

void simulateScroll(const f64 dx, const f64 dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    g_pendingScrollX += dx;
    g_pendingScrollY += dy;
    g_pendingScrollX = std::clamp(g_pendingScrollX, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
    g_pendingScrollY = std::clamp(g_pendingScrollY, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
}

void simulateGamepadButton(const i32 id, const GamepadButton btn, const bool pressed) {
    if (id < 0 || id >= MAX_GAMEPADS) return;
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_GAMEPAD_BUTTONS) return;
    if (g_pendingGamepadButtonCount < MAX_PENDING_GAMEPAD_BUTTON_EVENTS) {
        g_pendingGamepadButtonEvents[g_pendingGamepadButtonCount++] =
            {static_cast<i8>(id), static_cast<i8>(b), pressed};
    }
}

void simulateGamepadAxis(const i32 id, const GamepadAxis axis, const f32 value) {
    if (id < 0 || id >= MAX_GAMEPADS) return;
    const i32 a = static_cast<i32>(axis);
    if (a < 0 || a >= MAX_GAMEPAD_AXES) return;
    g_gamepads[id].axes[a] = value;
}

void simulateGamepadConnect(const i32 id, const bool connected) {
    if (id < 0 || id >= MAX_GAMEPADS) return;
    g_gamepads[id].connected = connected;
    if (!connected) {
        std::memset(g_gamepads[id].currentButtons, 0, sizeof(g_gamepads[id].currentButtons));
        std::memset(g_gamepads[id].axes, 0, sizeof(g_gamepads[id].axes));
        g_gamepads[id].name[0] = '\0';
    }
}

void simulateWindowFocus(const bool focused) {
    if (focused && g_mouse.pendingCapture) {
        // In headless mode (no GLFW window) we directly apply the pending
        // capture so tests can exercise the deferred-focus path.
        g_mouse.pendingCapture = false;
        g_mouse.firstMouseInput = true;
        // cursorCaptured was already set true by setCursorCaptured().
    } else if (!focused) {
        // Nothing to do: pendingCapture is only cleared when focus arrives.
    }
}

} // namespace test

#endif // FFE_TEST

} // namespace ffe
