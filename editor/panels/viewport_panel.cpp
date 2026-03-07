#include "panels/viewport_panel.h"

#include <imgui.h>

namespace ffe::editor_app::viewport_panel {

void render() {
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Viewport")) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();

        // Placeholder: display centered text indicating where the viewport will render.
        // Future sessions will add FBO-based rendering here via ImGui::Image().
        const char* label = "Scene Viewport";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 cursorPos(
            (avail.x - textSize.x) * 0.5f,
            (avail.y - textSize.y) * 0.5f
        );
        ImGui::SetCursorPos(ImVec2(
            ImGui::GetCursorPosX() + cursorPos.x,
            ImGui::GetCursorPosY() + cursorPos.y
        ));
        ImGui::TextDisabled("%s", label);
    }
    ImGui::End();
}

} // namespace ffe::editor_app::viewport_panel
