#pragma once

#include <string>
#include <vector>

namespace ffe::editor {

enum class FileDialogMode { OPEN, SAVE };

// ImGui-based file dialog for Open/Save Scene.
// Cross-platform — no native OS dialog dependency.
// Filters to .json scene files only.
// Security: prevents navigating above the project root.
class FileDialog {
public:
    // Open the dialog in the given mode, starting at startPath.
    void open(FileDialogMode mode, const std::string& startPath = ".");

    // Render the dialog (call each frame when open).
    // Returns true when the user confirms a selection.
    bool render();

    // Get the selected file path after render() returns true.
    const std::string& selectedPath() const;

    // Is the dialog currently open?
    bool isOpen() const;

    // Close without selection.
    void close();

private:
    bool m_open = false;
    FileDialogMode m_mode = FileDialogMode::OPEN;
    std::string m_currentDir;
    std::string m_projectRoot;   // Cannot navigate above this
    std::string m_selectedPath;
    char m_filename[256] = {};   // For save mode: filename input buffer
    std::vector<std::string> m_directories;
    std::vector<std::string> m_files;

    void refreshListing();
    bool isWithinProjectRoot(const std::string& path) const;
};

} // namespace ffe::editor
