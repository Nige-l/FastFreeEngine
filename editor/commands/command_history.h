#pragma once

#include "commands/command.h"

#include <memory>
#include <vector>

namespace ffe::editor {

// Bounded undo/redo stack using the command pattern.
// Maximum depth of 256 entries — oldest commands are dropped when full.
// This is editor cold-path code; std::vector with reserve is fine.
class CommandHistory {
public:
    static constexpr uint32_t MAX_UNDO_DEPTH = 256;

    CommandHistory();

    // Execute a command and push it onto the undo stack.
    // Clears the redo stack (divergent timeline).
    void executeCommand(std::unique_ptr<Command> cmd);

    // Undo the most recent command. No-op if undo stack is empty.
    void undo();

    // Redo the most recently undone command. No-op if redo stack is empty.
    void redo();

    bool canUndo() const;
    bool canRedo() const;

    // Description of the command that would be undone/redone (for menu display).
    // Returns nullptr if the respective stack is empty.
    const char* undoDescription() const;
    const char* redoDescription() const;

    // Number of commands on each stack (useful for testing).
    uint32_t undoCount() const;
    uint32_t redoCount() const;

private:
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
};

} // namespace ffe::editor
