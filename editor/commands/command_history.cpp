#include "commands/command_history.h"

namespace ffe::editor {

CommandHistory::CommandHistory() {
    m_undoStack.reserve(MAX_UNDO_DEPTH);
    m_redoStack.reserve(MAX_UNDO_DEPTH);
}

void CommandHistory::executeCommand(std::unique_ptr<Command> cmd) {
    if (!cmd) return;

    cmd->execute();

    // If undo stack is full, drop the oldest command
    if (m_undoStack.size() >= MAX_UNDO_DEPTH) {
        m_undoStack.erase(m_undoStack.begin());
    }

    m_undoStack.push_back(std::move(cmd));

    // Divergent timeline — clear redo stack
    m_redoStack.clear();
}

void CommandHistory::undo() {
    if (m_undoStack.empty()) return;

    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    cmd->undo();

    m_redoStack.push_back(std::move(cmd));
}

void CommandHistory::redo() {
    if (m_redoStack.empty()) return;

    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    cmd->execute();

    m_undoStack.push_back(std::move(cmd));
}

bool CommandHistory::canUndo() const {
    return !m_undoStack.empty();
}

bool CommandHistory::canRedo() const {
    return !m_redoStack.empty();
}

const char* CommandHistory::undoDescription() const {
    if (m_undoStack.empty()) return nullptr;
    return m_undoStack.back()->description();
}

const char* CommandHistory::redoDescription() const {
    if (m_redoStack.empty()) return nullptr;
    return m_redoStack.back()->description();
}

uint32_t CommandHistory::undoCount() const {
    return static_cast<uint32_t>(m_undoStack.size());
}

uint32_t CommandHistory::redoCount() const {
    return static_cast<uint32_t>(m_redoStack.size());
}

} // namespace ffe::editor
