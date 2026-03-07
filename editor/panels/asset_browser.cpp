#include "panels/asset_browser.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace ffe::editor {

namespace fs = std::filesystem;

void AssetBrowser::init(const std::string& projectRoot) {
    std::error_code ec;
    auto resolved = fs::canonical(projectRoot, ec);
    if (ec || !fs::is_directory(resolved, ec)) {
        resolved = fs::current_path(ec);
    }
    m_projectRoot = resolved.string();
    m_currentDir  = m_projectRoot;
    m_selectedAsset.clear();

    refreshListing();
}

void AssetBrowser::render() {
    ImGui::Begin("Asset Browser");

    if (m_projectRoot.empty()) {
        ImGui::TextDisabled("No project root set. Call init() first.");
        ImGui::End();
        return;
    }

    // Current directory display
    ImGui::TextWrapped("Path: %s", m_currentDir.c_str());
    ImGui::Separator();

    // Refresh button
    if (ImGui::SmallButton("Refresh")) {
        refreshListing();
    }
    ImGui::Separator();

    // Scrollable child region
    ImGui::BeginChild("AssetList", ImVec2(0, 0), ImGuiChildFlags_Borders);

    // ".." to navigate up (only if not at project root)
    if (m_currentDir != m_projectRoot) {
        if (ImGui::Selectable("../", false)) {
            std::error_code ec;
            const auto parent = fs::path(m_currentDir).parent_path();
            const auto parentStr = fs::canonical(parent, ec).string();
            if (!ec && isWithinProjectRoot(parentStr)) {
                m_currentDir = parentStr;
                m_selectedAsset.clear();
                refreshListing();
            }
        }
    }

    // Directories first
    for (const auto& dir : m_directories) {
        char label[512] = {};
        std::snprintf(label, sizeof(label), "[D] %s/", dir.c_str());
        if (ImGui::Selectable(label, false)) {
            std::error_code ec;
            const auto newPath = fs::canonical(fs::path(m_currentDir) / dir, ec);
            if (!ec && isWithinProjectRoot(newPath.string())) {
                m_currentDir = newPath.string();
                m_selectedAsset.clear();
                refreshListing();
            }
        }
    }

    // Files with type indicators
    for (const auto& file : m_files) {
        const auto fullPath = (fs::path(m_currentDir) / file).string();
        const bool isSelected = (m_selectedAsset == fullPath);

        char label[512] = {};
        const char* prefix = fileTypePrefix(file);
        std::snprintf(label, sizeof(label), "%s %s", prefix, file.c_str());

        if (ImGui::Selectable(label, isSelected)) {
            m_selectedAsset = fullPath;
        }

        // Drag-drop source: allow dragging files from the asset browser
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            // Payload is the full file path as a null-terminated string
            ImGui::SetDragDropPayload("ASSET_PATH", fullPath.c_str(),
                                      fullPath.size() + 1);
            // Tooltip showing the filename while dragging
            ImGui::Text("%s %s", prefix, file.c_str());
            ImGui::EndDragDropSource();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

const std::string& AssetBrowser::selectedAsset() const {
    return m_selectedAsset;
}

void AssetBrowser::refreshListing() {
    m_directories.clear();
    m_files.clear();

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(m_currentDir, ec)) {
        if (ec) {
            break;
        }

        const auto filename = entry.path().filename().string();

        // Skip hidden files/directories (starting with '.')
        if (!filename.empty() && filename[0] == '.') {
            continue;
        }

        if (entry.is_directory(ec)) {
            m_directories.push_back(filename);
        } else if (entry.is_regular_file(ec)) {
            m_files.push_back(filename);
        }
    }

    // Sort alphabetically (case-insensitive)
    const auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](const char ca, const char cb) {
                return std::tolower(static_cast<unsigned char>(ca)) <
                       std::tolower(static_cast<unsigned char>(cb));
            });
    };

    std::sort(m_directories.begin(), m_directories.end(), caseInsensitiveLess);
    std::sort(m_files.begin(), m_files.end(), caseInsensitiveLess);
}

bool AssetBrowser::isWithinProjectRoot(const std::string& path) const {
    if (path.size() < m_projectRoot.size()) {
        return false;
    }
    return path.compare(0, m_projectRoot.size(), m_projectRoot) == 0;
}

const char* AssetBrowser::fileTypePrefix(const std::string& filename) {
    // Find the extension (last '.' in the filename)
    const auto dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) {
        return "[   ]";
    }

    // Extract extension and compare (case-insensitive via lowercase)
    auto ext = filename.substr(dotPos);
    for (auto& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
        return "[IMG]";
    }
    if (ext == ".glb" || ext == ".gltf" || ext == ".obj" || ext == ".fbx") {
        return "[MESH]";
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
        return "[SND]";
    }
    if (ext == ".lua") {
        return "[LUA]";
    }
    if (ext == ".json") {
        return "[JSON]";
    }
    if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".c") {
        return "[SRC]";
    }
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl") {
        return "[SHDR]";
    }

    return "[   ]";
}

} // namespace ffe::editor
