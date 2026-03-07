#include "input/shortcut_manager.h"

#include <imgui.h>

namespace ffe::editor {

void ShortcutManager::update() {
    // Clear previous frame's triggers
    for (int i = 0; i < m_bindingCount; ++i) {
        m_triggered[i] = false;
    }

    const auto& io = ImGui::GetIO();

    // Suppress all shortcuts while a text field has focus
    if (io.WantCaptureKeyboard) {
        return;
    }

    for (int i = 0; i < m_bindingCount; ++i) {
        const auto& b = m_bindings[i].shortcut;

        // Check modifier state
        const bool ctrlHeld  = io.KeyCtrl;
        const bool shiftHeld = io.KeyShift;
        const bool altHeld   = io.KeyAlt;

        if (b.ctrl  != ctrlHeld)  continue;
        if (b.shift != shiftHeld) continue;
        if (b.alt   != altHeld)   continue;

        // Check that the key was pressed this frame (not just held)
        if (ImGui::IsKeyPressed(b.key, false)) {
            m_triggered[i] = true;
        }
    }
}

} // namespace ffe::editor
