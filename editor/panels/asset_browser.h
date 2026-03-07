#pragma once

#include <string>
#include <vector>

namespace ffe::editor {

// Asset browser panel — a simple file browser for navigating project asset
// directories. Shows directories and files with type indicators. Click a
// directory to navigate into it; click a file to select it.
//
// Directory listing is scanned only on init() and on directory change, not
// per-frame. Uses <filesystem> for directory iteration (same pattern as
// FileDialog).
class AssetBrowser {
public:
    // Set the project root and navigate to it. Scans the directory.
    void init(const std::string& projectRoot);

    // Render the panel. Call once per frame inside an ImGui context.
    void render();

    // Get the last selected asset path (empty if none selected).
    const std::string& selectedAsset() const;

private:
    std::string m_projectRoot;
    std::string m_currentDir;
    std::string m_selectedAsset;
    std::vector<std::string> m_directories;
    std::vector<std::string> m_files;

    // Refresh the directory listing from m_currentDir.
    void refreshListing();

    // Returns true if the given path is at or below m_projectRoot.
    bool isWithinProjectRoot(const std::string& path) const;

    // Return a short file-type indicator prefix for display, based on extension.
    static const char* fileTypePrefix(const std::string& filename);
};

} // namespace ffe::editor
