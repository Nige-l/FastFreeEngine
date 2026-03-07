#pragma once

#include "core/types.h"

// Forward declaration -- GLFW types do NOT appear in the public API
struct GLFWwindow;

namespace ffe {

// --- Key Codes ---------------------------------------------------------------
// Values match GLFW_KEY_* for zero-cost translation.
// Only keys that games commonly use are listed. Add more as needed.

inline constexpr i32 MAX_KEYS = 512;

enum class Key : i32 {
    // Letters
    A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72,
    I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80,
    Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
    Y = 89, Z = 90,

    // Numbers (top row)
    NUM_0 = 48, NUM_1 = 49, NUM_2 = 50, NUM_3 = 51, NUM_4 = 52,
    NUM_5 = 53, NUM_6 = 54, NUM_7 = 55, NUM_8 = 56, NUM_9 = 57,

    // Function keys
    F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295,
    F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,

    // Arrow keys
    RIGHT = 262, LEFT = 263, DOWN = 264, UP = 265,

    // Modifiers
    LEFT_SHIFT = 340, RIGHT_SHIFT = 344,
    LEFT_CTRL  = 341, RIGHT_CTRL  = 345,
    LEFT_ALT   = 342, RIGHT_ALT   = 346,

    // Common gameplay keys
    SPACE      = 32,
    ESCAPE     = 256,
    ENTER      = 257,
    TAB        = 258,
    BACKSPACE  = 259,

    // Punctuation / misc
    COMMA      = 44,
    PERIOD     = 46,
    SLASH      = 47,
    SEMICOLON  = 59,
    APOSTROPHE = 39,
    MINUS      = 45,
    EQUAL      = 61,
    LEFT_BRACKET  = 91,
    RIGHT_BRACKET = 93,
    BACKSLASH  = 92,
    GRAVE_ACCENT = 96,

    // Editing
    INSERT     = 260,
    DELETE_KEY  = 261,
    HOME       = 268,
    END        = 269,
    PAGE_UP    = 266,
    PAGE_DOWN  = 267,

    KEY_COUNT  = 512   // Upper bound for array sizing -- NOT a valid key
};

// --- Mouse -------------------------------------------------------------------

inline constexpr i32 MAX_MOUSE_BUTTONS = 5;

enum class MouseButton : i32 {
    LEFT   = 0,
    RIGHT  = 1,
    MIDDLE = 2,
    EXTRA1 = 3,
    EXTRA2 = 4
};

// --- Gamepad -----------------------------------------------------------------

inline constexpr i32 MAX_GAMEPADS = 4;
inline constexpr i32 MAX_GAMEPAD_BUTTONS = 15;
inline constexpr i32 MAX_GAMEPAD_AXES = 6;

enum class GamepadButton : i32 {
    A             = 0,
    B             = 1,
    X             = 2,
    Y             = 3,
    LEFT_BUMPER   = 4,
    RIGHT_BUMPER  = 5,
    BACK          = 6,
    START         = 7,
    GUIDE         = 8,
    LEFT_STICK    = 9,
    RIGHT_STICK   = 10,
    DPAD_UP       = 11,
    DPAD_DOWN     = 12,
    DPAD_LEFT     = 13,
    DPAD_RIGHT    = 14
};

enum class GamepadAxis : i32 {
    LEFT_X        = 0,
    LEFT_Y        = 1,
    RIGHT_X       = 2,
    RIGHT_Y       = 3,
    LEFT_TRIGGER  = 4,
    RIGHT_TRIGGER = 5
};

// --- Actions -----------------------------------------------------------------

inline constexpr i32 MAX_ACTIONS = 64;
inline constexpr i32 MAX_BINDINGS_PER_ACTION = 4;

// --- Input Event Forwarding --------------------------------------------------
// Allows the editor (ImGui) to receive GLFW events without ffe_core linking
// against imgui. The editor registers callback function pointers; FFE's GLFW
// callbacks invoke them before processing input internally.

using GlfwKeyCallbackFn         = void(*)(GLFWwindow*, int key, int scancode, int action, int mods);
using GlfwMouseButtonCallbackFn = void(*)(GLFWwindow*, int button, int action, int mods);
using GlfwCursorPosCallbackFn   = void(*)(GLFWwindow*, double x, double y);
using GlfwScrollCallbackFn      = void(*)(GLFWwindow*, double xoffset, double yoffset);

struct InputEventForwarder {
    GlfwKeyCallbackFn         keyCallback         = nullptr;
    GlfwMouseButtonCallbackFn mouseButtonCallback  = nullptr;
    GlfwCursorPosCallbackFn   cursorPosCallback    = nullptr;
    GlfwScrollCallbackFn      scrollCallback       = nullptr;
};

// Register an event forwarder. Call before initInput() or at any time —
// callbacks are invoked if the forwarder is non-null when a GLFW event fires.
void setInputEventForwarder(const InputEventForwarder& forwarder);

// --- Lifecycle ---------------------------------------------------------------

// Initialize the input system. Call after GLFW window creation.
// window may be nullptr (headless mode).
void initInput(GLFWwindow* window);

// Shutdown the input system. Call before GLFW window destruction.
void shutdownInput();

// Process pending events into current-frame state.
// Call once at the start of each tick, before any systems run.
void updateInput();

// --- Keyboard Queries --------------------------------------------------------

bool isKeyPressed(Key key);    // Down this frame, was up last frame
bool isKeyHeld(Key key);       // Down this frame, was down last frame
bool isKeyReleased(Key key);   // Up this frame, was down last frame
bool isKeyUp(Key key);         // Up this frame, was up last frame

// Modifier convenience (checks both left and right variants)
bool isShiftDown();
bool isCtrlDown();
bool isAltDown();

// --- Mouse Queries -----------------------------------------------------------

f64 mouseX();
f64 mouseY();
f64 mouseDeltaX();
f64 mouseDeltaY();
f64 scrollDeltaX();
f64 scrollDeltaY();

bool isMouseButtonPressed(MouseButton btn);
bool isMouseButtonHeld(MouseButton btn);
bool isMouseButtonReleased(MouseButton btn);
bool isMouseButtonUp(MouseButton btn);

void setCursorCaptured(bool captured);
bool isCursorCaptured();

// --- Gamepad Queries ---------------------------------------------------------

bool isGamepadConnected(i32 id);
bool isGamepadButtonPressed(i32 id, GamepadButton btn);
bool isGamepadButtonHeld(i32 id, GamepadButton btn);
bool isGamepadButtonReleased(i32 id, GamepadButton btn);
f32  getGamepadAxis(i32 id, GamepadAxis axis);
const char* getGamepadName(i32 id);

void setGamepadDeadzone(f32 deadzone);
f32  getGamepadDeadzone();

// --- Action Mapping ----------------------------------------------------------

// Register a named action. Returns action index, or -1 if MAX_ACTIONS exceeded.
i32 registerAction(const char* name);

// Bind a key or mouse button to an action. Returns false if MAX_BINDINGS exceeded
// or actionIndex is out of range.
bool bindActionKey(i32 actionIndex, Key key);
bool bindActionMouseButton(i32 actionIndex, MouseButton btn);

// Remove all bindings for an action.
void clearActionBindings(i32 actionIndex);

// Find action index by name. Returns -1 if not found. O(n) -- call at init, not per-frame.
i32 findAction(const char* name);

// Query action state (checks all bindings, returns true if ANY binding matches)
bool isActionPressed(i32 actionIndex);
bool isActionHeld(i32 actionIndex);
bool isActionReleased(i32 actionIndex);
bool isActionUp(i32 actionIndex);

// --- Test Hooks (test builds only) -------------------------------------------
#ifdef FFE_TEST
namespace test {
    void simulateKeyPress(Key key);
    void simulateKeyRelease(Key key);
    void simulateMouseButtonPress(MouseButton btn);
    void simulateMouseButtonRelease(MouseButton btn);
    void simulateMouseMove(f64 x, f64 y);
    void simulateScroll(f64 dx, f64 dy);
    void simulateGamepadButton(i32 id, GamepadButton btn, bool pressed);
    void simulateGamepadAxis(i32 id, GamepadAxis axis, f32 value);
    void simulateGamepadConnect(i32 id, bool connected);
} // namespace test
#endif

} // namespace ffe
