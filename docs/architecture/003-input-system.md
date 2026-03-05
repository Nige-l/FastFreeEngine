# ADR-003: Input System

**Revision 1** — addresses security-auditor findings from shift-left review.

**Status:** APPROVED
**Author:** architect
**Date:** 2026-03-05
**Tiers:** ALL (RETRO, LEGACY, STANDARD, MODERN)

This document defines the input system for FastFreeEngine. Input is core infrastructure — it is not tied to any GPU tier and works identically on all hardware. engine-dev should be able to implement the entire input system from this document alone without asking clarifying questions.

ADR-001 defined the core skeleton, main loop, ECS, and the fixed-rate `tick(dt)` loop. This ADR defines how user input gets from GLFW into that loop in a clean, frame-coherent, cache-friendly way.

---

## 1. Design Philosophy

### 1.1 Input Is Core Infrastructure

Input lives in `engine/core/`, not in the renderer. It depends on GLFW for raw events (GLFW is already a dependency for window creation), but the input system's public API has zero GLFW types in it. Game code, gameplay systems, and Lua scripts never call GLFW directly. They query FFE's input state.

This is a hard boundary. GLFW is an implementation detail hidden behind `input.cpp`. If we ever replace GLFW with SDL or a platform-native layer, only `input.cpp` changes. Nothing else in the engine notices.

### 1.2 State-Based, Not Event-Based

Game code does not register callbacks. Game code does not process event queues. Game code asks "is the jump button pressed this frame?" and gets a `bool`. This is the correct model for gameplay because:

- It is deterministic: every system in a single tick sees the same answer.
- It is simple: no callback registration, no event ordering bugs, no missed events.
- It is fast: a single array lookup per query.

GLFW fires callbacks asynchronously on the main thread. Those callbacks buffer raw events into a pending queue. At the start of each tick, the buffer is processed into the current frame's state. All systems during that tick see identical, consistent input state.

### 1.3 All Tiers

Input has no GPU dependency. A keyboard and mouse work the same whether you have a GeForce RTX 4090 or an Intel HD 4000. The input system declares support for ALL tiers.

### 1.4 No Allocations in the Hot Path

The input system performs zero heap allocations after initialization. All state arrays are fixed-size, stack-friendly, and small enough to live in L1 cache. The GLFW callback path writes into a pre-allocated ring buffer. The per-tick update reads that buffer and writes into fixed arrays. No `std::vector`, no `std::function`, no `new`.

---

## 2. Input State Model

### 2.1 Key and Button States

Every key and mouse button exists in exactly one of four states at any given tick:

| State | Meaning | Condition |
|-------|---------|-----------|
| **Up** | Not pressed, was not pressed last frame either | `!current && !previous` |
| **Pressed** | Just went down this frame (transition) | `current && !previous` |
| **Held** | Down, and was already down last frame | `current && previous` |
| **Released** | Just went up this frame (transition) | `!current && previous` |

These four states are derived from two bits of information: the current frame's raw down/up state and the previous frame's raw down/up state. We store only the raw `bool` (down or up) per key per frame. The pressed/held/released/up predicates are computed on query by comparing current vs previous.

### 2.2 Frame Coherence

State transitions happen **between** frames, never mid-tick. Here is the exact sequence:

1. GLFW callbacks fire during `glfwPollEvents()` (which happens in the main loop before any ticks run). These callbacks write raw events into a pending event buffer.
2. At the start of `Application::tick()`, before any systems run, `updateInput()` is called.
3. `updateInput()` does three things in order:
   a. Copies the current key/button state array to the previous state array (`memcpy`, one cache line for mouse buttons, a few cache lines for keys).
   b. Processes every pending event from the buffer, updating the current state array.
   c. Clears the pending event buffer.
   d. Snapshots the current mouse position and computes the delta from the previous position.
4. All systems running during this tick see the same input state. A key that is "pressed" at the start of tick remains "pressed" for every system in that tick.

### 2.3 Multiple Ticks Per Frame

The fixed-rate loop can run multiple ticks per rendered frame (when the frame takes longer than `fixedDt`). Each tick gets its own `updateInput()` call, but GLFW events only arrive during `glfwPollEvents()` which runs once per frame, before any ticks. This means:

- The **first** tick of a frame sees all the GLFW events that arrived during `glfwPollEvents()`.
- **Subsequent** ticks in the same frame see no new events — the pending buffer is empty. The previous state gets copied from the current state, so all keys show as either Held or Up (no new Pressed or Released transitions).

This is correct behavior. A key press that happened during the frame is seen as Pressed in exactly one tick, then Held in subsequent ticks. This prevents gameplay bugs where a jump triggers twice because two ticks ran in one frame.

---

## 3. Keyboard Input

### 3.1 Key Codes

FFE defines its own key enum that maps 1:1 to GLFW key codes. This keeps GLFW out of the public API while avoiding any translation overhead (the enum values are identical, just in the `ffe` namespace).

```cpp
namespace ffe {

// Key codes — values match GLFW_KEY_* for zero-cost translation.
// Only the keys that games actually use are listed. This is not an exhaustive
// keyboard scan code table. Add more as needed.
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
    DELETE_KEY = 261,  // DELETE is a macro on some platforms
    HOME       = 268,
    END        = 269,
    PAGE_UP    = 266,
    PAGE_DOWN  = 267,

    KEY_COUNT  = 512   // Upper bound for array sizing — NOT a valid key
};

} // namespace ffe
```

### 3.2 Key State Storage

Key states are stored as a flat `bool` array indexed by the raw key code value:

```cpp
inline constexpr i32 MAX_KEYS = 512; // Matches GLFW_KEY_LAST + 1

struct KeyboardState {
    bool current[MAX_KEYS];   // true = down this frame
    bool previous[MAX_KEYS];  // true = down last frame
};
```

Total size: `512 * 2 = 1024 bytes`. Fits in L1 cache on every CPU manufactured since 2005.

Lookup is O(1): `current[static_cast<i32>(key)]`. No hash maps, no search.

### 3.3 Query Functions

```cpp
namespace ffe {
    bool isKeyPressed(Key key);   // Just went down this frame
    bool isKeyHeld(Key key);      // Down, was already down last frame
    bool isKeyReleased(Key key);  // Just went up this frame
    bool isKeyUp(Key key);        // Not pressed at all

    // Modifier convenience — checks both left and right variants
    bool isShiftDown();
    bool isCtrlDown();
    bool isAltDown();
}
```

Implementation is trivial:

```cpp
bool isKeyPressed(Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return g_keyboard.current[k] && !g_keyboard.previous[k];
}

bool isKeyHeld(Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return g_keyboard.current[k] && g_keyboard.previous[k];
}

bool isKeyReleased(Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return !g_keyboard.current[k] && g_keyboard.previous[k];
}

bool isKeyUp(Key key) {
    const i32 k = static_cast<i32>(key);
    if (k < 0 || k >= MAX_KEYS) return false;
    return !g_keyboard.current[k] && !g_keyboard.previous[k];
}

bool isShiftDown() {
    return g_keyboard.current[static_cast<i32>(Key::LEFT_SHIFT)]
        || g_keyboard.current[static_cast<i32>(Key::RIGHT_SHIFT)];
}

// Contract: all key/button query functions perform bounds checks and return false
// for out-of-range values. This guards against invalid enum casts or corrupted input.
```

### 3.4 GLFW Key Callback

```cpp
static void glfwKeyCallback(GLFWwindow* /*window*/, int key, int /*scancode*/,
                             int action, int /*mods*/) {
    if (key < 0 || key >= MAX_KEYS) return; // Bounds check — GLFW can report unknown keys as -1
    if (action == GLFW_PRESS) {
        if (g_pendingKeyCount < MAX_PENDING_KEY_EVENTS) {
            g_pendingKeyEvents[g_pendingKeyCount++] = {static_cast<i16>(key), true};
        }
    } else if (action == GLFW_RELEASE) {
        if (g_pendingKeyCount < MAX_PENDING_KEY_EVENTS) {
            g_pendingKeyEvents[g_pendingKeyCount++] = {static_cast<i16>(key), false};
        }
    }
    // GLFW_REPEAT is ignored — we derive held state from current vs previous
    // Note: if the pending buffer is full, events are silently dropped. With 64 slots
    // and events only arriving during glfwPollEvents(), overflow is practically impossible
    // under normal usage, but the bounds check prevents buffer overrun regardless.
}
```

---

## 4. Mouse Input

### 4.1 Mouse State

```cpp
inline constexpr i32 MAX_MOUSE_BUTTONS = 5; // Left, Right, Middle, Extra1, Extra2

enum class MouseButton : i32 {
    LEFT   = 0,   // GLFW_MOUSE_BUTTON_LEFT
    RIGHT  = 1,   // GLFW_MOUSE_BUTTON_RIGHT
    MIDDLE = 2,   // GLFW_MOUSE_BUTTON_MIDDLE
    EXTRA1 = 3,   // GLFW_MOUSE_BUTTON_4
    EXTRA2 = 4    // GLFW_MOUSE_BUTTON_5
};

struct MouseState {
    // Position in screen coordinates (pixels, origin top-left)
    f64 x;
    f64 y;

    // Delta since last frame (computed during updateInput)
    f64 deltaX;
    f64 deltaY;

    // Previous position (used to compute delta)
    f64 prevX;
    f64 prevY;

    // Scroll wheel delta (accumulated since last frame)
    f64 scrollX;
    f64 scrollY;

    // Button states
    bool currentButtons[MAX_MOUSE_BUTTONS];
    bool previousButtons[MAX_MOUSE_BUTTONS];

    // Cursor mode
    bool cursorCaptured;  // true = hidden + locked (FPS-style)
    bool firstMouseInput; // true until the first cursor position callback fires
};
```

Total size: approximately 72 bytes. Trivially fits in a single cache line.

### 4.2 Mouse Query Functions

```cpp
namespace ffe {
    // Position
    f64 mouseX();
    f64 mouseY();

    // Delta (movement since last updateInput call)
    f64 mouseDeltaX();
    f64 mouseDeltaY();

    // Scroll
    f64 scrollDeltaX();
    f64 scrollDeltaY();

    // Buttons
    bool isMouseButtonPressed(MouseButton btn);
    bool isMouseButtonHeld(MouseButton btn);
    bool isMouseButtonReleased(MouseButton btn);
    bool isMouseButtonUp(MouseButton btn);

    // Cursor control
    void setCursorCaptured(bool captured); // Hides cursor + enables raw motion
    bool isCursorCaptured();
}
```

Button queries follow the same current/previous pattern as keyboard keys. All mouse button query functions (`isMouseButtonPressed`, `isMouseButtonHeld`, `isMouseButtonReleased`, `isMouseButtonUp`) must perform bounds checks on the button index:

```cpp
bool isMouseButtonPressed(MouseButton btn) {
    const i32 b = static_cast<i32>(btn);
    if (b < 0 || b >= MAX_MOUSE_BUTTONS) return false;
    return g_mouse.currentButtons[b] && !g_mouse.previousButtons[b];
}
```

### 4.3 Mouse Position and Delta

Mouse position is read from GLFW's cursor position callback. The delta is computed in `updateInput()`:

```cpp
// Inside updateInput():
g_mouse.deltaX = g_mouse.x - g_mouse.prevX;
g_mouse.deltaY = g_mouse.y - g_mouse.prevY;
g_mouse.prevX  = g_mouse.x;
g_mouse.prevY  = g_mouse.y;
```

On the very first frame (before any cursor callback has fired), `firstMouseInput` is true. The first cursor position callback sets the position and clears the flag. Delta is zero until the second callback. This prevents a massive delta spike on the first frame when the cursor position jumps from (0,0) to wherever the mouse actually is.

### 4.4 Scroll Wheel

Scroll events are accumulated in the GLFW scroll callback:

```cpp
static void glfwScrollCallback(GLFWwindow* /*window*/, double xoffset, double yoffset) {
    // Reject non-finite values (NaN, Inf) from drivers or platform bugs
    if (!std::isfinite(xoffset) || !std::isfinite(yoffset)) return;

    g_pendingScrollX += xoffset;
    g_pendingScrollY += yoffset;

    // Clamp accumulated scroll to prevent runaway values from broken hardware/drivers
    inline constexpr f64 MAX_SCROLL_ACCUMULATOR = 1000.0;
    g_pendingScrollX = std::clamp(g_pendingScrollX, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
    g_pendingScrollY = std::clamp(g_pendingScrollY, -MAX_SCROLL_ACCUMULATOR, MAX_SCROLL_ACCUMULATOR);
}
```

In `updateInput()`, the accumulated scroll is copied to `g_mouse.scrollX`/`scrollY` and the pending accumulators are zeroed. This means scroll delta represents the total scroll that happened since the last tick.

### 4.5 Cursor Capture

For FPS-style mouse look, the game calls `setCursorCaptured(true)`. This:

1. Sets `glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED)`.
2. If GLFW supports raw mouse motion (`glfwRawMouseMotionSupported()`), enables it with `glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE)`.
3. Sets `g_mouse.cursorCaptured = true`.

Calling `setCursorCaptured(false)` reverses this. The GLFWwindow pointer needed for these calls is stored internally during `initInput()`.

### 4.6 GLFW Mouse Callbacks

```cpp
static void glfwMouseButtonCallback(GLFWwindow* /*window*/, int button, int action, int /*mods*/) {
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) return;
    if (action == GLFW_PRESS) {
        if (g_pendingMouseButtonCount < MAX_PENDING_MOUSE_BUTTON_EVENTS) {
            g_pendingMouseButtonEvents[g_pendingMouseButtonCount++] = {static_cast<i8>(button), true};
        }
    } else if (action == GLFW_RELEASE) {
        if (g_pendingMouseButtonCount < MAX_PENDING_MOUSE_BUTTON_EVENTS) {
            g_pendingMouseButtonEvents[g_pendingMouseButtonCount++] = {static_cast<i8>(button), false};
        }
    }
    // Note: if the pending buffer is full, events are silently dropped. With 16 slots
    // this is effectively impossible for mouse buttons, but the bounds check is mandatory.
}

static void glfwCursorPosCallback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (g_mouse.firstMouseInput) {
        g_mouse.prevX = xpos;
        g_mouse.prevY = ypos;
        g_mouse.firstMouseInput = false;
    }
    g_mouse.x = xpos;
    g_mouse.y = ypos;
}
```

---

## 5. Action Mapping

### 5.1 Why Actions Exist

Raw key queries are fine for engine internals and prototyping, but shipped games need an abstraction layer:

- **Rebinding:** Players must be able to remap controls. If gameplay code checks `isKeyPressed(Key::SPACE)`, rebinding requires changing code. If it checks `isActionPressed("jump")`, rebinding changes data.
- **Multiple bindings:** "move_left" should be triggered by both `A` and `LEFT`. Without actions, every movement check becomes `isKeyHeld(Key::A) || isKeyHeld(Key::LEFT)` scattered everywhere.
- **Accessibility:** Alternative control schemes (one-handed, reduced keys) are data changes, not code changes.
- **Lua scripting:** Lua scripts should reference action names, not platform-specific key codes.

### 5.2 Action Data Layout

```cpp
inline constexpr i32 MAX_ACTIONS = 64;           // More than enough for any game
inline constexpr i32 MAX_BINDINGS_PER_ACTION = 4; // Up to 4 keys/buttons per action

// A single action binding — either a key or a mouse button
struct InputBinding {
    enum class Type : u8 { NONE = 0, KEY, MOUSE_BUTTON };
    Type type;
    i16  code; // Key code or MouseButton value
};

struct Action {
    // Name stored as a fixed-size char array — no heap allocation.
    // 31 chars + null terminator. "move_forward" is 12 chars. This is plenty.
    char name[32];
    InputBinding bindings[MAX_BINDINGS_PER_ACTION];
    u8 bindingCount;
};

struct ActionMap {
    Action actions[MAX_ACTIONS];
    i32 actionCount;
};
```

Total size of ActionMap: `64 * (32 + 4*4 + 1) + 4 = ~3.3 KB`. Fits comfortably in L1 cache.

### 5.3 Action Registration

Actions are registered at startup, typically during game initialization:

```cpp
namespace ffe {
    // Register a new action. Returns the action index for fast lookup.
    // Returns -1 if MAX_ACTIONS is exceeded.
    // The name is copied using snprintf(action.name, sizeof(action.name), "%s", name)
    // to guarantee null-termination and prevent buffer overrun regardless of input length.
    i32 registerAction(const char* name);

    // Bind a key to an action. Up to MAX_BINDINGS_PER_ACTION bindings per action.
    void bindActionKey(i32 actionIndex, Key key);
    void bindActionMouseButton(i32 actionIndex, MouseButton btn);

    // Remove all bindings for an action (for rebinding)
    void clearActionBindings(i32 actionIndex);

    // Find action index by name. Returns -1 if not found.
    // This is a linear scan over MAX_ACTIONS entries — call at init, not per-frame.
    i32 findAction(const char* name);
}
```

Example usage at game startup:

```cpp
const i32 jumpAction = ffe::registerAction("jump");
ffe::bindActionKey(jumpAction, ffe::Key::SPACE);
ffe::bindActionKey(jumpAction, ffe::Key::W);

const i32 moveLeftAction = ffe::registerAction("move_left");
ffe::bindActionKey(moveLeftAction, ffe::Key::A);
ffe::bindActionKey(moveLeftAction, ffe::Key::LEFT);

const i32 shootAction = ffe::registerAction("shoot");
ffe::bindActionMouseButton(shootAction, ffe::MouseButton::LEFT);
```

### 5.4 Action Query Functions

```cpp
namespace ffe {
    // Query by action index (fast — O(bindings) which is at most 4)
    bool isActionPressed(i32 actionIndex);
    bool isActionHeld(i32 actionIndex);
    bool isActionReleased(i32 actionIndex);
    bool isActionUp(i32 actionIndex);
}
```

Implementation: for each binding in the action, check the corresponding key or mouse button state. Return `true` if **any** binding satisfies the condition.

```cpp
bool isActionPressed(i32 actionIndex) {
    if (actionIndex < 0 || actionIndex >= g_actionMap.actionCount) return false;
    const Action& action = g_actionMap.actions[actionIndex];
    for (i32 i = 0; i < action.bindingCount; ++i) {
        const InputBinding& b = action.bindings[i];
        if (b.type == InputBinding::Type::KEY && isKeyPressed(static_cast<Key>(b.code))) return true;
        if (b.type == InputBinding::Type::MOUSE_BUTTON && isMouseButtonPressed(static_cast<MouseButton>(b.code))) return true;
    }
    return false;
}

// Contract: all action query functions (isActionPressed, isActionHeld, isActionReleased,
// isActionUp) and binding functions (bindActionKey, bindActionMouseButton,
// clearActionBindings) perform bounds checks on actionIndex and return false (or no-op
// for void functions) on invalid indices. This prevents out-of-bounds access from stale
// or corrupted action indices.
```

### 5.5 Action Maps From Config (Future)

In a future ADR (asset loading / config system), action maps will be loadable from a JSON configuration file:

```json
{
    "actions": [
        { "name": "jump",      "bindings": ["KEY_SPACE", "KEY_W"] },
        { "name": "move_left", "bindings": ["KEY_A", "KEY_LEFT"] },
        { "name": "shoot",     "bindings": ["MOUSE_LEFT"] }
    ]
}
```

The current ADR does not implement JSON loading. Actions are registered in code. The data layout is already designed to support loading from a file — the `ActionMap` struct is a flat, serializable block.

---

## 6. Integration with Game Loop

### 6.1 Initialization

During `Application::startup()`, after the GLFW window is created:

```cpp
// Step 4b (new — after window creation, before renderer init)
ffe::initInput(m_window);
```

`initInput()` does:
1. Zero-initializes all state arrays (`memset` — `KeyboardState`, `MouseState`, `ActionMap`, pending event buffers).
2. Stores the `GLFWwindow*` pointer internally.
3. Registers GLFW callbacks: `glfwSetKeyCallback`, `glfwSetMouseButtonCallback`, `glfwSetCursorPosCallback`, `glfwSetScrollCallback`.
4. Sets `g_mouse.firstMouseInput = true`.

### 6.2 Per-Tick Update

The `updateInput()` call must happen at the start of every tick, before any systems run. Modify `Application::tick()`:

```cpp
void Application::tick(const float dt) {
    ffe::updateInput(); // <-- NEW: process input before systems

    for (const auto& system : m_world.systems()) {
        ZoneScoped;
        ZoneName(system.name, system.nameLength);
        system.updateFn(m_world, dt);
    }
}
```

**Alternative approach (system-based):** Register input update as a system with priority 0 (the lowest, so it runs before everything else per the priority convention in `system.h`):

```cpp
// In startup(), register the input update as a system:
const SystemDescriptor inputDesc = {
    "InputUpdate",
    11, // strlen
    [](World& /*world*/, float /*dt*/) { ffe::updateInput(); },
    0   // Priority 0 — runs before all gameplay systems
};
m_world.registerSystem(inputDesc);
```

**Decision:** Use the system-based approach. This keeps `tick()` clean and follows the established pattern. Priority 0 is within the 0-99 range designated for "Input polling" in `system.h`. The lambda captures nothing and has no state, so it compiles to a plain function pointer — no `std::function`, no heap allocation.

**However**, the lambda-to-function-pointer conversion requires the lambda to be non-capturing and match the `void(*)(World&, float)` signature exactly. Since this is a stateless lambda with matching signature, C++ guarantees the conversion. engine-dev: if the compiler complains, use a named free function instead.

### 6.3 Shutdown

During `Application::shutdown()`, before window destruction:

```cpp
// Step 4a (new — before destroying the GLFW window)
ffe::shutdownInput();
```

`shutdownInput()` does:
1. Removes GLFW callbacks (sets them to `nullptr`).
2. Clears the stored `GLFWwindow*` pointer.
3. Zero-fills all state arrays.

### 6.4 Headless Mode

When `Application` runs in headless mode (`config.headless == true`), there is no GLFW window and no GLFW callbacks. `initInput()` must handle a `nullptr` window gracefully:

```cpp
void initInput(GLFWwindow* window) {
    // Zero-init all state...
    g_window = window;
    if (window != nullptr) {
        glfwSetKeyCallback(window, glfwKeyCallback);
        glfwSetMouseButtonCallback(window, glfwMouseButtonCallback);
        glfwSetCursorPosCallback(window, glfwCursorPosCallback);
        glfwSetScrollCallback(window, glfwScrollCallback);
    }
    g_mouse.firstMouseInput = true;
}
```

In headless mode, all keys report Up, mouse position stays at (0,0), and deltas are zero. This is correct — tests that need to simulate input can write directly to the state arrays via test-only helpers (see Section 10).

---

## 7. Data Layout

### 7.1 Global State

All input state lives in file-scope globals inside `input.cpp`. There is exactly one keyboard, one mouse, and one action map. This is not a limitation — GLFW itself is single-window in practice, and games have exactly one input context.

```cpp
// input.cpp — file-scope globals
static GLFWwindow*  g_window = nullptr;

// Keyboard
static KeyboardState g_keyboard = {};  // 1024 bytes

// Mouse
static MouseState g_mouse = {};        // ~72 bytes

// Actions
static ActionMap g_actionMap = {};     // ~3.3 KB

// Pending event buffers
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

static PendingKeyEvent g_pendingKeyEvents[MAX_PENDING_KEY_EVENTS];
static i32 g_pendingKeyCount = 0;

static PendingMouseButtonEvent g_pendingMouseButtonEvents[MAX_PENDING_MOUSE_BUTTON_EVENTS];
static i32 g_pendingMouseButtonCount = 0;

static f64 g_pendingScrollX = 0.0;
static f64 g_pendingScrollY = 0.0;
```

### 7.2 Memory Budget

| Component | Size | Notes |
|-----------|------|-------|
| `KeyboardState` | 1,024 B | Two 512-byte `bool` arrays |
| `MouseState` | ~72 B | Position, delta, buttons, flags |
| `ActionMap` | ~3,328 B | 64 actions, 32-byte names, 4 bindings each |
| Pending key events | 256 B | 64 events * 4 bytes each |
| Pending mouse events | 32 B | 16 events * 2 bytes each |
| Pending scroll | 16 B | Two `f64` accumulators |
| **Total** | **~4.7 KB** | |

4.7 KB fits entirely in L1 cache (32 KB on even the oldest supported CPUs). The hot path during `updateInput()` touches approximately 1.5 KB (memcpy previous states + scan pending events). The hot path during queries touches a single `bool` (two for pressed/released).

### 7.3 Why Not ECS Components?

Input state is global, singleton, and read by nearly every gameplay system. Putting it into an ECS component would mean every system does `world.registry().ctx().get<InputState>()` which is an extra indirection through EnTT's context map. A flat global with free functions is faster, simpler, and more discoverable. The input state is not per-entity — there is one keyboard, one mouse, one player's input.

---

## 8. Interface Design

### 8.1 Complete Public Interface (`engine/core/input.h`)

```cpp
#pragma once

#include "core/types.h"

// Forward declaration — GLFW types do NOT appear in the public API
struct GLFWwindow;

namespace ffe {

// ─── Key Codes ───────────────────────────────────────────────────────────────

inline constexpr i32 MAX_KEYS = 512;

enum class Key : i32 {
    // [Values as defined in Section 3.1]
    // ...
    KEY_COUNT = 512
};

// ─── Mouse ───────────────────────────────────────────────────────────────────

inline constexpr i32 MAX_MOUSE_BUTTONS = 5;

enum class MouseButton : i32 {
    LEFT   = 0,
    RIGHT  = 1,
    MIDDLE = 2,
    EXTRA1 = 3,
    EXTRA2 = 4
};

// ─── Actions ─────────────────────────────────────────────────────────────────

inline constexpr i32 MAX_ACTIONS = 64;
inline constexpr i32 MAX_BINDINGS_PER_ACTION = 4;

// ─── Lifecycle ───────────────────────────────────────────────────────────────

// Initialize the input system. Call after GLFW window creation.
// window may be nullptr (headless mode).
void initInput(GLFWwindow* window);

// Shutdown the input system. Call before GLFW window destruction.
void shutdownInput();

// Process pending events into current-frame state.
// Call once at the start of each tick, before any systems run.
void updateInput();

// ─── Keyboard Queries ────────────────────────────────────────────────────────

bool isKeyPressed(Key key);    // Down this frame, was up last frame
bool isKeyHeld(Key key);       // Down this frame, was down last frame
bool isKeyReleased(Key key);   // Up this frame, was down last frame
bool isKeyUp(Key key);         // Up this frame, was up last frame

// Modifier convenience (checks both left and right variants)
bool isShiftDown();
bool isCtrlDown();
bool isAltDown();

// ─── Mouse Queries ───────────────────────────────────────────────────────────

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

// ─── Action Mapping ──────────────────────────────────────────────────────────

// Register a named action. Returns action index. -1 if MAX_ACTIONS exceeded.
i32 registerAction(const char* name);

// Bind a key or mouse button to an action. Returns false if MAX_BINDINGS exceeded.
bool bindActionKey(i32 actionIndex, Key key);
bool bindActionMouseButton(i32 actionIndex, MouseButton btn);

// Remove all bindings for an action.
void clearActionBindings(i32 actionIndex);

// Find action index by name. Returns -1 if not found. O(n) — call at init, not per-frame.
i32 findAction(const char* name);

// Query action state (checks all bindings, returns true if ANY binding matches)
bool isActionPressed(i32 actionIndex);
bool isActionHeld(i32 actionIndex);
bool isActionReleased(i32 actionIndex);
bool isActionUp(i32 actionIndex);

} // namespace ffe
```

### 8.2 What Is NOT in the Public Interface

- `GLFWwindow` usage beyond the `initInput` parameter (forward-declared only).
- The internal state structs (`KeyboardState`, `MouseState`, `ActionMap`). These are implementation details in `input.cpp`.
- GLFW callback functions. These are `static` in `input.cpp`.
- Pending event buffers. Internal to `input.cpp`.
- Any callback registration mechanism. Game code does not register input callbacks.

---

## 9. File Layout

```
engine/core/
├── input.h           # Public interface (Section 8.1)
└── input.cpp         # Implementation: state, callbacks, queries, actions
```

Two files. No subdirectories. No additional headers. The Key enum and MouseButton enum live in `input.h` because they are part of the public API.

### 9.1 CMake Integration

Add `input.cpp` to the existing `engine/core/CMakeLists.txt` source list. No new dependencies — GLFW is already linked for window creation.

---

## 10. Implementation Checklist

| # | File | What | Approx Lines |
|---|------|------|-------------|
| 1 | `engine/core/input.h` | Public interface: Key enum, MouseButton enum, all function declarations, constants | ~160 |
| 2 | `engine/core/input.cpp` | Internal state structs, GLFW callbacks, `initInput`, `shutdownInput`, `updateInput`, all query functions, action system | ~350 |
| 3 | `engine/core/CMakeLists.txt` | Add `input.cpp` to source list | +1 line |
| 4 | `engine/core/application.h` | No changes required (input is not a member — it uses file-scope globals) | 0 |
| 5 | `engine/core/application.cpp` | Add `#include "core/input.h"`, call `initInput(m_window)` in `startup()`, register input update system, call `shutdownInput()` in `shutdown()` | +15 lines |
| **Total** | | | **~525 lines** |

### 10.1 Implementation Order

1. **`input.h`** — Write the complete public interface. This is the contract.
2. **`input.cpp`** — Implement in this order:
   a. Internal state structs and file-scope globals.
   b. GLFW callbacks (key, mouse button, cursor position, scroll).
   c. `initInput()` and `shutdownInput()`.
   d. `updateInput()` — the frame transition logic.
   e. Keyboard query functions.
   f. Mouse query functions and `setCursorCaptured`.
   g. Action registration and binding functions.
   h. Action query functions.
3. **`CMakeLists.txt`** — Add `input.cpp`.
4. **`application.cpp`** — Wire into startup/shutdown/tick.

### 10.2 Test Hooks

For headless testing, add an internal (non-public) function to simulate input events:

```cpp
// In input.cpp, guarded by #ifdef FFE_TEST — this is a HARD REQUIREMENT.
// Test hooks MUST be compiled out of non-test builds. They MUST NOT exist
// behind any other mechanism (e.g., runtime flags, alternative headers).
// #ifdef FFE_TEST is the only acceptable guard.
namespace ffe::test {
    void simulateKeyPress(Key key);
    void simulateKeyRelease(Key key);
    void simulateMouseButtonPress(MouseButton btn);
    void simulateMouseButtonRelease(MouseButton btn);
    void simulateMouseMove(f64 x, f64 y);
    void simulateScroll(f64 dx, f64 dy);
}
```

These write directly to the pending event buffers, exactly as GLFW callbacks would. This lets `test-engineer` write deterministic input tests without a window.

---

## 11. What This Prevents (and Why That's OK)

### 11.1 No Gamepad Support

This ADR covers keyboard and mouse only. Gamepad support (SDL_GameController or GLFW joystick API) is a separate ADR. The action mapping layer is designed to accommodate gamepads later — `InputBinding::Type` already has room for `GAMEPAD_BUTTON` and `GAMEPAD_AXIS` variants without changing the action query API.

### 11.2 No Text Input / IME

The input system handles key-as-button input for gameplay. It does not handle text input, Unicode character entry, or IME composition. Those are UI concerns that belong in a future text input / UI system. GLFW's `glfwSetCharCallback` is deliberately not registered here.

### 11.3 No Multi-Window Input

Input state is global. There is one keyboard state and one mouse state. This matches FFE's single-window architecture (ADR-001). If FFE ever supports multiple windows (unlikely for a game engine targeting older hardware), input routing would need a redesign.

### 11.4 No Input Recording / Playback

The pending event buffer design would support recording and playback (serialize the buffer per-tick), but this ADR does not implement it. It is a natural extension that does not require architectural changes.

### 11.5 No Analog Axes for Keyboard

Keys are digital (down or up). There is no "how hard is W pressed" concept. Analog input belongs to gamepads, which are out of scope.

### 11.6 No Input Consumed / Event Propagation

There is no mechanism for one system to "consume" an input and prevent other systems from seeing it. All systems see the same state. If the UI layer needs to eat inputs (e.g., typing in a chat box should not also move the character), that is handled at the action mapping level — disable the gameplay action map while the UI is focused. This is a future concern.

### 11.7 No Per-Frame Allocation

The input system allocates nothing after `initInput()`. All arrays are fixed-size. All strings are fixed-length char arrays. This means input works identically on RETRO tier (512 KB arena) and MODERN tier. There is zero interaction with the arena allocator.

---

## Appendix A: updateInput() Complete Pseudocode

```
function updateInput():
    // 1. Keyboard: copy current -> previous
    memcpy(g_keyboard.previous, g_keyboard.current, MAX_KEYS)

    // 2. Process pending key events
    for i in 0..g_pendingKeyCount:
        event = g_pendingKeyEvents[i]
        g_keyboard.current[event.keyCode] = event.down
    g_pendingKeyCount = 0

    // 3. Mouse buttons: copy current -> previous
    memcpy(g_mouse.previousButtons, g_mouse.currentButtons, MAX_MOUSE_BUTTONS)

    // 4. Process pending mouse button events
    for i in 0..g_pendingMouseButtonCount:
        event = g_pendingMouseButtonEvents[i]
        g_mouse.currentButtons[event.button] = event.down
    g_pendingMouseButtonCount = 0

    // 5. Mouse position delta
    g_mouse.deltaX = g_mouse.x - g_mouse.prevX
    g_mouse.deltaY = g_mouse.y - g_mouse.prevY
    g_mouse.prevX  = g_mouse.x
    g_mouse.prevY  = g_mouse.y

    // 6. Scroll delta
    g_mouse.scrollX = g_pendingScrollX
    g_mouse.scrollY = g_pendingScrollY
    g_pendingScrollX = 0.0
    g_pendingScrollY = 0.0
```

Total cost per tick: two `memcpy` calls (517 bytes total), a loop over typically 0-3 pending events, and 6 floating-point subtractions. This is effectively free.
