#pragma once
#ifdef FFE_EDITOR

#include "core/types.h"

struct GLFWwindow;

namespace ffe {
class World;
}

namespace ffe::editor {

class EditorOverlay {
public:
    void init(GLFWwindow* window);
    void shutdown();
    void beginFrame();
    void render(World& world);
    void toggle();
    bool isVisible() const;
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // HUD text — always rendered, even when inspector panels are hidden.
    // Reads from HudTextBuffer in ECS context each frame.
    void setShowHud(bool show);

private:
    void drawPerformancePanel(World& world);
    void drawEntityInspector(World& world);
    void drawConsolePanel();

    bool m_visible = false;
    bool m_initialised = false;
    float m_fpsAccum = 0.0f;
    int m_fpsFrameCount = 0;
    float m_displayFps = 0.0f;
    float m_displayFrameTime = 0.0f;
    u32 m_selectedEntity = 0;
    bool m_hasSelection = false;
    bool m_showHud = true;
};

} // namespace ffe::editor

#endif // FFE_EDITOR
