#include "panels/file_dialog.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace ffe::editor {

namespace fs = std::filesystem;

void FileDialog::open(FileDialogMode mode, const std::string& startPath) {
    m_mode = mode;
    m_open = true;
    m_selectedPath.clear();
    std::memset(m_filename, 0, sizeof(m_filename));

    // Resolve the starting directory to an absolute canonical path.
    std::error_code ec;
    auto resolved = fs::canonical(startPath, ec);
    if (ec || !fs::is_directory(resolved, ec)) {
        resolved = fs::current_path(ec);
    }
    m_currentDir = resolved.string();

    // Project root is the starting directory (or cwd).
    // The dialog will not allow navigating above this.
    m_projectRoot = m_currentDir;

    refreshListing();
    ImGui::OpenPopup("FileDialog");
}

bool FileDialog::render() {
    if (!m_open) {
        return false;
    }

    bool confirmed = false;
    const char* title = (m_mode == FileDialogMode::OPEN) ? "Open Scene" : "Save Scene";

    ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("FileDialog", &m_open, ImGuiWindowFlags_NoCollapse)) {
        // Title
        ImGui::TextUnformatted(title);
        ImGui::Separator();

        // Current path display
        ImGui::TextWrapped("Path: %s", m_currentDir.c_str());
        ImGui::Separator();

        // Scrollable child region for directory listing
        const float footerHeight = (m_mode == FileDialogMode::SAVE) ? 70.0f : 40.0f;
        ImGui::BeginChild("FileList", ImVec2(0, -footerHeight), ImGuiChildFlags_Borders);

        // ".." entry to go up one directory (only if not at project root)
        if (m_currentDir != m_projectRoot) {
            if (ImGui::Selectable("../", false)) {
                std::error_code ec;
                auto parent = fs::path(m_currentDir).parent_path();
                auto parentStr = fs::canonical(parent, ec).string();
                if (!ec && isWithinProjectRoot(parentStr)) {
                    m_currentDir = parentStr;
                    refreshListing();
                }
            }
        }

        // Directories
        for (const auto& dir : m_directories) {
            char label[512] = {};
            std::snprintf(label, sizeof(label), "[Dir] %s/", dir.c_str());
            if (ImGui::Selectable(label, false)) {
                std::error_code ec;
                auto newPath = fs::canonical(fs::path(m_currentDir) / dir, ec);
                if (!ec && isWithinProjectRoot(newPath.string())) {
                    m_currentDir = newPath.string();
                    refreshListing();
                }
            }
        }

        // Files (.json only)
        for (const auto& file : m_files) {
            const bool isSelected = (m_selectedPath == (fs::path(m_currentDir) / file).string());
            if (ImGui::Selectable(file.c_str(), isSelected)) {
                m_selectedPath = (fs::path(m_currentDir) / file).string();
                if (m_mode == FileDialogMode::SAVE) {
                    std::snprintf(m_filename, sizeof(m_filename), "%s", file.c_str());
                }
            }
        }

        ImGui::EndChild();

        // Save mode: filename input
        if (m_mode == FileDialogMode::SAVE) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##Filename", m_filename, sizeof(m_filename));
        }

        // OK / Cancel buttons
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (m_mode == FileDialogMode::OPEN) {
                // Must have a file selected
                if (!m_selectedPath.empty()) {
                    confirmed = true;
                    m_open = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                // Save mode: build path from filename input
                if (m_filename[0] != '\0') {
                    std::string fname(m_filename);
                    // Append .json if not already present
                    if (fname.size() < 5 || fname.substr(fname.size() - 5) != ".json") {
                        fname += ".json";
                    }
                    m_selectedPath = (fs::path(m_currentDir) / fname).string();
                    confirmed = true;
                    m_open = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_selectedPath.clear();
            m_open = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // If the modal was closed via the X button
    if (!m_open) {
        m_selectedPath.clear();
    }

    return confirmed;
}

const std::string& FileDialog::selectedPath() const {
    return m_selectedPath;
}

bool FileDialog::isOpen() const {
    return m_open;
}

void FileDialog::close() {
    m_open = false;
    m_selectedPath.clear();
}

void FileDialog::refreshListing() {
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
            // Filter: only .json files
            if (entry.path().extension() == ".json") {
                m_files.push_back(filename);
            }
        }
    }

    // Sort alphabetically (case-insensitive)
    const auto caseInsensitiveLess = [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](char ca, char cb) { return std::tolower(static_cast<unsigned char>(ca)) <
                                          std::tolower(static_cast<unsigned char>(cb)); });
    };

    std::sort(m_directories.begin(), m_directories.end(), caseInsensitiveLess);
    std::sort(m_files.begin(), m_files.end(), caseInsensitiveLess);
}

bool FileDialog::isWithinProjectRoot(const std::string& path) const {
    // Ensure the path starts with the project root.
    // This prevents navigating above the directory where the dialog was opened.
    if (path.size() < m_projectRoot.size()) {
        return false;
    }
    return path.compare(0, m_projectRoot.size(), m_projectRoot) == 0;
}

} // namespace ffe::editor
