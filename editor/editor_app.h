#pragma once

#include "core/types.h"
#include "commands/command_history.h"
#include "input/shortcut_manager.h"
#include "panels/file_dialog.h"
#include "panels/viewport_panel.h"
#include "play_mode.h"

#include <memory>
#include <string>

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

    // Play-in-editor
    ffe::editor::PlayMode m_playMode;

    // Undo / redo
    ffe::editor::CommandHistory m_commandHistory;

    // Keyboard shortcuts
    ffe::editor::ShortcutManager m_shortcuts;

    // Panels
    ViewportPanel m_viewport;
    ffe::editor::FileDialog m_fileDialog;

    // Panel visibility
    bool m_showViewport = true;

    // Scene tracking
    std::string m_currentScenePath;
    ffe::editor::FileDialogMode m_pendingDialogMode = ffe::editor::FileDialogMode::OPEN;

    // Selected entity
    EntityId m_selectedEntity = NULL_ENTITY;
};

} // namespace ffe::editor_app
