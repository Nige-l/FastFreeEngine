#pragma once

#include "core/types.h"

#include <memory>

struct GLFWwindow;

namespace ffe {
class Application;
}

namespace ffe::editor_app {

class EditorApp {
public:
    EditorApp();
    ~EditorApp();

    // Non-copyable, non-movable
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;
    EditorApp(EditorApp&&) = delete;
    EditorApp& operator=(EditorApp&&) = delete;

    // Lifecycle
    bool init();
    void run();
    void shutdown();

private:
    void renderMenuBar();
    void renderPanels();

    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Application> m_app;

    // Panel visibility
    bool m_showViewport = true;

    // Selected entity (for future inspector panel)
    u32  m_selectedEntity = 0;
    bool m_hasSelection = false;
};

} // namespace ffe::editor_app
