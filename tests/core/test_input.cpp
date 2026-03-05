#include <catch2/catch_test_macros.hpp>
#include "core/input.h"

// Helper: init in headless mode, run tests, shutdown
struct InputFixture {
    InputFixture()  { ffe::initInput(nullptr); }
    ~InputFixture() { ffe::shutdownInput(); }
};

// =============================================================================
// Key state transitions
// =============================================================================

TEST_CASE("Key starts in Up state", "[input][keyboard]") {
    InputFixture fix;
    ffe::updateInput();

    REQUIRE(ffe::isKeyUp(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyPressed(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyHeld(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyReleased(ffe::Key::W));
}

TEST_CASE("Key press produces Pressed state", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();

    REQUIRE(ffe::isKeyPressed(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyHeld(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyReleased(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyUp(ffe::Key::W));
}

TEST_CASE("Key transitions Pressed -> Held on next frame", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();

    // Next frame, no new events: key stays down -> held
    ffe::updateInput();

    REQUIRE(ffe::isKeyHeld(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyPressed(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyReleased(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyUp(ffe::Key::W));
}

TEST_CASE("Key transitions Held -> Released on release", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();
    ffe::updateInput(); // now held

    ffe::test::simulateKeyRelease(ffe::Key::W);
    ffe::updateInput();

    REQUIRE(ffe::isKeyReleased(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyPressed(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyHeld(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyUp(ffe::Key::W));
}

TEST_CASE("Key transitions Released -> Up on next frame", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();
    ffe::updateInput(); // held
    ffe::test::simulateKeyRelease(ffe::Key::W);
    ffe::updateInput(); // released

    ffe::updateInput(); // up

    REQUIRE(ffe::isKeyUp(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyPressed(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyHeld(ffe::Key::W));
    REQUIRE_FALSE(ffe::isKeyReleased(ffe::Key::W));
}

TEST_CASE("Full key lifecycle: up -> pressed -> held -> released -> up", "[input][keyboard]") {
    InputFixture fix;

    // Frame 0: up
    ffe::updateInput();
    REQUIRE(ffe::isKeyUp(ffe::Key::A));

    // Frame 1: pressed
    ffe::test::simulateKeyPress(ffe::Key::A);
    ffe::updateInput();
    REQUIRE(ffe::isKeyPressed(ffe::Key::A));

    // Frame 2: held
    ffe::updateInput();
    REQUIRE(ffe::isKeyHeld(ffe::Key::A));

    // Frame 3: still held
    ffe::updateInput();
    REQUIRE(ffe::isKeyHeld(ffe::Key::A));

    // Frame 4: released
    ffe::test::simulateKeyRelease(ffe::Key::A);
    ffe::updateInput();
    REQUIRE(ffe::isKeyReleased(ffe::Key::A));

    // Frame 5: back to up
    ffe::updateInput();
    REQUIRE(ffe::isKeyUp(ffe::Key::A));
}

TEST_CASE("Multiple keys can be tracked independently", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();
    REQUIRE(ffe::isKeyPressed(ffe::Key::W));
    REQUIRE(ffe::isKeyUp(ffe::Key::S));

    ffe::test::simulateKeyPress(ffe::Key::S);
    ffe::updateInput();
    REQUIRE(ffe::isKeyHeld(ffe::Key::W));
    REQUIRE(ffe::isKeyPressed(ffe::Key::S));
}

TEST_CASE("Modifier convenience functions", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::LEFT_SHIFT);
    ffe::updateInput();
    REQUIRE(ffe::isShiftDown());

    ffe::test::simulateKeyRelease(ffe::Key::LEFT_SHIFT);
    ffe::test::simulateKeyPress(ffe::Key::RIGHT_SHIFT);
    ffe::updateInput();
    REQUIRE(ffe::isShiftDown());

    ffe::test::simulateKeyRelease(ffe::Key::RIGHT_SHIFT);
    ffe::updateInput();
    REQUIRE_FALSE(ffe::isShiftDown());
}

TEST_CASE("Ctrl modifier convenience", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::LEFT_CTRL);
    ffe::updateInput();
    REQUIRE(ffe::isCtrlDown());

    ffe::test::simulateKeyRelease(ffe::Key::LEFT_CTRL);
    ffe::updateInput();
    REQUIRE_FALSE(ffe::isCtrlDown());
}

TEST_CASE("Alt modifier convenience", "[input][keyboard]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::LEFT_ALT);
    ffe::updateInput();
    REQUIRE(ffe::isAltDown());

    ffe::test::simulateKeyRelease(ffe::Key::LEFT_ALT);
    ffe::updateInput();
    REQUIRE_FALSE(ffe::isAltDown());
}

// =============================================================================
// Mouse button states
// =============================================================================

TEST_CASE("Mouse button starts in Up state", "[input][mouse]") {
    InputFixture fix;
    ffe::updateInput();

    REQUIRE(ffe::isMouseButtonUp(ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::isMouseButtonPressed(ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::isMouseButtonHeld(ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::isMouseButtonReleased(ffe::MouseButton::LEFT));
}

TEST_CASE("Mouse button press produces Pressed state", "[input][mouse]") {
    InputFixture fix;

    ffe::test::simulateMouseButtonPress(ffe::MouseButton::LEFT);
    ffe::updateInput();

    REQUIRE(ffe::isMouseButtonPressed(ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::isMouseButtonHeld(ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::isMouseButtonReleased(ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::isMouseButtonUp(ffe::MouseButton::LEFT));
}

TEST_CASE("Mouse button full lifecycle: pressed -> held -> released -> up", "[input][mouse]") {
    InputFixture fix;

    // Pressed
    ffe::test::simulateMouseButtonPress(ffe::MouseButton::RIGHT);
    ffe::updateInput();
    REQUIRE(ffe::isMouseButtonPressed(ffe::MouseButton::RIGHT));

    // Held
    ffe::updateInput();
    REQUIRE(ffe::isMouseButtonHeld(ffe::MouseButton::RIGHT));

    // Released
    ffe::test::simulateMouseButtonRelease(ffe::MouseButton::RIGHT);
    ffe::updateInput();
    REQUIRE(ffe::isMouseButtonReleased(ffe::MouseButton::RIGHT));

    // Up
    ffe::updateInput();
    REQUIRE(ffe::isMouseButtonUp(ffe::MouseButton::RIGHT));
}

TEST_CASE("All mouse buttons tracked independently", "[input][mouse]") {
    InputFixture fix;

    ffe::test::simulateMouseButtonPress(ffe::MouseButton::LEFT);
    ffe::updateInput();

    REQUIRE(ffe::isMouseButtonPressed(ffe::MouseButton::LEFT));
    REQUIRE(ffe::isMouseButtonUp(ffe::MouseButton::MIDDLE));
    REQUIRE(ffe::isMouseButtonUp(ffe::MouseButton::RIGHT));
}

// =============================================================================
// Mouse position and delta
// =============================================================================

TEST_CASE("Mouse position reports simulated coordinates", "[input][mouse]") {
    InputFixture fix;

    ffe::test::simulateMouseMove(100.0, 200.0);
    ffe::updateInput();

    REQUIRE(ffe::mouseX() == 100.0);
    REQUIRE(ffe::mouseY() == 200.0);
}

TEST_CASE("Mouse delta is zero on first move (no jump)", "[input][mouse]") {
    InputFixture fix;

    ffe::test::simulateMouseMove(100.0, 200.0);
    ffe::updateInput();

    // First move: prev is set to same position, so delta is 0
    REQUIRE(ffe::mouseDeltaX() == 0.0);
    REQUIRE(ffe::mouseDeltaY() == 0.0);
}

TEST_CASE("Mouse delta reflects movement between frames", "[input][mouse]") {
    InputFixture fix;

    ffe::test::simulateMouseMove(100.0, 200.0);
    ffe::updateInput();

    ffe::test::simulateMouseMove(150.0, 250.0);
    ffe::updateInput();

    REQUIRE(ffe::mouseDeltaX() == 50.0);
    REQUIRE(ffe::mouseDeltaY() == 50.0);
}

TEST_CASE("Mouse delta resets to zero when mouse does not move", "[input][mouse]") {
    InputFixture fix;

    ffe::test::simulateMouseMove(100.0, 200.0);
    ffe::updateInput();

    ffe::test::simulateMouseMove(150.0, 250.0);
    ffe::updateInput();
    REQUIRE(ffe::mouseDeltaX() == 50.0);

    // No movement this frame
    ffe::updateInput();
    REQUIRE(ffe::mouseDeltaX() == 0.0);
    REQUIRE(ffe::mouseDeltaY() == 0.0);
}

// =============================================================================
// Scroll wheel
// =============================================================================

TEST_CASE("Scroll delta reports simulated values", "[input][scroll]") {
    InputFixture fix;

    ffe::test::simulateScroll(1.0, -2.0);
    ffe::updateInput();

    REQUIRE(ffe::scrollDeltaX() == 1.0);
    REQUIRE(ffe::scrollDeltaY() == -2.0);
}

TEST_CASE("Scroll delta resets to zero on next update", "[input][scroll]") {
    InputFixture fix;

    ffe::test::simulateScroll(1.0, -2.0);
    ffe::updateInput();

    ffe::updateInput();
    REQUIRE(ffe::scrollDeltaX() == 0.0);
    REQUIRE(ffe::scrollDeltaY() == 0.0);
}

TEST_CASE("Scroll accumulates between frames", "[input][scroll]") {
    InputFixture fix;

    ffe::test::simulateScroll(1.0, 0.0);
    ffe::test::simulateScroll(2.0, 3.0);
    ffe::updateInput();

    REQUIRE(ffe::scrollDeltaX() == 3.0);
    REQUIRE(ffe::scrollDeltaY() == 3.0);
}

// =============================================================================
// Action mapping
// =============================================================================

TEST_CASE("Register and find action by name", "[input][actions]") {
    InputFixture fix;

    const ffe::i32 idx = ffe::registerAction("jump");
    REQUIRE(idx >= 0);
    REQUIRE(ffe::findAction("jump") == idx);
}

TEST_CASE("findAction returns -1 for unknown action", "[input][actions]") {
    InputFixture fix;

    REQUIRE(ffe::findAction("nonexistent") == -1);
}

TEST_CASE("Action follows bound key state", "[input][actions]") {
    InputFixture fix;

    const ffe::i32 jump = ffe::registerAction("jump");
    REQUIRE(ffe::bindActionKey(jump, ffe::Key::SPACE));

    // Up initially
    ffe::updateInput();
    REQUIRE(ffe::isActionUp(jump));

    // Press
    ffe::test::simulateKeyPress(ffe::Key::SPACE);
    ffe::updateInput();
    REQUIRE(ffe::isActionPressed(jump));

    // Hold
    ffe::updateInput();
    REQUIRE(ffe::isActionHeld(jump));

    // Release
    ffe::test::simulateKeyRelease(ffe::Key::SPACE);
    ffe::updateInput();
    REQUIRE(ffe::isActionReleased(jump));

    // Back to up
    ffe::updateInput();
    REQUIRE(ffe::isActionUp(jump));
}

TEST_CASE("Action follows bound mouse button state", "[input][actions]") {
    InputFixture fix;

    const ffe::i32 shoot = ffe::registerAction("shoot");
    REQUIRE(ffe::bindActionMouseButton(shoot, ffe::MouseButton::LEFT));

    ffe::test::simulateMouseButtonPress(ffe::MouseButton::LEFT);
    ffe::updateInput();
    REQUIRE(ffe::isActionPressed(shoot));

    ffe::updateInput();
    REQUIRE(ffe::isActionHeld(shoot));
}

// =============================================================================
// Multiple bindings
// =============================================================================

TEST_CASE("Multiple keys bound to same action - either triggers", "[input][actions]") {
    InputFixture fix;

    const ffe::i32 jump = ffe::registerAction("jump");
    REQUIRE(ffe::bindActionKey(jump, ffe::Key::SPACE));
    REQUIRE(ffe::bindActionKey(jump, ffe::Key::W));

    // Press only SPACE
    ffe::test::simulateKeyPress(ffe::Key::SPACE);
    ffe::updateInput();
    REQUIRE(ffe::isActionPressed(jump));

    // Release SPACE, press W
    ffe::test::simulateKeyRelease(ffe::Key::SPACE);
    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();
    // W is now pressed, action should still be active
    REQUIRE(ffe::isActionPressed(jump));
}

TEST_CASE("Action with key and mouse button bindings", "[input][actions]") {
    InputFixture fix;

    const ffe::i32 action = ffe::registerAction("fire");
    REQUIRE(ffe::bindActionKey(action, ffe::Key::F));
    REQUIRE(ffe::bindActionMouseButton(action, ffe::MouseButton::LEFT));

    // Press mouse button only
    ffe::test::simulateMouseButtonPress(ffe::MouseButton::LEFT);
    ffe::updateInput();
    REQUIRE(ffe::isActionPressed(action));

    // Release mouse, press key next frame
    ffe::test::simulateMouseButtonRelease(ffe::MouseButton::LEFT);
    ffe::test::simulateKeyPress(ffe::Key::F);
    ffe::updateInput();
    REQUIRE(ffe::isActionPressed(action));
}

TEST_CASE("clearActionBindings removes all bindings", "[input][actions]") {
    InputFixture fix;

    const ffe::i32 action = ffe::registerAction("test_clear");
    ffe::bindActionKey(action, ffe::Key::A);
    ffe::clearActionBindings(action);

    ffe::test::simulateKeyPress(ffe::Key::A);
    ffe::updateInput();

    // Action has no bindings, so isActionUp should return true (vacuous truth)
    REQUIRE(ffe::isActionUp(action));
    REQUIRE_FALSE(ffe::isActionPressed(action));
}

// =============================================================================
// Bounds checking - key codes
// =============================================================================

TEST_CASE("Out-of-range key code returns false, does not crash", "[input][bounds]") {
    InputFixture fix;
    ffe::updateInput();

    const auto badKey = static_cast<ffe::Key>(9999);
    REQUIRE_FALSE(ffe::isKeyPressed(badKey));
    REQUIRE_FALSE(ffe::isKeyHeld(badKey));
    REQUIRE_FALSE(ffe::isKeyReleased(badKey));
    REQUIRE_FALSE(ffe::isKeyUp(badKey));
}

TEST_CASE("Negative key code returns false, does not crash", "[input][bounds]") {
    InputFixture fix;
    ffe::updateInput();

    const auto badKey = static_cast<ffe::Key>(-1);
    REQUIRE_FALSE(ffe::isKeyPressed(badKey));
    REQUIRE_FALSE(ffe::isKeyHeld(badKey));
    REQUIRE_FALSE(ffe::isKeyReleased(badKey));
    REQUIRE_FALSE(ffe::isKeyUp(badKey));
}

TEST_CASE("Out-of-range mouse button returns false, does not crash", "[input][bounds]") {
    InputFixture fix;
    ffe::updateInput();

    const auto badBtn = static_cast<ffe::MouseButton>(99);
    REQUIRE_FALSE(ffe::isMouseButtonPressed(badBtn));
    REQUIRE_FALSE(ffe::isMouseButtonHeld(badBtn));
    REQUIRE_FALSE(ffe::isMouseButtonReleased(badBtn));
    REQUIRE_FALSE(ffe::isMouseButtonUp(badBtn));
}

// =============================================================================
// Action index bounds
// =============================================================================

TEST_CASE("Invalid action index returns false for all queries", "[input][actions][bounds]") {
    InputFixture fix;
    ffe::updateInput();

    REQUIRE_FALSE(ffe::isActionPressed(-1));
    REQUIRE_FALSE(ffe::isActionHeld(-1));
    REQUIRE_FALSE(ffe::isActionReleased(-1));
    REQUIRE_FALSE(ffe::isActionUp(-1));

    REQUIRE_FALSE(ffe::isActionPressed(9999));
    REQUIRE_FALSE(ffe::isActionHeld(9999));
    REQUIRE_FALSE(ffe::isActionReleased(9999));
    REQUIRE_FALSE(ffe::isActionUp(9999));
}

TEST_CASE("bindActionKey with invalid index returns false", "[input][actions][bounds]") {
    InputFixture fix;

    REQUIRE_FALSE(ffe::bindActionKey(-1, ffe::Key::A));
    REQUIRE_FALSE(ffe::bindActionKey(9999, ffe::Key::A));
}

TEST_CASE("bindActionMouseButton with invalid index returns false", "[input][actions][bounds]") {
    InputFixture fix;

    REQUIRE_FALSE(ffe::bindActionMouseButton(-1, ffe::MouseButton::LEFT));
    REQUIRE_FALSE(ffe::bindActionMouseButton(9999, ffe::MouseButton::LEFT));
}

// =============================================================================
// Headless mode
// =============================================================================

TEST_CASE("Input system works in headless mode (nullptr window)", "[input][headless]") {
    // This entire test file operates in headless mode (nullptr window).
    // This test explicitly verifies the full workflow works without a GLFW window.
    InputFixture fix;

    // Key input
    ffe::test::simulateKeyPress(ffe::Key::ESCAPE);
    ffe::updateInput();
    REQUIRE(ffe::isKeyPressed(ffe::Key::ESCAPE));

    // Mouse input
    ffe::test::simulateMouseMove(42.0, 84.0);
    ffe::test::simulateMouseButtonPress(ffe::MouseButton::MIDDLE);
    ffe::updateInput();
    REQUIRE(ffe::mouseX() == 42.0);
    REQUIRE(ffe::isMouseButtonPressed(ffe::MouseButton::MIDDLE));

    // Scroll
    ffe::test::simulateScroll(0.0, 5.0);
    ffe::updateInput();
    REQUIRE(ffe::scrollDeltaY() == 5.0);

    // Action mapping
    const ffe::i32 action = ffe::registerAction("headless_action");
    ffe::bindActionKey(action, ffe::Key::ENTER);
    ffe::test::simulateKeyPress(ffe::Key::ENTER);
    ffe::updateInput();
    REQUIRE(ffe::isActionPressed(action));
}

TEST_CASE("Cursor captured state works in headless mode", "[input][headless]") {
    InputFixture fix;

    REQUIRE_FALSE(ffe::isCursorCaptured());
    ffe::setCursorCaptured(true);
    REQUIRE(ffe::isCursorCaptured());
    ffe::setCursorCaptured(false);
    REQUIRE_FALSE(ffe::isCursorCaptured());
}

TEST_CASE("Shutdown and re-init clears all state", "[input][headless]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::W);
    ffe::updateInput();
    REQUIRE(ffe::isKeyPressed(ffe::Key::W));

    ffe::shutdownInput();
    ffe::initInput(nullptr);

    ffe::updateInput();
    REQUIRE(ffe::isKeyUp(ffe::Key::W));
}

// =============================================================================
// Action capacity exhaustion
// =============================================================================

TEST_CASE("Register MAX_ACTIONS actions - all succeed", "[input][actions][capacity]") {
    InputFixture fix;

    for (ffe::i32 i = 0; i < ffe::MAX_ACTIONS; ++i) {
        // Use a fixed-size buffer; names are just "a0".."a63"
        char name[8];
        // Build name manually to avoid sprintf (not needed in hot path anyway,
        // but we follow the no-hidden-heap-in-hot-paths rule for clarity)
        name[0] = 'a';
        name[1] = static_cast<char>('0' + (i / 10));
        name[2] = static_cast<char>('0' + (i % 10));
        name[3] = '\0';
        const ffe::i32 idx = ffe::registerAction(name);
        REQUIRE(idx >= 0);
    }
}

TEST_CASE("Registering a 65th action returns -1, does not crash", "[input][actions][capacity]") {
    InputFixture fix;

    // Fill all MAX_ACTIONS slots
    for (ffe::i32 i = 0; i < ffe::MAX_ACTIONS; ++i) {
        char name[8];
        name[0] = 'a';
        name[1] = static_cast<char>('0' + (i / 10));
        name[2] = static_cast<char>('0' + (i % 10));
        name[3] = '\0';
        ffe::registerAction(name);
    }

    // One too many
    const ffe::i32 overflow = ffe::registerAction("overflow");
    REQUIRE(overflow == -1);
}

TEST_CASE("Bind MAX_BINDINGS_PER_ACTION keys to one action - all succeed", "[input][actions][capacity]") {
    InputFixture fix;

    const ffe::i32 action = ffe::registerAction("multi");
    REQUIRE(action >= 0);

    // Use four distinct keys
    REQUIRE(ffe::bindActionKey(action, ffe::Key::A));
    REQUIRE(ffe::bindActionKey(action, ffe::Key::B));
    REQUIRE(ffe::bindActionKey(action, ffe::Key::C));
    REQUIRE(ffe::bindActionKey(action, ffe::Key::D));
}

TEST_CASE("Binding a 5th key to the same action returns false, does not crash", "[input][actions][capacity]") {
    InputFixture fix;

    const ffe::i32 action = ffe::registerAction("multi_overflow");
    REQUIRE(action >= 0);

    REQUIRE(ffe::bindActionKey(action, ffe::Key::A));
    REQUIRE(ffe::bindActionKey(action, ffe::Key::B));
    REQUIRE(ffe::bindActionKey(action, ffe::Key::C));
    REQUIRE(ffe::bindActionKey(action, ffe::Key::D));

    // Fifth binding must fail
    REQUIRE_FALSE(ffe::bindActionKey(action, ffe::Key::E));
}

// =============================================================================
// Right modifier keys
// =============================================================================

TEST_CASE("RIGHT_CTRL triggers isCtrlDown", "[input][keyboard][modifiers]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::RIGHT_CTRL);
    ffe::updateInput();
    REQUIRE(ffe::isCtrlDown());

    ffe::test::simulateKeyRelease(ffe::Key::RIGHT_CTRL);
    ffe::updateInput();
    REQUIRE_FALSE(ffe::isCtrlDown());
}

TEST_CASE("RIGHT_ALT triggers isAltDown", "[input][keyboard][modifiers]") {
    InputFixture fix;

    ffe::test::simulateKeyPress(ffe::Key::RIGHT_ALT);
    ffe::updateInput();
    REQUIRE(ffe::isAltDown());

    ffe::test::simulateKeyRelease(ffe::Key::RIGHT_ALT);
    ffe::updateInput();
    REQUIRE_FALSE(ffe::isAltDown());
}

// Note: RIGHT_SHIFT coverage already exists in "Modifier convenience functions"
// above which explicitly presses RIGHT_SHIFT and checks isShiftDown(). No
// duplicate needed here.
