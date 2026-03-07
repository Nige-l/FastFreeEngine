// test_shortcuts.cpp — Unit tests for ShortcutManager.
//
// Tests shortcut registration and triggered() queries. Since these tests run
// headless (no ImGui context), we cannot test the full update() path that
// reads ImGui IO state. Instead we test the registration logic and verify that
// triggered() returns false when no keys are pressed (the default state).
// All tests run headless — no GL context or ImGui required.

#include <catch2/catch_test_macros.hpp>

#include "editor/input/shortcut_manager.h"

using namespace ffe::editor;

// -----------------------------------------------------------------------
// Registration
// -----------------------------------------------------------------------

TEST_CASE("ShortcutManager registerDefaults populates bindings", "[editor][shortcuts]") {
    ShortcutManager mgr;
    CHECK(mgr.bindingCount() == 0);

    mgr.registerDefaults();

    // 7 default bindings: undo, redo, save, delete_entity,
    // gizmo_translate, gizmo_rotate, gizmo_scale
    CHECK(mgr.bindingCount() == 7);
}

TEST_CASE("ShortcutManager addBinding increments count", "[editor][shortcuts]") {
    ShortcutManager mgr;
    mgr.addBinding("test_action", 65, true, false, false);
    CHECK(mgr.bindingCount() == 1);

    mgr.addBinding("another_action", 66, false, false, false);
    CHECK(mgr.bindingCount() == 2);
}

TEST_CASE("ShortcutManager addBinding respects MAX_BINDINGS", "[editor][shortcuts]") {
    ShortcutManager mgr;
    for (int i = 0; i < 32; ++i) {
        mgr.addBinding("action", 65 + i, false, false, false);
    }
    CHECK(mgr.bindingCount() == 32);

    // 33rd binding should be silently dropped
    mgr.addBinding("overflow", 100, false, false, false);
    CHECK(mgr.bindingCount() == 32);
}

// -----------------------------------------------------------------------
// Triggered queries (no ImGui context — always false)
// -----------------------------------------------------------------------

TEST_CASE("ShortcutManager triggered returns false with no update", "[editor][shortcuts]") {
    ShortcutManager mgr;
    mgr.registerDefaults();

    // Without calling update(), nothing should be triggered
    CHECK_FALSE(mgr.triggered("undo"));
    CHECK_FALSE(mgr.triggered("redo"));
    CHECK_FALSE(mgr.triggered("save"));
    CHECK_FALSE(mgr.triggered("delete_entity"));
    CHECK_FALSE(mgr.triggered("gizmo_translate"));
    CHECK_FALSE(mgr.triggered("gizmo_rotate"));
    CHECK_FALSE(mgr.triggered("gizmo_scale"));
}

TEST_CASE("ShortcutManager triggered returns false for unknown action", "[editor][shortcuts]") {
    ShortcutManager mgr;
    mgr.registerDefaults();
    CHECK_FALSE(mgr.triggered("nonexistent_action"));
}
