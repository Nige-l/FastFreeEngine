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
    bool cursorCaptured = false;
    bool firstMouseInput = true;
};

struct InputBinding {
    enum class Type : u8 { NONE = 0, KEY, MOUSE_BUTTON };
    Type type = Type::NONE;
    i16  code = 0;
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

// --- GLFW callbacks ----------------------------------------------------------

static void glfwKeyCallback(GLFWwindow* /*window*/, const int key, const int /*scancode*/,
                             const int action, const int /*mods*/) {
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

static void glfwMouseButtonCallback(GLFWwindow* /*window*/, const int button,
                                     const int action, const int /*mods*/) {
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

static void glfwCursorPosCallback(GLFWwindow* /*window*/, const double xpos, const double ypos) {
    if (g_mouse.firstMouseInput) {
        g_mouse.prevX = xpos;
        g_mouse.prevY = ypos;
        g_mouse.firstMouseInput = false;
    }
    g_mouse.x = xpos;
    g_mouse.y = ypos;
}

static void glfwScrollCallback(GLFWwindow* /*window*/, const double xoffset, const double yoffset) {
    if (!std::isfinite(xoffset) || !std::isfinite(yoffset)) return;

    g_pendingScrollX += xoffset;
    g_pendingScrollY += yoffset;

    g_pendingScrollX = std::clamp(g_pendingScrollX, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
    g_pendingScrollY = std::clamp(g_pendingScrollY, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
}

// --- Lifecycle ---------------------------------------------------------------

void initInput(GLFWwindow* window) {
    std::memset(&g_keyboard, 0, sizeof(g_keyboard));
    std::memset(&g_mouse, 0, sizeof(g_mouse));
    std::memset(&g_actionMap, 0, sizeof(g_actionMap));
    std::memset(g_pendingKeyEvents, 0, sizeof(g_pendingKeyEvents));
    std::memset(g_pendingMouseButtonEvents, 0, sizeof(g_pendingMouseButtonEvents));
    g_pendingKeyCount = 0;
    g_pendingMouseButtonCount = 0;
    g_pendingScrollX = 0.0;
    g_pendingScrollY = 0.0;
    g_mouse.firstMouseInput = true;

    g_window = window;
    if (window != nullptr) {
        glfwSetKeyCallback(window, glfwKeyCallback);
        glfwSetMouseButtonCallback(window, glfwMouseButtonCallback);
        glfwSetCursorPosCallback(window, glfwCursorPosCallback);
        glfwSetScrollCallback(window, glfwScrollCallback);
    }
}

void shutdownInput() {
    if (g_window != nullptr) {
        glfwSetKeyCallback(g_window, nullptr);
        glfwSetMouseButtonCallback(g_window, nullptr);
        glfwSetCursorPosCallback(g_window, nullptr);
        glfwSetScrollCallback(g_window, nullptr);
    }
    g_window = nullptr;
    std::memset(&g_keyboard, 0, sizeof(g_keyboard));
    std::memset(&g_mouse, 0, sizeof(g_mouse));
    std::memset(&g_actionMap, 0, sizeof(g_actionMap));
    g_pendingKeyCount = 0;
    g_pendingMouseButtonCount = 0;
    g_pendingScrollX = 0.0;
    g_pendingScrollY = 0.0;
}

void updateInput() {
    // 1. Keyboard: copy current -> previous
    std::memcpy(g_keyboard.previous, g_keyboard.current, sizeof(g_keyboard.current));

    // 2. Process pending key events
    for (i32 i = 0; i < g_pendingKeyCount; ++i) {
        const auto& event = g_pendingKeyEvents[i];
        g_keyboard.current[event.keyCode] = event.down;
    }
    g_pendingKeyCount = 0;

    // 3. Mouse buttons: copy current -> previous
    std::memcpy(g_mouse.previousButtons, g_mouse.currentButtons, MAX_MOUSE_BUTTONS);

    // 4. Process pending mouse button events
    for (i32 i = 0; i < g_pendingMouseButtonCount; ++i) {
        const auto& event = g_pendingMouseButtonEvents[i];
        g_mouse.currentButtons[event.button] = event.down;
    }
    g_pendingMouseButtonCount = 0;

    // 5. Mouse position delta
    g_mouse.deltaX = g_mouse.x - g_mouse.prevX;
    g_mouse.deltaY = g_mouse.y - g_mouse.prevY;
    g_mouse.prevX  = g_mouse.x;
    g_mouse.prevY  = g_mouse.y;

    // 6. Scroll delta
    g_mouse.scrollX = g_pendingScrollX;
    g_mouse.scrollY = g_pendingScrollY;
    g_pendingScrollX = 0.0;
    g_pendingScrollY = 0.0;
}

// --- Keyboard queries --------------------------------------------------------

bool isKeyPressed(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return g_keyboard.current[k] && !g_keyboard.previous[k];
}

bool isKeyHeld(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return g_keyboard.current[k] && g_keyboard.previous[k];
}

bool isKeyReleased(const Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return !g_keyboard.current[k] && g_keyboard.previous[k];
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
    return g_mouse.currentButtons[b] && !g_mouse.previousButtons[b];
}

bool isMouseButtonHeld(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return g_mouse.currentButtons[b] && g_mouse.previousButtons[b];
}

bool isMouseButtonReleased(const MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return !g_mouse.currentButtons[b] && g_mouse.previousButtons[b];
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
        glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    } else {
        glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
}

bool isCursorCaptured() { return g_mouse.cursorCaptured; }

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
    b.code = static_cast<i16>(static_cast<i32>(key));
    return true;
}

bool bindActionMouseButton(const i32 actionIndex, const MouseButton btn) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    Action& action = g_actionMap.actions[actionIndex];
    if (action.bindingCount >= MAX_BINDINGS_PER_ACTION) return false;
    auto& b = action.bindings[action.bindingCount++];
    b.type = InputBinding::Type::MOUSE_BUTTON;
    b.code = static_cast<i16>(static_cast<i32>(btn));
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

} // namespace test

#endif // FFE_TEST

} // namespace ffe
