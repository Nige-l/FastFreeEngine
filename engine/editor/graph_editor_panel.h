#pragma once
#ifdef FFE_EDITOR

// graph_editor_panel.h -- Interactive node canvas for visual scripting graphs.
//
// GraphEditorPanel renders an interactive node canvas for inspecting and
// laying out a loaded GraphAsset. It is a docked ImGui panel in the FFE
// standalone editor (ffe_editor_app) and in the debug overlay (ffe_editor).
//
// In play mode (isPlaying == true): the panel is read-only -- no dragging,
// no add/delete. In edit mode: nodes can be dragged to new positions.
// Connections are drawn as bezier curves. Right-click adds a new node.
// Save-to-disk is deferred to M3; this panel is a viewer/layout tool.
//
// Owner: renderer-specialist (ImGui code; no engine logic).
// Tiers: All tiers (ImGui is CPU-side; no GPU dependency beyond the editor).

#include "core/visual_scripting.h"

#include <imgui.h>

namespace ffe::editor {

// GraphEditorPanel renders an interactive node canvas for a loaded GraphAsset.
//
// Usage (call once per frame):
//   panel.setActiveGraph(handle, &visualScriptingSystem);
//   panel.render(isPlaying);
class GraphEditorPanel {
public:
    GraphEditorPanel();
    ~GraphEditorPanel() = default;

    // Non-copyable, non-movable -- owns per-panel state.
    GraphEditorPanel(const GraphEditorPanel&)            = delete;
    GraphEditorPanel& operator=(const GraphEditorPanel&) = delete;

    // Set which graph to display.
    // Pass GraphHandle{0} (or nullptr vs) to show an empty canvas.
    // Loads editor positions from editorX/editorY stored on the GraphAsset.
    void setActiveGraph(GraphHandle handle, VisualScriptingSystem* vs);

    // Render the panel into the current ImGui dockspace. Call once per frame.
    // isPlaying == true: read-only mode (no drag, no right-click add-node).
    void render(bool isPlaying);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

private:
    // --- Active graph ---
    GraphHandle            m_activeHandle{};
    const GraphAsset*      m_asset = nullptr;   // Non-owning view into VSS pool
    VisualScriptingSystem* m_vs    = nullptr;   // Non-owning pointer

    // --- Per-node editor positions (editor-only, not stored in GraphAsset) ---
    // Populated from the GraphAsset node order at setActiveGraph() time.
    // Matches MAX_NODES_PER_GRAPH = 256.
    struct NodeLayout {
        float x = 0.0f;
        float y = 0.0f;
    };
    NodeLayout m_layouts[MAX_NODES_PER_GRAPH] = {};

    // Staging buffer for nodes added via right-click in edit mode.
    // Not wired into the live GraphAsset (save-to-disk deferred to M3).
    struct StagedNode {
        NodeInstance  instance{};
        NodeLayout    layout{};
        bool          used = false;
    };
    static constexpr uint32_t MAX_STAGED_NODES = 64;
    StagedNode  m_staged[MAX_STAGED_NODES] = {};
    uint32_t    m_stagedCount = 0;

    // --- Canvas pan and zoom ---
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    float m_zoom = 1.0f;
    static constexpr float ZOOM_MIN = 0.1f;
    static constexpr float ZOOM_MAX = 4.0f;

    // --- Selection state ---
    // Index into the composite node list (asset nodes then staged nodes).
    // UINT32_MAX = nothing selected.
    static constexpr uint32_t NO_SELECTION = UINT32_MAX;
    uint32_t m_selectedIdx = NO_SELECTION;

    // Whether a node is being dragged this frame.
    bool m_dragging       = false;
    bool m_visible        = true;

    // Position where the right-click context menu was opened (canvas space).
    float m_ctxCanvasX = 0.0f;
    float m_ctxCanvasY = 0.0f;

    // --- Node geometry constants ---
    static constexpr float NODE_WIDTH        = 160.0f;
    static constexpr float NODE_TITLE_HEIGHT =  24.0f;
    static constexpr float NODE_PORT_HEIGHT  =  20.0f;
    static constexpr float PORT_RADIUS       =   6.0f;
    static constexpr float PORT_LABEL_MARGIN =   4.0f;

    // --- Bezier control-point horizontal offset (canvas units) ---
    static constexpr float BEZIER_CTRL_OFFSET = 80.0f;

    // --- Rendering helpers ---
    void drawGrid(ImDrawList* dl, ImVec2 origin, ImVec2 size) const;

    // Draw one node. nodeIdx is the combined index (< asset nodeCount = asset node,
    // >= asset nodeCount = staged node).
    void drawNode(ImDrawList* dl, uint32_t nodeIdx, ImVec2 origin, bool selected,
                  bool readOnly) const;

    // Draw all bezier connections for the live asset.
    void drawConnections(ImDrawList* dl, ImVec2 origin) const;

    // Draw the right-click "Add Node" popup.
    // Returns true if a new node was added.
    bool drawAddNodeMenu(ImVec2 origin);

    // Handle left-mouse node drag. Updates m_layouts or m_staged[].layout.
    // Returns true if any node is being dragged.
    bool handleNodeDrag(ImVec2 origin);

    // Hit-test all nodes (front-to-back, i.e. highest index first).
    // Returns the combined index of the hit node, or NO_SELECTION.
    uint32_t hitTestNodes(ImVec2 canvasMousePos) const;

    // Coordinate transforms (canvas <-> screen).
    ImVec2 canvasToScreen(float cx, float cy, ImVec2 origin) const;
    ImVec2 screenToCanvas(ImVec2 screen, ImVec2 origin) const;

    // Screen-space centre of a port circle for node nodeIdx, port portIdx.
    // isInput=true: left edge; isInput=false: right edge.
    ImVec2 portScreenPos(uint32_t nodeIdx, uint8_t portIdx, bool isInput,
                         ImVec2 origin) const;

    // Total height of a node given its port count.
    static float nodeHeight(uint8_t portCount);

    // Node-type colour for the title bar background.
    static ImU32 nodeTypeColor(NodeTypeId typeId);

    // Port-type colour.
    static ImU32 portTypeColor(PortType pt);

    // Retrieve the NodeDef for a node at combined index.
    // Returns nullptr if the index is out of range or typeId is unknown.
    const NodeDef* nodeDefForIdx(uint32_t nodeIdx) const;

    // Retrieve the canvas-space position of a node at combined index.
    const NodeLayout& layoutForIdx(uint32_t nodeIdx) const;
          NodeLayout& layoutForIdx(uint32_t nodeIdx);

    // Retrieve a NodeInstance at combined index (const).
    // Returns nullptr on out-of-range.
    const NodeInstance* nodeInstanceForIdx(uint32_t nodeIdx) const;

    // Total number of nodes visible in the panel (asset + staged).
    uint32_t totalNodeCount() const;
};

} // namespace ffe::editor

#endif // FFE_EDITOR
