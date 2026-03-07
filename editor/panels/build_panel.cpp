#include "panels/build_panel.h"

#include <imgui.h>

#include <cstring>

namespace ffe::editor {

void BuildPanel::draw() {
    ImGui::Begin("Build / Export");

    // --- Project Settings ---
    ImGui::SeparatorText("Project Settings");
    ImGui::InputText("Project Name", m_config.projectName,
                     sizeof(m_config.projectName));
    ImGui::InputText("Entry Scene", m_config.entryScene,
                     sizeof(m_config.entryScene));
    ImGui::InputText("Output Directory", m_config.outputDir,
                     sizeof(m_config.outputDir));
    ImGui::InputText("Runtime Binary", m_runtimePath,
                     sizeof(m_runtimePath));

    // --- Asset Directories ---
    ImGui::SeparatorText("Asset Directories");
    drawDirectoryList("Asset", m_config.assetDirs, 4, m_config.assetDirCount);

    // --- Script Directories ---
    ImGui::SeparatorText("Script Directories");
    drawDirectoryList("Script", m_config.scriptDirs, 2, m_config.scriptDirCount);

    // --- Save / Load Config ---
    ImGui::SeparatorText("Configuration");
    if (ImGui::Button("Save Config")) {
        saveBuildConfig(m_config, "build_config.json");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Config")) {
        loadBuildConfig(m_config, "build_config.json");
    }

    ImGui::Separator();

    // --- Export Button ---
    if (ImGui::Button("Export Game", ImVec2(ImGui::GetContentRegionAvail().x, 40.0f))) {
        m_lastResult = exportGame(m_config, m_runtimePath);
        m_hasExported = true;
    }

    // --- Status Display ---
    if (m_hasExported) {
        ImGui::Spacing();
        if (m_lastResult.success) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Export succeeded!");
            ImGui::TextWrapped("Output: %s", m_lastResult.outputPath.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Export failed!");
            ImGui::TextWrapped("Error: %s", m_lastResult.errorMessage.c_str());
        }
    }

    ImGui::End();
}

void BuildPanel::drawDirectoryList(const char* label, char dirs[][256],
                                   const int maxCount, int& count) {
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        char inputLabel[64];
        std::snprintf(inputLabel, sizeof(inputLabel), "%s Dir %d", label, i + 1);
        ImGui::InputText(inputLabel, dirs[i], 256);

        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            // Shift remaining entries down
            for (int j = i; j < count - 1; ++j) {
                std::strncpy(dirs[j], dirs[j + 1], 256);
                dirs[j][255] = '\0';
            }
            // Clear the last slot
            std::memset(dirs[count - 1], 0, 256);
            --count;
            ImGui::PopID();
            // Skip the rest of this iteration since we shifted
            continue;
        }
        ImGui::PopID();
    }

    if (count < maxCount) {
        char addLabel[64];
        std::snprintf(addLabel, sizeof(addLabel), "Add %s Dir", label);
        if (ImGui::SmallButton(addLabel)) {
            std::memset(dirs[count], 0, 256);
            ++count;
        }
    }
}

} // namespace ffe::editor
