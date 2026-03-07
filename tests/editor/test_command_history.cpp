// test_command_history.cpp — Unit tests for the editor command history (undo/redo).
//
// Tests execute/undo/redo sequencing, bounded stack overflow,
// redo stack clearing on new command, and empty stack no-ops.
// All tests run headless — no GL context or ImGui required.

#include <catch2/catch_test_macros.hpp>

#include "editor/commands/command.h"
#include "editor/commands/command_history.h"

#include <memory>

using namespace ffe::editor;

// A simple test command that increments/decrements a counter.
class TestCounterCommand : public Command {
public:
    explicit TestCounterCommand(int& counter, int delta = 1)
        : m_counter(counter), m_delta(delta) {}

    void execute() override { m_counter += m_delta; }
    void undo() override { m_counter -= m_delta; }
    const char* description() const override { return "TestCounter"; }

private:
    int& m_counter;
    int m_delta;
};

// -----------------------------------------------------------------------
// Basic execute/undo/redo
// -----------------------------------------------------------------------

TEST_CASE("CommandHistory execute pushes to undo stack", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    REQUIRE_FALSE(history.canUndo());
    REQUIRE_FALSE(history.canRedo());
    REQUIRE(history.undoCount() == 0);

    history.executeCommand(std::make_unique<TestCounterCommand>(counter));

    CHECK(counter == 1);
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoCount() == 1);
    CHECK(history.redoCount() == 0);
}

TEST_CASE("CommandHistory undo reverses a command", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    history.executeCommand(std::make_unique<TestCounterCommand>(counter));
    CHECK(counter == 1);

    history.undo();
    CHECK(counter == 0);
    CHECK_FALSE(history.canUndo());
    CHECK(history.canRedo());
    CHECK(history.undoCount() == 0);
    CHECK(history.redoCount() == 1);
}

TEST_CASE("CommandHistory redo re-applies an undone command", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    history.executeCommand(std::make_unique<TestCounterCommand>(counter));
    history.undo();
    CHECK(counter == 0);

    history.redo();
    CHECK(counter == 1);
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("CommandHistory multiple undo/redo", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 10));
    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 20));
    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 30));
    CHECK(counter == 60);
    CHECK(history.undoCount() == 3);

    history.undo(); // undo +30
    CHECK(counter == 30);
    history.undo(); // undo +20
    CHECK(counter == 10);

    CHECK(history.undoCount() == 1);
    CHECK(history.redoCount() == 2);

    history.redo(); // redo +20
    CHECK(counter == 30);
    history.redo(); // redo +30
    CHECK(counter == 60);

    CHECK(history.undoCount() == 3);
    CHECK(history.redoCount() == 0);
}

// -----------------------------------------------------------------------
// New command clears redo stack
// -----------------------------------------------------------------------

TEST_CASE("CommandHistory new command after undo clears redo stack", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 10));
    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 20));
    history.undo(); // counter = 10, redo has +20
    CHECK(history.canRedo());

    // Execute a new command — redo stack should be cleared (divergent timeline)
    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 5));
    CHECK(counter == 15);
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoCount() == 2); // +10, +5
}

// -----------------------------------------------------------------------
// Bounded stack overflow
// -----------------------------------------------------------------------

TEST_CASE("CommandHistory bounded stack drops oldest on overflow", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    // Fill the stack to MAX_UNDO_DEPTH
    for (uint32_t i = 0; i < CommandHistory::MAX_UNDO_DEPTH; ++i) {
        history.executeCommand(std::make_unique<TestCounterCommand>(counter, 1));
    }
    CHECK(history.undoCount() == CommandHistory::MAX_UNDO_DEPTH);
    CHECK(counter == static_cast<int>(CommandHistory::MAX_UNDO_DEPTH));

    // One more should drop the oldest
    history.executeCommand(std::make_unique<TestCounterCommand>(counter, 100));
    CHECK(history.undoCount() == CommandHistory::MAX_UNDO_DEPTH);
    CHECK(counter == static_cast<int>(CommandHistory::MAX_UNDO_DEPTH) + 100);

    // Undo all — counter should NOT go to 0 because the first +1 was dropped
    for (uint32_t i = 0; i < CommandHistory::MAX_UNDO_DEPTH; ++i) {
        history.undo();
    }
    CHECK_FALSE(history.canUndo());
    // Lost the first +1, so counter should be 1
    CHECK(counter == 1);
}

// -----------------------------------------------------------------------
// Empty stack no-ops
// -----------------------------------------------------------------------

TEST_CASE("CommandHistory undo on empty stack is no-op", "[editor][command_history]") {
    CommandHistory history;
    // Should not crash
    history.undo();
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("CommandHistory redo on empty stack is no-op", "[editor][command_history]") {
    CommandHistory history;
    // Should not crash
    history.redo();
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

// -----------------------------------------------------------------------
// Description accessors
// -----------------------------------------------------------------------

TEST_CASE("CommandHistory descriptions return correct names", "[editor][command_history]") {
    CommandHistory history;
    int counter = 0;

    CHECK(history.undoDescription() == nullptr);
    CHECK(history.redoDescription() == nullptr);

    history.executeCommand(std::make_unique<TestCounterCommand>(counter));
    CHECK(std::string(history.undoDescription()) == "TestCounter");
    CHECK(history.redoDescription() == nullptr);

    history.undo();
    CHECK(history.undoDescription() == nullptr);
    CHECK(std::string(history.redoDescription()) == "TestCounter");
}

// -----------------------------------------------------------------------
// Null command is ignored
// -----------------------------------------------------------------------

TEST_CASE("CommandHistory ignores null command", "[editor][command_history]") {
    CommandHistory history;

    history.executeCommand(nullptr);
    CHECK_FALSE(history.canUndo());
    CHECK(history.undoCount() == 0);
}
