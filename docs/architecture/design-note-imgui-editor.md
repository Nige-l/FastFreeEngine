# Design Note: Dear ImGui Editor Overlay

**Status:** Proposed
**Author:** architect
**Date:** 2026-03-06
**Tier support:** LEGACY and above (OpenGL 3.0+ required; no RETRO support)

---

## 1. Integration Approach

Dear ImGui ships official GLFW and OpenGL 3.x backends (`imgui_impl_glfw.cpp`, `imgui_impl_opengl3.cpp`). FFE already uses GLFW + OpenGL 3.3 core profile, so these backends slot in with zero adaptation.

**Dependency:** vcpkg package `imgui[glfw-binding,opengl3-binding]`. Add to `vcpkg.json` and `find_package(imgui CONFIG REQUIRED)` in CMake. The backend sources compile as part of the engine library, not as a separate static lib.

---

## 2. Module Structure

```
engine/editor/
  editor.h / editor.cpp            -- EditorOverlay class
  editor_widgets.h / editor_widgets.cpp  -- panel implementations
```

`EditorOverlay` is owned by `Application` (new member `m_editorOverlay`). It is **not** part of the renderer. The renderer has no knowledge of ImGui.

Key API:

```cpp
namespace ffe::editor {

class EditorOverlay {
public:
    void init(GLFWwindow* window);   // Creates ImGui context, installs backends
    void shutdown();                  // Destroys ImGui context
    void beginFrame();                // ImGui_ImplOpenGL3_NewFrame + GLFW + ImGui::NewFrame
    void render(World& world);        // Draws panels, calls ImGui::Render + RenderDrawData
    void toggle();                    // Flip visibility
    bool isVisible() const;
    bool wantsMouse() const;          // Wraps ImGuiIO::WantCaptureMouse
    bool wantsKeyboard() const;       // Wraps ImGuiIO::WantCaptureKeyboard
};

} // namespace ffe::editor
```

---

## 3. Render Integration

The call sequence inside `Application::render()` becomes:

```
renderPrepareSystem(world, alpha)     // populate render queue
sortRenderQueue(m_renderQueue)
rhi::beginFrame(clearColor)
  ... sprite batch submission ...
#ifdef FFE_EDITOR
  m_editorOverlay.beginFrame()
  m_editorOverlay.render(m_world)     // ImGui draw + RenderDrawData
#endif
rhi::endFrame(m_window)              // swapBuffers
```

ImGui's OpenGL3 backend saves and restores all GL state it touches (blend, scissor, VAO, program, etc.), so it is safe to call after the sprite batch without corrupting engine state. ImGui draws on top of everything -- no depth test interaction.

---

## 4. Input Routing

**F1** toggles `EditorOverlay::toggle()`. This binding is checked directly in `Application::run()`, outside the normal input system, so it always works.

When the overlay is visible, `Application` checks `wantsMouse()` and `wantsKeyboard()` **after** `glfwPollEvents()` but **before** dispatching `updateInput()`. If ImGui claims the input, the engine's input system sees no events for that frame. When the overlay is hidden, ImGui's `NewFrame` is never called and all input flows to the game.

ImGui installs its own GLFW callbacks via `ImGui_ImplGlfw_InitForOpenGL(window, true)`. The `true` parameter tells ImGui to chain callbacks -- it calls the previously installed GLFW callbacks after processing, so FFE's existing window-close callback continues to work.

---

## 5. Build Integration

New CMake option:

```cmake
option(FFE_EDITOR "Build with Dear ImGui editor overlay" OFF)
if(FFE_EDITOR)
    find_package(imgui CONFIG REQUIRED)
    target_compile_definitions(ffe_engine PUBLIC FFE_EDITOR)
    # link imgui backends to ffe_engine
endif()
```

Default **OFF**. Enabled explicitly with `-DFFE_EDITOR=ON`. All editor code in `Application` is wrapped in `#ifdef FFE_EDITOR`. Release builds never compile or link ImGui.

Editor code is never in the hot path. `EditorOverlay::render()` only executes when the overlay is visible. When hidden, the cost is a single branch on a bool per frame.

---

## 6. MVP Panels

| Panel | Description |
|-------|-------------|
| **Performance overlay** | FPS (1-second rolling average), frame time (ms), entity count from `World`, active draw commands from `RenderQueue::count` |
| **Entity inspector** | Scrollable list of entity IDs. Select one to view/edit `Transform` fields (position, scale, rotation). Read-only for `Sprite` fields. |
| **Console** (stretch) | Ring buffer of recent `FFE_LOG` output. Requires a small logging hook to capture messages. Defer if it complicates the logging system. |

All panels are simple `ImGui::Begin`/`ImGui::End` floating windows. No docking, no layout persistence.

---

## 7. Scope Boundaries

This is a debug overlay, not an editor application. Explicitly out of scope:

- Scene serialization (save/load)
- Asset browser or file dialogs
- Undo/redo
- Dockable or persistent window layouts
- Gizmos (translate/rotate/scale handles)
- Any ImGui code running in headless or test builds

---

## 8. Security

ImGui processes keyboard and mouse input from GLFW, which is already trusted local input. No external data parsing, no file I/O, no network. No security review required for this integration.
