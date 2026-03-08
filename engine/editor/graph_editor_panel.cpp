#ifdef FFE_EDITOR

// graph_editor_panel.cpp -- Interactive node canvas for visual scripting graphs.
//
// Implements the ImGui-based node graph editor panel declared in
// graph_editor_panel.h.  All drawing uses ImDrawList (CPU-side commands) --
// no GPU or RHI calls are made here.
//
// Design constraints (per CLAUDE.md):
//   - No heap allocation during render(): all data lives in members or ImGui scratch.
//   - No virtual dispatch.
//   - Const-correct throughout.
//   - Zero warnings with -Wall -Wextra.

#include "editor/graph_editor_panel.h"
#include "core/visual_scripting.h"

#include <imgui.h>

#include <cstring>   // memset
#include <cmath>     // fabsf, powf

namespace ffe::editor {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GraphEditorPanel::GraphEditorPanel() {
    memset(m_layouts, 0, sizeof(m_layouts));
    memset(m_staged,  0, sizeof(m_staged));
}

// ---------------------------------------------------------------------------
// setActiveGraph
// ---------------------------------------------------------------------------

void GraphEditorPanel::setActiveGraph(GraphHandle handle, VisualScriptingSystem* vs) {
    m_activeHandle  = handle;
    m_vs            = vs;
    m_asset         = nullptr;
    m_selectedIdx   = NO_SELECTION;
    m_dragging      = false;
    m_stagedCount   = 0;
    memset(m_staged, 0, sizeof(m_staged));

    if (!vs || !isValid(handle)) {
        return;
    }

    m_asset = vs->getGraphAsset(handle);
    if (!m_asset) {
        return;
    }

    // Initialise editor layout positions.
    // editorX/editorY are not retained in GraphAsset (editor-only data, not
    // persisted to the runtime pool). Default to a simple left-to-right
    // cascade so nodes don't all pile on top of each other when first viewed.
    const uint32_t n = m_asset->nodeCount;
    for (uint32_t i = 0; i < n && i < MAX_NODES_PER_GRAPH; ++i) {
        m_layouts[i].x = static_cast<float>(i % 4) * (NODE_WIDTH + 40.0f);
        m_layouts[i].y = static_cast<float>(i / 4) * 160.0f;
    }
}

// ---------------------------------------------------------------------------
// render -- top-level frame entry point
// ---------------------------------------------------------------------------

void GraphEditorPanel::render(const bool isPlaying) {
    if (!m_visible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Graph Editor", &m_visible)) {
        ImGui::End();
        return;
    }

    // Fill the entire window with the canvas child.
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##graph_canvas", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    const ImVec2 sizeAvail = ImGui::GetContentRegionAvail();
    ImDrawList*  dl       = ImGui::GetWindowDrawList();

    // Background fill.
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + sizeAvail.x, origin.y + sizeAvail.y),
                      IM_COL32(30, 30, 30, 255));

    // Grid.
    drawGrid(dl, origin, sizeAvail);

    // Invisible button spanning the canvas to capture input.
    ImGui::InvisibleButton("##canvas_bg", sizeAvail,
                           ImGuiButtonFlags_MouseButtonLeft  |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered  = ImGui::IsItemHovered();
    const bool canvasLClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool canvasRClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    ImGuiIO& io = ImGui::GetIO();

    // --- Pan ---
    if (!isPlaying) {
        const bool midDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        const bool altLDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
                              (io.KeyAlt);
        if (canvasHovered && (midDrag || altLDrag)) {
            m_panX += io.MouseDelta.x;
            m_panY += io.MouseDelta.y;
        }
    }

    // --- Zoom ---
    if (canvasHovered && io.MouseWheel != 0.0f) {
        const float factor = powf(1.1f, io.MouseWheel);
        m_zoom = m_zoom * factor;
        if (m_zoom < ZOOM_MIN) { m_zoom = ZOOM_MIN; }
        if (m_zoom > ZOOM_MAX) { m_zoom = ZOOM_MAX; }
    }

    // --- Empty canvas ---
    if (!m_asset) {
        const char* msg = "No graph loaded";
        const ImVec2 textSize = ImGui::CalcTextSize(msg);
        const ImVec2 textPos{
            origin.x + (sizeAvail.x - textSize.x) * 0.5f,
            origin.y + (sizeAvail.y - textSize.y) * 0.5f
        };
        dl->AddText(textPos, IM_COL32(120, 120, 120, 255), msg);
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    // --- Connections (drawn behind nodes) ---
    drawConnections(dl, origin);

    // --- Nodes ---
    const uint32_t total = totalNodeCount();
    for (uint32_t i = 0; i < total; ++i) {
        const bool selected = (i == m_selectedIdx);
        drawNode(dl, i, origin, selected, isPlaying);
    }

    // --- Node drag (edit mode only) ---
    if (!isPlaying) {
        handleNodeDrag(origin);
    }

    // --- Selection on LMB click (not during drag) ---
    if (!isPlaying && canvasLClicked && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 mouseCanvas = screenToCanvas(io.MousePos, origin);
        const uint32_t hit = hitTestNodes(mouseCanvas);
        m_selectedIdx = hit;
    }

    // --- Add-node context menu on RMB ---
    if (!isPlaying && canvasRClicked) {
        const ImVec2 mouseCanvas = screenToCanvas(io.MousePos, origin);
        m_ctxCanvasX = mouseCanvas.x;
        m_ctxCanvasY = mouseCanvas.y;
        ImGui::OpenPopup("##add_node");
    }

    drawAddNodeMenu(origin);

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// drawGrid
// ---------------------------------------------------------------------------

void GraphEditorPanel::drawGrid(ImDrawList* dl, const ImVec2 origin,
                                const ImVec2 size) const {
    const float step    = 64.0f * m_zoom;
    const float offsetX = fmodf(m_panX * m_zoom, step);
    const float offsetY = fmodf(m_panY * m_zoom, step);
    const ImU32 col     = IM_COL32(50, 50, 50, 255);

    for (float x = offsetX; x < size.x; x += step) {
        dl->AddLine(ImVec2(origin.x + x, origin.y),
                    ImVec2(origin.x + x, origin.y + size.y),
                    col);
    }
    for (float y = offsetY; y < size.y; y += step) {
        dl->AddLine(ImVec2(origin.x,          origin.y + y),
                    ImVec2(origin.x + size.x, origin.y + y),
                    col);
    }
}

// ---------------------------------------------------------------------------
// drawNode
// ---------------------------------------------------------------------------

void GraphEditorPanel::drawNode(ImDrawList* dl, const uint32_t nodeIdx,
                                const ImVec2 origin, const bool selected,
                                const bool /*readOnly*/) const {
    const NodeDef* def = nodeDefForIdx(nodeIdx);
    if (!def) { return; }

    const NodeLayout& layout = layoutForIdx(nodeIdx);
    const ImVec2 topLeft     = canvasToScreen(layout.x, layout.y, origin);
    const float  h           = nodeHeight(def->portCount);
    const ImVec2 bottomRight { topLeft.x + NODE_WIDTH * m_zoom,
                               topLeft.y + h * m_zoom };

    // Title bar background.
    const ImVec2 titleBR { bottomRight.x, topLeft.y + NODE_TITLE_HEIGHT * m_zoom };
    dl->AddRectFilled(topLeft, titleBR, nodeTypeColor(def->typeId), 4.0f);

    // Body background.
    dl->AddRectFilled(ImVec2(topLeft.x, titleBR.y), bottomRight,
                      IM_COL32(45, 45, 45, 255), 0.0f);

    // Outer border.
    dl->AddRect(topLeft, bottomRight,
                selected ? IM_COL32(255, 220, 0, 255) : IM_COL32(80, 80, 80, 255),
                4.0f, ImDrawFlags_RoundCornersAll,
                selected ? 2.0f : 1.0f);

    // Title text.
    const float fontSize  = ImGui::GetFontSize() * m_zoom;
    const float textY     = topLeft.y + (NODE_TITLE_HEIGHT * m_zoom - fontSize) * 0.5f;
    dl->AddText(nullptr, fontSize,
                ImVec2(topLeft.x + 6.0f * m_zoom, textY),
                IM_COL32(240, 240, 240, 255),
                def->typeName);

    // Ports.
    uint8_t inputIdx  = 0;
    uint8_t outputIdx = 0;
    for (uint8_t p = 0; p < def->portCount; ++p) {
        const PortDef& port = def->ports[p];
        const bool     isIn = (port.dir == PortDir::INPUT);
        const ImVec2   circlePos = portScreenPos(nodeIdx, p, isIn, origin);
        const ImU32    pcol     = portTypeColor(port.type);

        dl->AddCircleFilled(circlePos, PORT_RADIUS * m_zoom, pcol);
        dl->AddCircle(circlePos, PORT_RADIUS * m_zoom, IM_COL32(200, 200, 200, 200), 12, 1.0f);

        // Port label.
        const float labelY = circlePos.y - ImGui::GetFontSize() * m_zoom * 0.5f;
        if (isIn) {
            const float labelX = circlePos.x + (PORT_RADIUS + PORT_LABEL_MARGIN) * m_zoom;
            dl->AddText(nullptr, ImGui::GetFontSize() * m_zoom,
                        ImVec2(labelX, labelY),
                        IM_COL32(200, 200, 200, 255),
                        port.name);
            ++inputIdx;
        } else {
            // Right-align: measure text first.
            const ImVec2 ts  = ImGui::CalcTextSize(port.name);
            const float  labelX = circlePos.x - (ts.x * m_zoom) -
                                  (PORT_RADIUS + PORT_LABEL_MARGIN) * m_zoom;
            dl->AddText(nullptr, ImGui::GetFontSize() * m_zoom,
                        ImVec2(labelX, labelY),
                        IM_COL32(200, 200, 200, 255),
                        port.name);
            ++outputIdx;
        }
    }

    // Suppress unused variable warnings when NDEBUG omits asserts.
    (void)inputIdx;
    (void)outputIdx;
}

// ---------------------------------------------------------------------------
// drawConnections
// ---------------------------------------------------------------------------

void GraphEditorPanel::drawConnections(ImDrawList* dl, const ImVec2 origin) const {
    if (!m_asset) { return; }

    for (uint32_t ci = 0; ci < m_asset->connectionCount; ++ci) {
        const Connection& conn = m_asset->connections[ci];

        // Find node indices by ID.
        uint32_t srcIdx = NO_SELECTION;
        uint32_t dstIdx = NO_SELECTION;
        for (uint32_t ni = 0; ni < m_asset->nodeCount; ++ni) {
            if (m_asset->nodes[ni].id == conn.srcNode) { srcIdx = ni; }
            if (m_asset->nodes[ni].id == conn.dstNode) { dstIdx = ni; }
        }
        if (srcIdx == NO_SELECTION || dstIdx == NO_SELECTION) { continue; }

        const NodeDef* srcDef = VisualScriptingSystem::getNodeDef(
            m_asset->nodes[srcIdx].typeId);
        if (!srcDef) { continue; }

        // Output port screen pos: srcPort is the index into the output ports.
        // In the NodeDef, outputs follow inputs in the ports[] array.
        const uint8_t srcPortAbsIdx =
            static_cast<uint8_t>(srcDef->inputCount + conn.srcPort);
        if (srcPortAbsIdx >= srcDef->portCount) { continue; }

        const ImVec2 srcPos = portScreenPos(srcIdx, srcPortAbsIdx, false, origin);
        const ImVec2 dstPos = portScreenPos(dstIdx, conn.dstPort, true, origin);

        const PortType pt   = srcDef->ports[srcPortAbsIdx].type;
        const ImU32    col  = portTypeColor(pt);
        const float    dist = fabsf(dstPos.x - srcPos.x);
        const float    ctrl = (dist > BEZIER_CTRL_OFFSET * m_zoom)
                                  ? dist * 0.5f
                                  : BEZIER_CTRL_OFFSET * m_zoom;

        const ImVec2 cp1 { srcPos.x + ctrl, srcPos.y };
        const ImVec2 cp2 { dstPos.x - ctrl, dstPos.y };

        dl->AddBezierCubic(srcPos, cp1, cp2, dstPos, col, 2.0f, 20);
    }
}

// ---------------------------------------------------------------------------
// drawAddNodeMenu
// ---------------------------------------------------------------------------

bool GraphEditorPanel::drawAddNodeMenu(const ImVec2 origin) {
    bool added = false;
    if (ImGui::BeginPopup("##add_node")) {
        ImGui::TextUnformatted("Add Node");
        ImGui::Separator();

        const int typeCount = VisualScriptingSystem::getNodeTypeCount();
        for (int t = 1; t <= typeCount; ++t) {
            const NodeDef* def = VisualScriptingSystem::getNodeDef(
                static_cast<NodeTypeId>(t));
            if (!def) { continue; }

            if (ImGui::MenuItem(def->typeName)) {
                if (m_stagedCount < MAX_STAGED_NODES) {
                    StagedNode& sn = m_staged[m_stagedCount];
                    memset(&sn, 0, sizeof(sn));
                    sn.instance.id     = static_cast<NodeId>(
                        (m_asset ? m_asset->nodeCount : 0u) +
                        m_stagedCount + 1u);
                    sn.instance.typeId = static_cast<NodeTypeId>(t);
                    sn.layout.x        = m_ctxCanvasX;
                    sn.layout.y        = m_ctxCanvasY;
                    sn.used            = true;
                    ++m_stagedCount;
                    added = true;
                }
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
    (void)origin;   // reserved for future coordinate display
    return added;
}

// ---------------------------------------------------------------------------
// handleNodeDrag
// ---------------------------------------------------------------------------

bool GraphEditorPanel::handleNodeDrag(const ImVec2 origin) {
    ImGuiIO& io = ImGui::GetIO();

    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) || io.KeyAlt) {
        m_dragging = false;
        return false;
    }

    // On fresh drag start, pick the node under the cursor.
    if (!m_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 mouseCanvas = screenToCanvas(io.MousePos, origin);
        const uint32_t hit = hitTestNodes(mouseCanvas);
        if (hit != NO_SELECTION) {
            m_selectedIdx = hit;
            m_dragging    = true;
        }
    }

    if (m_dragging && m_selectedIdx != NO_SELECTION) {
        NodeLayout& lay = layoutForIdx(m_selectedIdx);
        // Convert delta from screen pixels to canvas units.
        lay.x += io.MouseDelta.x / m_zoom;
        lay.y += io.MouseDelta.y / m_zoom;
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// hitTestNodes
// ---------------------------------------------------------------------------

uint32_t GraphEditorPanel::hitTestNodes(const ImVec2 canvasMousePos) const {
    const uint32_t total = totalNodeCount();
    if (total == 0) { return NO_SELECTION; }

    // Iterate back-to-front (highest index = topmost visually).
    uint32_t i = total;
    while (i-- > 0) {
        const NodeDef* def = nodeDefForIdx(i);
        if (!def) { continue; }
        const NodeLayout& lay = layoutForIdx(i);
        const float w = NODE_WIDTH;
        const float h = nodeHeight(def->portCount);
        if (canvasMousePos.x >= lay.x && canvasMousePos.x <= lay.x + w &&
            canvasMousePos.y >= lay.y && canvasMousePos.y <= lay.y + h) {
            return i;
        }
    }
    return NO_SELECTION;
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

ImVec2 GraphEditorPanel::canvasToScreen(const float cx, const float cy,
                                        const ImVec2 origin) const {
    return {
        origin.x + (cx + m_panX) * m_zoom,
        origin.y + (cy + m_panY) * m_zoom
    };
}

ImVec2 GraphEditorPanel::screenToCanvas(const ImVec2 screen, const ImVec2 origin) const {
    return {
        (screen.x - origin.x) / m_zoom - m_panX,
        (screen.y - origin.y) / m_zoom - m_panY
    };
}

// ---------------------------------------------------------------------------
// portScreenPos
// ---------------------------------------------------------------------------

ImVec2 GraphEditorPanel::portScreenPos(const uint32_t nodeIdx,
                                       const uint8_t  portIdx,
                                       const bool     isInput,
                                       const ImVec2   origin) const {
    const NodeLayout& lay = layoutForIdx(nodeIdx);
    const ImVec2 topLeft  = canvasToScreen(lay.x, lay.y, origin);

    // Vertical: title bar + (portIdx + 0.5) rows.
    const float portY = topLeft.y +
                        (NODE_TITLE_HEIGHT + NODE_PORT_HEIGHT * portIdx +
                         NODE_PORT_HEIGHT * 0.5f) * m_zoom;

    const float portX = isInput
                            ? topLeft.x
                            : topLeft.x + NODE_WIDTH * m_zoom;

    return { portX, portY };
}

// ---------------------------------------------------------------------------
// nodeHeight
// ---------------------------------------------------------------------------

float GraphEditorPanel::nodeHeight(const uint8_t portCount) {
    return NODE_TITLE_HEIGHT + NODE_PORT_HEIGHT * static_cast<float>(portCount);
}

// ---------------------------------------------------------------------------
// nodeTypeColor
// ---------------------------------------------------------------------------

ImU32 GraphEditorPanel::nodeTypeColor(const NodeTypeId typeId) {
    // Event nodes (1-3): dark red
    if (typeId >= 1 && typeId <= 3) {
        return IM_COL32(120, 30, 30, 255);
    }
    // Action nodes (4-11): split by sub-category
    switch (typeId) {
        case 4:  // SetVelocity
        case 5:  // SetPosition
        case 6:  // GetPosition
            return IM_COL32(30, 50, 120, 255);   // dark blue
        case 7:  // BranchIf
            return IM_COL32(80, 30, 100, 255);   // dark purple
        case 8:  // Add
        case 9:  // Multiply
            return IM_COL32(30, 90, 40, 255);    // dark green
        case 10: // PlaySound
            return IM_COL32(100, 70, 20, 255);   // dark amber
        case 11: // DestroyEntity
            return IM_COL32(100, 30, 30, 255);   // darker red
        default:
            return IM_COL32(60, 60, 60, 255);    // neutral grey for unknowns
    }
}

// ---------------------------------------------------------------------------
// portTypeColor
// ---------------------------------------------------------------------------

ImU32 GraphEditorPanel::portTypeColor(const PortType pt) {
    switch (pt) {
        case PortType::FLOAT:  return IM_COL32(255, 200,   0, 255);  // gold
        case PortType::BOOL:   return IM_COL32( 80, 200,  80, 255);  // green
        case PortType::ENTITY: return IM_COL32( 80, 200, 220, 255);  // cyan
        case PortType::VEC2:   return IM_COL32(255, 140,   0, 255);  // orange
        case PortType::VEC3:   return IM_COL32(255, 100,  50, 255);  // red-orange
        default:               return IM_COL32(180, 180, 180, 255);  // grey fallback
    }
}

// ---------------------------------------------------------------------------
// nodeDefForIdx
// ---------------------------------------------------------------------------

const NodeDef* GraphEditorPanel::nodeDefForIdx(const uint32_t nodeIdx) const {
    const NodeInstance* inst = nodeInstanceForIdx(nodeIdx);
    if (!inst) { return nullptr; }
    return VisualScriptingSystem::getNodeDef(inst->typeId);
}

// ---------------------------------------------------------------------------
// layoutForIdx
// ---------------------------------------------------------------------------

const GraphEditorPanel::NodeLayout& GraphEditorPanel::layoutForIdx(
    const uint32_t nodeIdx) const {
    if (m_asset && nodeIdx < m_asset->nodeCount) {
        return m_layouts[nodeIdx];
    }
    const uint32_t assetCount = m_asset ? m_asset->nodeCount : 0u;
    const uint32_t stagedOff  = nodeIdx - assetCount;
    // Bounds guaranteed by totalNodeCount(); callers should not exceed it.
    return m_staged[stagedOff].layout;
}

GraphEditorPanel::NodeLayout& GraphEditorPanel::layoutForIdx(
    const uint32_t nodeIdx) {
    if (m_asset && nodeIdx < m_asset->nodeCount) {
        return m_layouts[nodeIdx];
    }
    const uint32_t assetCount = m_asset ? m_asset->nodeCount : 0u;
    const uint32_t stagedOff  = nodeIdx - assetCount;
    return m_staged[stagedOff].layout;
}

// ---------------------------------------------------------------------------
// nodeInstanceForIdx
// ---------------------------------------------------------------------------

const NodeInstance* GraphEditorPanel::nodeInstanceForIdx(
    const uint32_t nodeIdx) const {
    if (m_asset && nodeIdx < m_asset->nodeCount) {
        return &m_asset->nodes[nodeIdx];
    }
    const uint32_t assetCount = m_asset ? m_asset->nodeCount : 0u;
    const uint32_t stagedOff  = nodeIdx - assetCount;
    if (stagedOff < m_stagedCount) {
        return &m_staged[stagedOff].instance;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// totalNodeCount
// ---------------------------------------------------------------------------

uint32_t GraphEditorPanel::totalNodeCount() const {
    const uint32_t assetCount = m_asset ? m_asset->nodeCount : 0u;
    return assetCount + m_stagedCount;
}

} // namespace ffe::editor

#endif // FFE_EDITOR
