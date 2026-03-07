# ADR: Standalone Editor Architecture

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (OpenGL 3.3 core)
**Security Review Required:** YES — editor will load/save scene files (external input), and future phases introduce asset browsing and LLM integration

---

## 1. Problem Statement

FFE has a complete 2D+3D runtime (738 tests, ~115 Lua bindings, mesh loading, lighting, shadows, skybox, skeletal animation, physics, audio). Phase 2 is complete. To compete with mainstream engines, FFE needs a standalone graphical editor application — like Unity or Unreal Editor — where developers create scenes, inspect entities, manage assets, and test their games without writing boilerplate C++ or running from the command line.

This ADR defines the architecture for that editor. It covers the build structure, GUI framework, scene serialisation format, editor-engine boundary, undo/redo system, and directory layout. It is the specification that implementation agents will follow.

---

## 2. Scope

**In scope:**
- Editor as a separate CMake target and binary
- Dear ImGui as the GUI framework
- Scene serialisation to JSON (save/load)
- Editor-engine boundary (editor owns the loop, engine provides tick/render)
- Undo/redo via command pattern
- Directory structure for editor code and scene serialisation
- FBO-based viewport rendering
- First milestone: window + ImGui, scene save/load, basic entity inspector

**Out of scope (future milestones — architecture must not block these):**
- Asset browser panel
- Play-in-editor (run game inside editor viewport)
- Multi-viewport / split views
- Prefab system
- Build pipeline (export standalone executable)
- LLM integration panel
- Project creation wizard

---

## 3. Editor as Separate CMake Target

### Build Structure

The editor is a **separate executable**, not a mode of the game runtime. It links the same engine libraries that games link, but it owns its own `main()` and main loop.

```
CMakeLists.txt  (root — adds editor/ subdirectory)
editor/
  CMakeLists.txt  (defines ffe_editor executable)
  main.cpp
  ...
```

### CMake Configuration

```cmake
# editor/CMakeLists.txt
add_executable(ffe_editor
    main.cpp
    editor_app.cpp
    panels/scene_hierarchy_panel.cpp
    panels/inspector_panel.cpp
    panels/asset_browser_panel.cpp
    commands/command_history.cpp
)

target_link_libraries(ffe_editor PRIVATE
    ffe_engine          # full engine library (core + renderer + audio + physics + scripting)
    imgui::imgui        # Dear ImGui (already in vcpkg)
)

target_compile_definitions(ffe_editor PRIVATE FFE_EDITOR_APP=1)
```

The `FFE_EDITOR_APP` define distinguishes the standalone editor binary from the existing `FFE_EDITOR` debug overlay that ships inside the engine. The debug overlay (`engine/editor/`) continues to exist as an in-game tool. The standalone editor (`editor/`) is a separate application.

### Root CMakeLists.txt Addition

```cmake
option(FFE_BUILD_EDITOR "Build the standalone editor application" ON)

if(FFE_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
```

### Why a Separate Binary

- The editor has fundamentally different lifecycle than a game (no fixed game loop, no Lua script driving gameplay).
- Editor-specific dependencies (file dialogs, project management) do not belong in the game runtime.
- Games built with FFE should not ship editor code. Separate binary means zero editor overhead in the shipped game.
- The existing `engine/editor/` debug overlay remains as a lightweight in-game tool. The standalone editor is a different thing entirely.

---

## 4. GUI Framework: Dear ImGui

### Decision

Use **Dear ImGui** (immediate-mode GUI) for all editor UI.

### Justification

| Criterion | Dear ImGui | Alternative: Qt | Alternative: Custom retained-mode |
|-----------|-----------|-----------------|----------------------------------|
| Already in project | YES — vcpkg dep, used for debug overlay | No — massive new dependency | No — months of work |
| OpenGL 3.3 compatible | YES — `imgui_impl_opengl3` with GLSL 330 | Yes, but heavy | Depends on implementation |
| Hidden allocations | None in hot path (arena-style internal buffer) | Many (signal/slot, QObject heap) | Depends |
| Learning curve for contributors | Low — widely known | High — Qt-specific patterns | High — custom API |
| Docking/multi-panel | YES — `imgui_docking` branch (available in vcpkg) | Built-in | Must build from scratch |
| Performance on LEGACY hardware | Excellent — trivial vertex count for UI | Heavy — full widget toolkit | Depends |

Dear ImGui is the clear choice. It is already a dependency, it runs on our target hardware, it has no hidden allocations, and its docking branch provides the multi-panel layout an editor needs. The debug overlay already proves it works in our OpenGL 3.3 pipeline.

### ImGui Docking

The editor will use ImGui's docking API (`ImGui::DockSpaceOverViewport()`) to allow panels to be docked, undocked, and rearranged. This is available in the imgui vcpkg port with the `docking-experimental` feature. If the vcpkg port does not include docking, we fall back to fixed panel layout for the first milestone and add docking later.

---

## 5. Scene Serialisation Format

### Format: JSON via nlohmann-json

nlohmann-json is already a vcpkg dependency used for save/load (`ffe.saveData` / `ffe.loadData`). JSON is human-readable, diffable in version control, and easy to debug. Binary formats (FlatBuffers, MessagePack) are future optimisations if scene files become large.

### Schema

```json
{
  "version": 1,
  "name": "My Scene",
  "entities": [
    {
      "id": 42,
      "name": "Player",
      "parent": null,
      "components": {
        "Transform": {
          "x": 100.0, "y": 200.0,
          "scaleX": 1.0, "scaleY": 1.0,
          "rotation": 0.0
        },
        "Sprite": {
          "texture": "assets/player.png",
          "width": 32, "height": 32,
          "r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0,
          "flipX": false, "flipY": false
        }
      }
    },
    {
      "id": 43,
      "name": "Ground",
      "parent": null,
      "components": {
        "Transform3D": {
          "x": 0.0, "y": 0.0, "z": 0.0,
          "scaleX": 10.0, "scaleY": 1.0, "scaleZ": 10.0,
          "rotX": 0.0, "rotY": 0.0, "rotZ": 0.0
        },
        "Mesh": {
          "path": "assets/ground.glb"
        },
        "Material3D": {
          "r": 0.5, "g": 0.8, "b": 0.3, "a": 1.0,
          "texture": "assets/grass.png",
          "shininess": 32.0
        },
        "RigidBody3D": {
          "type": "static",
          "shape": "box",
          "mass": 0.0,
          "extents": [10.0, 1.0, 10.0]
        }
      }
    }
  ],
  "settings": {
    "clearColor": [0.1, 0.1, 0.12, 1.0],
    "ambientColor": [0.2, 0.2, 0.2],
    "lightDirection": [0.5, -1.0, 0.3],
    "lightColor": [1.0, 1.0, 1.0],
    "shadowsEnabled": false,
    "skybox": null
  }
}
```

### Schema Rules

1. **`version`** — integer, incremented when the schema changes. The deserialiser checks this and rejects unknown versions with a clear error.
2. **`name`** — scene display name. Optional, defaults to filename.
3. **`entities`** — array of entity objects. Order is preserved for deterministic load.
4. **`id`** — scene-local entity ID. Used for parent-child references within the file. NOT the EnTT entity ID (those are assigned at runtime).
5. **`name`** — per-entity display name for the editor. Stored as a `Name` tag component (string, max 64 chars). Not used by the engine runtime.
6. **`parent`** — scene-local ID of the parent entity, or `null` for root entities. Enables hierarchy.
7. **`components`** — object where keys are component type names and values are component data. Only serialisable components appear here.

### Serialisable Components

| Component | Key fields | Notes |
|-----------|-----------|-------|
| `Transform` | x, y, scaleX, scaleY, rotation | 2D transform |
| `PreviousTransform` | NOT serialised | Runtime interpolation state |
| `Sprite` | texture, width, height, r/g/b/a, flipX/flipY | Texture is an asset path |
| `SpriteAnimation` | NOT serialised (first milestone) | Future: serialize animation config |
| `Tilemap` | NOT serialised (first milestone) | Future: serialize tile data |
| `ParticleEmitter` | NOT serialised (first milestone) | Future: serialize emitter config |
| `Transform3D` | x, y, z, scaleX/Y/Z, rotX/Y/Z | 3D transform |
| `Mesh` | path | Asset path to .glb file |
| `Material3D` | r/g/b/a, texture, shininess | Texture is an asset path or null |
| `RigidBody3D` | type, shape, mass, extents | Physics body configuration |
| `Collider2D` | type, width/height or radius, layer, mask | 2D collision |
| `Skeleton` | NOT serialised | Runtime animation state |
| `AnimationState` | clipIndex, looping, speed | Playback config (not current time) |
| `Name` | name (string, max 64 chars) | Editor-only tag for display |

### Entity Hierarchy (Parent-Child)

Parent-child relationships are stored via the `parent` field in the JSON. At load time:

1. All entities are created first (pass 1).
2. Parent-child links are resolved by mapping scene-local IDs to runtime EnTT entity IDs (pass 2).
3. The hierarchy is stored as a `Parent` component (holds the runtime entity ID of the parent) and a `Children` component (holds a fixed-capacity array of child entity IDs, max 64 children per entity).

```cpp
struct Parent {
    entt::entity parent = entt::null;
};

struct Children {
    entt::entity children[64] = {};
    u32 count = 0;
};
```

These are ECS components, not a separate tree structure. The scene hierarchy panel in the editor reads `Parent`/`Children` to display the tree. The engine runtime uses them for transform propagation (child transforms are relative to parent).

### Scene Serialiser Location

The serialiser lives in `engine/scene/`, not `editor/`, because the engine runtime also needs to load scenes (e.g., `ffe.loadScene("level2.json")` from Lua).

---

## 6. Editor-Engine Boundary

### Core Principle: Editor Owns the Main Loop

The current `Application::run()` method owns the main loop: it calls `glfwPollEvents()`, runs `tick()`, calls `render()`, and swaps buffers. The editor cannot use this — it needs to interleave ImGui rendering, handle editor input, and control when (or whether) the engine ticks.

### Solution: Editor-Hosted Mode

Add an alternative API to `Application` that exposes the loop internals without owning the loop:

```cpp
class Application {
public:
    // Existing: engine owns the loop (games use this)
    int32_t run();

    // New: editor-hosted mode (editor owns the loop)
    Result initSubsystems();        // startup() without entering the loop
    void shutdownSubsystems();      // shutdown() without exiting run()
    void tickOnce(float dt);        // single fixed-update step
    void renderOnce(float alpha);   // single render pass
    GLFWwindow* window() const;     // editor needs the window handle for ImGui

    // Existing
    World& world();
    // ...
};
```

The editor's main loop looks like:

```cpp
// editor/main.cpp (pseudocode)
int main() {
    ApplicationConfig config;
    config.windowTitle = "FFE Editor";
    Application app(config);
    app.initSubsystems();

    EditorApp editor;
    editor.init(app);

    while (!editor.shouldClose()) {
        glfwPollEvents();
        editor.beginFrame();           // ImGui NewFrame, input handling

        if (editor.isPlayMode()) {
            app.tickOnce(fixedDt);     // engine gameplay tick
        }

        app.renderOnce(alpha);         // render scene to FBO
        editor.renderPanels();         // ImGui panels (hierarchy, inspector, viewport)
        editor.endFrame();             // ImGui Render, swap buffers
    }

    editor.shutdown();
    app.shutdownSubsystems();
    return 0;
}
```

### Play Mode

| Mode | Engine ticks? | Editor UI? | Entity editing? |
|------|--------------|------------|-----------------|
| **Edit** | No | Yes | Yes |
| **Play** | Yes (Lua scripts run, physics steps, audio plays) | Yes (read-only inspector) | No |
| **Pause** | No (frozen) | Yes (read-only inspector) | No |

When entering Play mode:
1. Editor serialises the current scene to an in-memory JSON snapshot (the "restore point").
2. Editor starts calling `app.tickOnce()` each frame.
3. Lua `onInit()` fires, physics starts, audio plays.

When exiting Play mode:
1. Editor stops calling `app.tickOnce()`.
2. Editor deserialises the restore-point snapshot back into the ECS, restoring pre-play state.
3. All runtime state (physics bodies, audio voices, Lua state) is reset.

This is the same approach Unity uses. The in-memory snapshot avoids writing temporary files.

### Editor Reads/Writes ECS Directly

The editor does not go through Lua or a command API to inspect entities. It reads and writes EnTT components directly through `Application::world()`. This is safe because the editor is a development tool, not a game — there is no sandbox boundary to enforce.

The inspector panel reads component data from the registry and writes it back when the user edits a field. All edits go through the undo/redo command system (Section 7).

---

## 7. Undo/Redo Architecture

### Command Pattern

Every user action that modifies scene state is wrapped in a `Command` object that records both the old and new state.

```cpp
struct Command {
    virtual ~Command() = default;
    virtual void execute() = 0;     // apply the change
    virtual void undo() = 0;        // revert the change
    virtual const char* name() const = 0;  // for display ("Set Transform", "Delete Entity")
};
```

**Note on virtual:** This is editor UI code, not per-frame engine code. The command stack is modified on user interaction (mouse click, keyboard shortcut), not every frame. Virtual dispatch is appropriate here. Section 3 of CLAUDE.md ("no virtual in per-frame code") applies to the engine runtime, not editor UI handlers.

### Command History

```cpp
class CommandHistory {
public:
    void execute(std::unique_ptr<Command> cmd);  // execute and push onto undo stack
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    const char* undoName() const;   // name of the command that would be undone
    const char* redoName() const;

private:
    static constexpr u32 MAX_UNDO_DEPTH = 256;
    std::unique_ptr<Command> m_undoStack[MAX_UNDO_DEPTH];
    std::unique_ptr<Command> m_redoStack[MAX_UNDO_DEPTH];
    u32 m_undoCount = 0;
    u32 m_redoCount = 0;
};
```

When a new command is executed, the redo stack is cleared. The undo stack is bounded at 256 entries — oldest commands are dropped when the stack is full.

### Command Types (First Milestone)

| Command | Old State | New State |
|---------|-----------|-----------|
| `ModifyComponentCommand<T>` | Copy of old component `T` | Copy of new component `T` |
| `CreateEntityCommand` | (none) | Entity ID + initial components |
| `DestroyEntityCommand` | Entity ID + all component snapshots | (none) |
| `RenameEntityCommand` | Old name string | New name string |

`ModifyComponentCommand<T>` is a template. For each component type, `execute()` calls `registry.replace<T>(entity, newValue)` and `undo()` calls `registry.replace<T>(entity, oldValue)`. The inspector captures the old value before presenting the edit widget, then creates the command with old+new when the user finishes editing.

### Future Command Types (architecture supports, not implemented now)

- `ReparentEntityCommand` — change parent-child relationship
- `AddComponentCommand<T>` / `RemoveComponentCommand<T>`
- `BatchCommand` — groups multiple commands into one undo step (e.g., "paste 10 entities")

---

## 8. Directory Structure

```
editor/                              # Standalone editor application
  CMakeLists.txt
  main.cpp                           # Editor entry point, owns the main loop
  editor_app.h                       # EditorApp class — top-level editor state
  editor_app.cpp
  panels/                            # ImGui panel implementations
    scene_hierarchy_panel.h
    scene_hierarchy_panel.cpp
    inspector_panel.h
    inspector_panel.cpp
    asset_browser_panel.h            # Stub for first milestone
    asset_browser_panel.cpp
    viewport_panel.h                 # Scene viewport (FBO display)
    viewport_panel.cpp
  commands/                          # Undo/redo command implementations
    command.h                        # Command base, CommandHistory
    command_history.cpp
    modify_component_command.h       # Template command for component edits
    entity_commands.h                # Create/destroy/rename entity commands
    entity_commands.cpp

engine/scene/                        # Scene serialisation (shared by editor + runtime)
  scene_serialiser.h
  scene_serialiser.cpp
```

### What Goes Where

- **`editor/`** — Editor-specific code. UI panels, command history, editor application lifecycle. This code is never linked into games.
- **`engine/scene/`** — Scene serialisation and deserialisation. Shared between the editor (save/load scenes) and the engine runtime (loading scenes at game startup or via `ffe.loadScene()`).
- **`engine/editor/`** — The existing debug overlay. Unchanged. This is an in-game tool, not the standalone editor.

---

## 9. ImGui Render Integration

### Current State

The existing debug overlay (`engine/editor/`) already initialises ImGui with `imgui_impl_glfw` and `imgui_impl_opengl3` (GLSL 330 core). It renders ImGui draw data on top of the game framebuffer after the engine's render pass.

### Editor Viewport: Render to FBO

The standalone editor renders the scene to an **offscreen framebuffer object (FBO)**, then displays that FBO's colour attachment as a texture inside an ImGui `Image()` widget. This gives the editor full control over the viewport:

- The viewport can be resized by dragging the ImGui panel.
- Multiple viewports are possible in the future (top/front/side/perspective).
- The editor UI never overlaps with the scene render — they are in separate framebuffers.

```
Frame:
  1. Editor begins ImGui frame
  2. Bind scene FBO
  3. app.renderOnce(alpha)   — engine renders scene into FBO
  4. Bind default framebuffer (screen)
  5. Editor draws ImGui panels (hierarchy, inspector, viewport)
     - Viewport panel: ImGui::Image(fboTextureId, viewportSize)
  6. ImGui::Render() + swap buffers
```

### FBO Management

The editor creates and manages the FBO:

```cpp
struct ViewportFBO {
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    i32 width = 0;
    i32 height = 0;

    void create(i32 w, i32 h);
    void resize(i32 w, i32 h);  // recreate attachments if size changed
    void destroy();
    void bind();
    void unbind();
};
```

The FBO is resized when the ImGui viewport panel changes size. `glViewport` is set to the FBO dimensions before `renderOnce()` and restored to the window dimensions afterward.

### OpenGL 3.3 Compatibility

FBOs with colour + depth attachments are core OpenGL 3.0+. No compatibility concerns for LEGACY tier.

---

## 10. Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO | **No** | Engine does not support RETRO for rendering. Editor inherits this. |
| LEGACY | **Yes** (primary) | ImGui + FBO viewport + engine rendering must all sustain 60 fps. ImGui itself is negligible (<1ms for typical panel layouts on 2012 hardware). The FBO adds one extra texture blit per frame. |
| STANDARD | **Yes** | Identical to LEGACY. |
| MODERN | **Yes** | Identical to LEGACY. Future: Vulkan backend would require different ImGui integration. |

### Performance Budget

The editor's per-frame overhead on top of the engine's existing rendering:

| Operation | Cost | Notes |
|-----------|------|-------|
| ImGui vertex generation | <0.5ms | Immediate-mode — generates ~2K-10K vertices for typical panel layout |
| ImGui draw calls | 5-20 | One per ImGui draw list clip rect |
| FBO bind/unbind | ~0.01ms | Two `glBindFramebuffer` calls |
| FBO texture blit (viewport) | <0.2ms | Single textured quad |
| Undo stack operations | O(1) | Only on user interaction, not per-frame |
| Scene serialisation | 1-50ms | Only on save/load, not per-frame |

Total editor overhead: <1ms per frame. Well within the 16.6ms budget for 60 fps on LEGACY.

---

## 11. Security Considerations

### Scene File Loading

Scene JSON files are external input (created by users, potentially shared between developers, downloaded from the internet). The scene deserialiser must:

1. **Validate the `version` field** — reject unknown versions.
2. **Validate all string fields** — component names must be from a known whitelist. Asset paths must pass the existing `isPathSafe()` check (no path traversal).
3. **Validate numeric fields** — NaN/Inf checks on all floats. Range checks on counts (entity count, children count).
4. **Bound entity count** — reject scenes with more than a configurable maximum (e.g., 100,000 entities) to prevent memory exhaustion.
5. **Bound JSON file size** — reject files larger than a configurable maximum (e.g., 64 MB) before parsing.
6. **No code execution on load** — scene files contain data only. No embedded Lua, no eval, no script paths that auto-execute.

### Asset Paths in Scene Files

Texture and mesh paths in scene files are relative to the project root. The serialiser must:

- Canonicalise paths using `ffe::canonicalizePath()` (existing utility).
- Reject paths that escape the project directory (path traversal via `../`).
- Log a warning (not crash) if a referenced asset does not exist.

### Future: LLM Integration

The LLM integration panel (future milestone) will accept user prompts and generate code. This introduces a significant attack surface. A separate ADR will be required before that work begins.

---

## 12. First Milestone Scope (Session 51)

The first implementation session should deliver:

### Must Have

1. **`editor/` directory** with CMakeLists.txt, `main.cpp`, `EditorApp` class.
2. **Editor binary opens a window** with ImGui initialised, dockspace layout, and basic menu bar.
3. **Scene hierarchy panel** — lists all entities by name (or "Entity #id" if unnamed). Click to select.
4. **Inspector panel** — displays components of the selected entity. Editable fields for `Transform`, `Transform3D`, `Sprite`, `Material3D`. Read-only display for other components.
5. **Scene serialisation** — `engine/scene/scene_serialiser.h/.cpp` with `serialise(World&) -> json` and `deserialise(json, World&)` functions. Save/load via File menu.
6. **Viewport panel** — renders the engine scene to an FBO, displays in ImGui. Mouse/keyboard input in the viewport is forwarded to the engine (camera control).

### Nice to Have (if time permits)

7. **Undo/redo** for component modifications (ModifyComponentCommand).
8. **Entity create/delete** via right-click menu in hierarchy.
9. **`Name` component** — editor assigns display names to entities.

### Explicitly Deferred

- Play-in-editor (requires careful state snapshot/restore).
- Asset browser (requires asset database).
- Multi-viewport.
- Prefab system.
- Build pipeline.
- LLM panel.

---

## 13. Future Milestones

The architecture must support these without requiring fundamental redesign:

### Play-in-Editor (Milestone 2)
- Serialise scene to in-memory JSON snapshot.
- Enter Play mode: engine ticks, Lua runs, physics steps.
- Exit Play mode: deserialise snapshot to restore pre-play state.
- The `tickOnce()` / `renderOnce()` API makes this straightforward.

### Asset Browser (Milestone 3)
- Scans project directory for textures, meshes, audio, scripts.
- Drag-and-drop assets onto entities in the inspector.
- Thumbnail generation for textures and meshes.
- The scene serialiser already stores asset paths — the browser just provides a UI for selecting them.

### Multi-Viewport (Milestone 4)
- Multiple `ViewportFBO` instances, each with independent camera.
- ImGui docking allows placing viewports side-by-side.
- The FBO-per-viewport architecture supports this directly.

### Prefab System (Milestone 5)
- A prefab is a scene file containing a subtree of entities.
- Instantiate prefab = deserialise subtree + remap entity IDs + attach to parent.
- The scene serialiser's entity hierarchy support enables this.

### Build Pipeline (Milestone 6)
- Package game assets + compiled Lua scripts + engine runtime into a distributable.
- The editor is the build tool — separate from the runtime it packages.
- The CMake separation between `ffe_editor` and `ffe_engine` makes this clean.

### LLM Integration Panel (Milestone 7)
- Text input panel where the developer types prompts.
- LLM reads `.context.md` files + current scene state.
- LLM generates Lua scripts or scene modifications.
- Requires a separate ADR for security (sandboxing LLM-generated code).

---

## 14. Files to Create or Modify

### New Files

| File | Contents |
|------|----------|
| `editor/CMakeLists.txt` | CMake target for `ffe_editor` executable |
| `editor/main.cpp` | Editor entry point, main loop |
| `editor/editor_app.h` | `EditorApp` class — top-level editor state and lifecycle |
| `editor/editor_app.cpp` | EditorApp implementation |
| `editor/panels/scene_hierarchy_panel.h` | Scene tree panel |
| `editor/panels/scene_hierarchy_panel.cpp` | Scene tree implementation |
| `editor/panels/inspector_panel.h` | Entity component inspector |
| `editor/panels/inspector_panel.cpp` | Inspector implementation |
| `editor/panels/viewport_panel.h` | FBO-based scene viewport |
| `editor/panels/viewport_panel.cpp` | Viewport implementation |
| `editor/commands/command.h` | `Command` base class, `CommandHistory` |
| `editor/commands/command_history.cpp` | CommandHistory implementation |
| `editor/commands/modify_component_command.h` | Template command for component edits |
| `editor/commands/entity_commands.h` | Create/destroy/rename commands |
| `editor/commands/entity_commands.cpp` | Entity command implementations |
| `engine/scene/scene_serialiser.h` | `serialise()` / `deserialise()` declarations |
| `engine/scene/scene_serialiser.cpp` | JSON scene serialisation implementation |

### Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` (root) | Add `FFE_BUILD_EDITOR` option, `add_subdirectory(editor)` |
| `engine/CMakeLists.txt` | Add `engine/scene/` source files to engine library |
| `engine/core/application.h` | Add `initSubsystems()`, `shutdownSubsystems()`, `tickOnce()`, `renderOnce()`, `window()` |
| `engine/core/application.cpp` | Implement editor-hosted mode methods (extract internals from `run()`) |
| `engine/renderer/render_system.h` | Add `Name`, `Parent`, `Children` ECS components |

### Files NOT Modified

- `engine/editor/editor.h` — the debug overlay is unchanged. It continues to work as an in-game tool.
- No new shaders. The editor renders the same scene as the game runtime.
- No new vcpkg dependencies. Dear ImGui and nlohmann-json are already present.

---

## 15. Test Plan

1. **Scene serialisation round-trip** — unit test: create entities with various components, serialise to JSON, clear the world, deserialise, verify all components match original values.
2. **Entity hierarchy serialisation** — unit test: parent-child relationships survive round-trip.
3. **Version validation** — unit test: reject JSON with unknown version number.
4. **Path traversal rejection** — unit test: scene file with `"texture": "../../etc/passwd"` is rejected.
5. **NaN/Inf rejection** — unit test: scene file with NaN in transform fields is rejected or sanitised.
6. **Entity count limit** — unit test: scene file with 200,000 entities is rejected.
7. **CommandHistory** — unit test: execute/undo/redo sequence produces correct state. Undo stack overflow drops oldest command.
8. **ModifyComponentCommand** — unit test: modify a Transform, undo restores old value, redo restores new value.
9. **CreateEntityCommand / DestroyEntityCommand** — unit test: create, undo removes entity, redo recreates it.
10. **Editor binary builds** — build-engineer verifies `ffe_editor` compiles with zero warnings on Clang-18 and GCC-13.
