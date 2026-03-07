#pragma once

#include <memory>

namespace ffe::editor {

// Base command interface for the undo/redo system.
// Virtual dispatch is acceptable here — commands execute on user interaction
// (mouse click, keyboard shortcut), not per-frame. See ADR Section 7.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual const char* description() const = 0;
};

} // namespace ffe::editor
