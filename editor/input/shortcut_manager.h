#pragma once

#include <cstdint>
#include <cstring>

namespace ffe::editor {

// Keyboard shortcut definition. Key codes follow GLFW conventions
// (e.g. GLFW_KEY_Z = 90, GLFW_KEY_S = 83, GLFW_KEY_DELETE = 261).
struct Shortcut {
    int key        = 0;
    bool ctrl      = false;
    bool shift     = false;
    bool alt       = false;
};

// ShortcutManager — maps keyboard shortcuts to named editor actions.
//
// Call update() once per frame to poll ImGui key state. Then call
// triggered("action_name") to check whether a shortcut fired this frame.
// Shortcuts are suppressed when ImGui wants keyboard input (text fields).
//
// Fixed-capacity (32 bindings max). No heap allocation. Suitable for
// editor cold-path use.
//
// Registration and query methods are inline (no ImGui dependency).
// Only update() requires ImGui and lives in shortcut_manager.cpp.
class ShortcutManager {
public:
    // Poll ImGui IO state and mark any triggered bindings.
    // Must be called once per frame before any triggered() queries.
    // Implemented in shortcut_manager.cpp (requires ImGui).
    void update();

    // Returns true if the named action's shortcut fired this frame.
    inline bool triggered(const char* action) const {
        for (int i = 0; i < m_bindingCount; ++i) {
            if (m_triggered[i] && std::strcmp(m_bindings[i].action, action) == 0) {
                return true;
            }
        }
        return false;
    }

    // Register all default editor shortcuts. Call once at startup.
    inline void registerDefaults() {
        // GLFW key codes (stable since GLFW 3.0)
        constexpr int KEY_S      = 83;
        constexpr int KEY_Z      = 90;
        constexpr int KEY_G      = 71;
        constexpr int KEY_R      = 82;
        constexpr int KEY_DELETE = 261;

        addBinding("undo",             KEY_Z,      /*ctrl=*/true,  /*shift=*/false, /*alt=*/false);
        addBinding("redo",             KEY_Z,      /*ctrl=*/true,  /*shift=*/true,  /*alt=*/false);
        addBinding("save",             KEY_S,      /*ctrl=*/true,  /*shift=*/false, /*alt=*/false);
        addBinding("delete_entity",    KEY_DELETE,  /*ctrl=*/false, /*shift=*/false, /*alt=*/false);
        addBinding("gizmo_translate",  KEY_G,      /*ctrl=*/false, /*shift=*/false, /*alt=*/false);
        addBinding("gizmo_rotate",     KEY_R,      /*ctrl=*/false, /*shift=*/false, /*alt=*/false);
        addBinding("gizmo_scale",      KEY_S,      /*ctrl=*/false, /*shift=*/false, /*alt=*/false);
    }

    // Register a custom shortcut binding.
    inline void addBinding(const char* action, const int key,
                           const bool ctrl = false, const bool shift = false,
                           const bool alt = false) {
        if (m_bindingCount >= MAX_BINDINGS) return;

        auto& binding      = m_bindings[m_bindingCount];
        binding.shortcut   = {key, ctrl, shift, alt};
        binding.action     = action;
        ++m_bindingCount;
    }

    // Number of registered bindings (useful for testing).
    int bindingCount() const { return m_bindingCount; }

private:
    struct Binding {
        Shortcut shortcut;
        const char* action = nullptr;
    };

    static constexpr int MAX_BINDINGS = 32;
    Binding m_bindings[MAX_BINDINGS] = {};
    int m_bindingCount = 0;
    bool m_triggered[MAX_BINDINGS] = {};
};

} // namespace ffe::editor
