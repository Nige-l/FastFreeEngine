#pragma once

// vegetation.h — Phase 9 M5: GPU-instanced billboard grass + tree system
// Tier: LEGACY (OpenGL 3.3+)
//
// Overview:
//   VegetationSystem manages two independent GPU-instanced populations:
//   1. Grass — Y-axis cylindrical billboard quads using the VEGETATION shader (enum 21).
//      Each patch is tied to a TerrainHandle; instance positions are scattered across
//      the terrain surface and Y-queried via getTerrainHeight().
//   2. Trees — Two instanced draw calls per frame (trunk + crown) using MESH_BLINN_PHONG_INSTANCED.
//      The caller provides world positions; no implicit terrain height lookup.
//
// Constraints:
//   - No heap allocation per frame. All GPU buffers allocated at init() time.
//   - No virtual functions in hot paths.
//   - No RTTI, no exceptions, no std::function.
//   - Alpha-test (discard) only — no blending — no sorting required.
//   - GLSL 330 core only (LEGACY tier).
//
// Thread safety: single-threaded. All methods must be called from the render thread.

#include "core/types.h"
#include "renderer/rhi_types.h"
#include "renderer/terrain.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Maximum simultaneous grass patches. Each patch: 1 VAO + 2 VBOs (quad verts + instances).
// 32 patches × (256 instances × 24 B + 96 B quad) ≈ 200 KB GPU — trivial on LEGACY.
inline constexpr u32 MAX_VEGETATION_PATCHES = 32;

// Maximum grass instances per patch. Density is clamped to [1, MAX_GRASS_INSTANCES].
inline constexpr u32 MAX_GRASS_INSTANCES    = 256;

// Maximum trees in the scene. Two InstanceData buffers (trunk + crown) × 512 entries
// × 64 B = 64 KB GPU — trivial on LEGACY.
inline constexpr u32 MAX_TREES              = 512;

// ---------------------------------------------------------------------------
// Handle
// ---------------------------------------------------------------------------

// Opaque handle to a grass patch. Value 0 (default) is always invalid.
// Same pattern as TerrainHandle, MeshHandle.
struct VegetationHandle { u32 id = 0; };
inline bool isValid(const VegetationHandle h) { return h.id != 0; }
static_assert(sizeof(VegetationHandle) == 4);

// ---------------------------------------------------------------------------
// Instance structs
// ---------------------------------------------------------------------------

// Per-grass-instance data uploaded to the GPU via instance VBO.
// Attribute layout (divisor = 1):
//   slot 4 — vec3  i_position  (bytes 0-11)
//   slot 5 — float i_scale     (bytes 12-15)
//   slot 6 — float i_rotation  (bytes 16-19, Y-axis pre-rotation in radians)
//   _pad               (bytes 20-23, alignment pad)
// Total: 24 bytes.
struct GrassInstance {
    glm::vec3 position;  // world-space XYZ (12 bytes)
    float     scale;     // uniform scale   ( 4 bytes)
    float     rotation;  // Y-axis pre-rotation in radians ( 4 bytes)
    float     _pad;      // padding to 24 bytes            ( 4 bytes)
};
static_assert(sizeof(GrassInstance) == 24, "GrassInstance must be 24 bytes");

// Per-tree-instance data (CPU-side only). Model matrices for trunk/crown are
// derived at render time and uploaded as InstanceData (glm::mat4) to the GPU.
//   Trunk : translate(x,y,z) * scale(0.15s, 1.0s, 0.15s)
//   Crown : translate(x, y + 1.0s, z) * scale(0.7s, 0.7s, 0.7s)
// where s = TreeInstance::scale.
// Total: 16 bytes.
struct TreeInstance {
    float x;      // world X ( 4 bytes)
    float y;      // world Y ( 4 bytes)
    float z;      // world Z ( 4 bytes)
    float scale;  // uniform scale ( 4 bytes)
};
static_assert(sizeof(TreeInstance) == 16, "TreeInstance must be 16 bytes");

// ---------------------------------------------------------------------------
// VegetationConfig — per-patch grass configuration
// ---------------------------------------------------------------------------

struct VegetationConfig {
    u32                  density       = 64;    // Instance count, clamped to [1, 256]
    float                grassHeight   = 0.5f;  // World-space height of each quad
    float                grassWidth    = 0.3f;  // World-space width of each quad
    float                fadeStartDist = 20.0f; // Distance at which alpha fade begins
    float                fadeEndDist   = 40.0f; // Distance at which grass is fully discarded
    rhi::TextureHandle   textureHandle = {0};   // Grass diffuse+alpha texture (0 = solid green fallback)
};

// ---------------------------------------------------------------------------
// VegetationSystem
// ---------------------------------------------------------------------------

// GPU-instanced billboard grass and simple procedural tree system.
//
// Lifetime: constructed as a member of the render coordinator (e.g., MeshRenderer).
// Call init() once after the OpenGL context is created.
// Call shutdown() before the context is destroyed.
//
// NOT a global singleton.
class VegetationSystem {
public:
    // --- Lifecycle ---

    // Allocate GPU buffers, create unit-cube meshes for trees, and generate
    // the 4×4 solid-green fallback texture. No heap allocation after this call.
    void init();

    // Free all GPU resources (VAOs, VBOs, textures, cube mesh buffers).
    // Safe to call even if init() was never called.
    void shutdown();

    // --- Grass ---

    // Scatter `cfg.density` grass instances across the full terrain surface,
    // querying Y positions via getTerrainHeight(). Returns a valid handle on
    // success; returns VegetationHandle{0} if MAX_VEGETATION_PATCHES is reached
    // or terrain is invalid. Cold path only — do NOT call per frame.
    VegetationHandle addPatch(TerrainHandle terrain, const VegetationConfig& cfg);

    // Remove a grass patch and free its GPU resources.
    // No-op if handle is invalid.
    void removePatch(VegetationHandle handle);

    // Render all active grass patches with Y-axis cylindrical billboarding.
    // Binds the VEGETATION shader, sets uniforms, issues one glDrawArraysInstanced
    // call per active patch. Alpha-test (discard) only — no blending.
    void renderGrass(const glm::mat4& view, const glm::mat4& proj,
                     const glm::vec3& cameraPos,
                     float fadeStartDist, float fadeEndDist);

    // --- Shader handle injection ---

    // Provide shader handles resolved from ShaderLibrary.
    // Must be called after init() and after initShaderLibrary().
    // vegetationShader  : VEGETATION (enum 21) — grass billboard shader.
    // meshInstancedShader : MESH_BLINN_PHONG_INSTANCED (enum 13) — tree trunk/crown shader.
    void setShaderHandles(rhi::ShaderHandle vegetationShader,
                          rhi::ShaderHandle meshInstancedShader);

    // --- Trees ---

    // Add a tree at world position (x, y, z) with uniform scale.
    // Caller is responsible for Y placement (use getTerrainHeight() in Lua if needed).
    // Returns false (and silently ignores the call) if MAX_TREES is reached.
    // Rebuilds the GPU instance buffers (trunk + crown) via glBufferData. Cold path.
    bool addTree(float x, float y, float z, float scale);

    // Remove all trees and reset the GPU instance buffers. Cold path.
    void clearTrees();

    // Render all trees as two instanced draw calls (trunk then crown).
    // Uses MESH_BLINN_PHONG_INSTANCED shader (enum 13). No-op if treeCount == 0.
    void renderTrees(const glm::mat4& view, const glm::mat4& proj,
                     const glm::vec3& lightDir, const glm::vec3& lightColor,
                     const glm::vec3& ambientColor);

private:
    // --- Patch slot ---
    struct PatchSlot {
        bool             active       = false;
        VegetationHandle handle       = {};
        TerrainHandle    terrain      = {};
        u32              instanceCount = 0;
        VegetationConfig config       = {};
        // OpenGL object names (0 = unallocated)
        u32              vao          = 0;  // Vertex array object
        u32              quadVbo      = 0;  // Quad vertex positions + UVs
        u32              instanceVbo  = 0;  // GrassInstance array (GL_STATIC_DRAW)
    };

    // Fixed-size patch storage — no dynamic allocation.
    PatchSlot m_patches[MAX_VEGETATION_PATCHES] = {};
    u32       m_nextHandleId                    = 1; // Monotonically increasing; 0 is reserved for invalid

    // --- Tree storage ---
    TreeInstance m_trees[MAX_TREES] = {};
    u32          m_treeCount        = 0;

    // GPU buffers for tree instance data (trunk matrices + crown matrices).
    // Each buffer holds MAX_TREES InstanceData (glm::mat4, 64 bytes each).
    u32 m_trunkInstanceVbo = 0; // GL buffer name for trunk InstanceData
    u32 m_crownInstanceVbo = 0; // GL buffer name for crown InstanceData

    // Shared VAO for tree trunk and crown draw calls (quad geometry re-used).
    u32 m_trunkVao = 0;
    u32 m_crownVao = 0;

    // Unit-cube VBO shared by trunk and crown (position + normal, no UVs needed).
    u32 m_cubeVbo = 0;
    u32 m_cubeIbo = 0;
    u32 m_cubeIndexCount = 0;

    // Fallback 4×4 solid-green RGBA texture used when patch textureHandle == 0.
    rhi::TextureHandle m_fallbackTexture = {0};

    // Shader handles resolved once at init() time.
    rhi::ShaderHandle m_vegetationShader    = {0};
    rhi::ShaderHandle m_meshInstancedShader = {0};

    // Whether init() has been called successfully.
    bool m_initialised = false;

    // --- Internal helpers ---
    // Resolve a patch slot index from a VegetationHandle. Returns MAX_VEGETATION_PATCHES
    // (sentinel) if not found.
    u32 findSlot(VegetationHandle handle) const;

    // Generate the quad geometry for grass (4 vertices, 2 triangles).
    // Uploads to quadVbo. Called once per addPatch().
    void uploadQuadGeometry(PatchSlot& slot);

    // Scatter instances using a fixed-seed LCG across the terrain surface.
    // Writes up to MAX_GRASS_INSTANCES entries and uploads to instanceVbo.
    void scatterAndUploadInstances(PatchSlot& slot);

    // Rebuild the trunk and crown instance buffers from m_trees[0..m_treeCount).
    // Called by addTree() and clearTrees(). No per-frame call.
    void rebuildTreeBuffers();
};

} // namespace ffe::renderer
