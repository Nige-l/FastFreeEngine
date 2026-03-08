// vegetation.cpp -- GPU-instanced billboard grass and procedural tree system.
//
// Grass: Y-axis cylindrical billboard quads. Each patch is tied to a TerrainHandle.
// Instance positions are scattered deterministically across the terrain surface and
// Y-queried via getTerrainHeight(). One glDrawArraysInstanced call per active patch.
//
// Trees: Two instanced draw calls per frame (trunk + crown) using the
// MESH_BLINN_PHONG_INSTANCED shader (enum 13). Instance data (mat4 + vec4 color) uploaded
// as InstanceData to two separate VBOs sized for MAX_TREES entries.
//
// No heap allocation per frame. All GPU buffers allocated at init() time.
// No virtual functions. No RTTI. No exceptions. No std::function.
//
// Tier: LEGACY (OpenGL 3.3 core). No DSA.

#include "renderer/vegetation.h"
#include "renderer/shader_library.h"
#include "renderer/gpu_instancing.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Simple LCG parameters (Numerical Recipes).
static constexpr u32 LCG_A = 1664525u;
static constexpr u32 LCG_C = 1013904223u;

static u32 lcgNext(u32& state) {
    state = LCG_A * state + LCG_C;
    return state;
}

// Map LCG output to [0.0, 1.0).
static float lcgFloat(u32& state) {
    return static_cast<float>(lcgNext(state)) / static_cast<float>(0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Quad geometry for grass (6 vertices = 2 triangles, Y-up billboard).
//
// Local-space positions: X in [-0.5, +0.5], Y in [0, 1], Z = 0.
// The vertex shader applies cylindrical Y-axis billboarding — only the X
// component is rotated toward the camera; Y is untouched.
// UV layout: (0,0) bottom-left, (1,1) top-right.
// ---------------------------------------------------------------------------

struct QuadVertex {
    float x, y, z;  // local position
    float u, v;     // UV
};

static constexpr QuadVertex GRASS_QUAD_VERTS[6] = {
    // Triangle 1 (CCW)
    { -0.5f, 0.0f, 0.0f,  0.0f, 1.0f },
    {  0.5f, 0.0f, 0.0f,  1.0f, 1.0f },
    {  0.5f, 1.0f, 0.0f,  1.0f, 0.0f },
    // Triangle 2 (CCW)
    { -0.5f, 0.0f, 0.0f,  0.0f, 1.0f },
    {  0.5f, 1.0f, 0.0f,  1.0f, 0.0f },
    { -0.5f, 1.0f, 0.0f,  0.0f, 0.0f },
};

// ---------------------------------------------------------------------------
// Unit cube geometry for tree trunk/crown.
// 36 vertices (6 faces x 2 triangles x 3 verts).
// Layout per vertex: position(3f) + normal(3f) = 24 bytes.
// Slots 0 (vec3 position) and 1 (vec3 normal) match MESH_BLINN_PHONG_INSTANCED.
// ---------------------------------------------------------------------------

struct CubeVertex {
    float px, py, pz;
    float nx, ny, nz;
};

static constexpr CubeVertex CUBE_VERTS[36] = {
    // -Z face
    { -0.5f, -0.5f, -0.5f,  0.f,  0.f, -1.f },
    {  0.5f,  0.5f, -0.5f,  0.f,  0.f, -1.f },
    {  0.5f, -0.5f, -0.5f,  0.f,  0.f, -1.f },
    { -0.5f, -0.5f, -0.5f,  0.f,  0.f, -1.f },
    { -0.5f,  0.5f, -0.5f,  0.f,  0.f, -1.f },
    {  0.5f,  0.5f, -0.5f,  0.f,  0.f, -1.f },
    // +Z face
    { -0.5f, -0.5f,  0.5f,  0.f,  0.f,  1.f },
    {  0.5f, -0.5f,  0.5f,  0.f,  0.f,  1.f },
    {  0.5f,  0.5f,  0.5f,  0.f,  0.f,  1.f },
    { -0.5f, -0.5f,  0.5f,  0.f,  0.f,  1.f },
    {  0.5f,  0.5f,  0.5f,  0.f,  0.f,  1.f },
    { -0.5f,  0.5f,  0.5f,  0.f,  0.f,  1.f },
    // -X face
    { -0.5f,  0.5f,  0.5f, -1.f,  0.f,  0.f },
    { -0.5f,  0.5f, -0.5f, -1.f,  0.f,  0.f },
    { -0.5f, -0.5f, -0.5f, -1.f,  0.f,  0.f },
    { -0.5f,  0.5f,  0.5f, -1.f,  0.f,  0.f },
    { -0.5f, -0.5f, -0.5f, -1.f,  0.f,  0.f },
    { -0.5f, -0.5f,  0.5f, -1.f,  0.f,  0.f },
    // +X face
    {  0.5f,  0.5f,  0.5f,  1.f,  0.f,  0.f },
    {  0.5f, -0.5f, -0.5f,  1.f,  0.f,  0.f },
    {  0.5f,  0.5f, -0.5f,  1.f,  0.f,  0.f },
    {  0.5f,  0.5f,  0.5f,  1.f,  0.f,  0.f },
    {  0.5f, -0.5f,  0.5f,  1.f,  0.f,  0.f },
    {  0.5f, -0.5f, -0.5f,  1.f,  0.f,  0.f },
    // -Y face
    { -0.5f, -0.5f, -0.5f,  0.f, -1.f,  0.f },
    {  0.5f, -0.5f, -0.5f,  0.f, -1.f,  0.f },
    {  0.5f, -0.5f,  0.5f,  0.f, -1.f,  0.f },
    { -0.5f, -0.5f, -0.5f,  0.f, -1.f,  0.f },
    {  0.5f, -0.5f,  0.5f,  0.f, -1.f,  0.f },
    { -0.5f, -0.5f,  0.5f,  0.f, -1.f,  0.f },
    // +Y face
    { -0.5f,  0.5f, -0.5f,  0.f,  1.f,  0.f },
    {  0.5f,  0.5f,  0.5f,  0.f,  1.f,  0.f },
    {  0.5f,  0.5f, -0.5f,  0.f,  1.f,  0.f },
    { -0.5f,  0.5f, -0.5f,  0.f,  1.f,  0.f },
    { -0.5f,  0.5f,  0.5f,  0.f,  1.f,  0.f },
    {  0.5f,  0.5f,  0.5f,  0.f,  1.f,  0.f },
};

// ---------------------------------------------------------------------------
// VegetationSystem — lifecycle
// ---------------------------------------------------------------------------

void VegetationSystem::init() {
    if (m_initialised) { return; }

    // Guard: no GL context in headless mode.
    if (glad_glGenVertexArrays == nullptr) {
        // Headless: allow logic paths (handle allocation, density clamping) to
        // work normally. Just skip all GL object creation.
        m_initialised = true;
        return;
    }

    // --- Fallback 4x4 solid-green RGBA texture ---
    {
        static constexpr u32 W = 4;
        static constexpr u32 H = 4;
        // RGBA8: R=51, G=153, B=38, A=255 (solid opaque green)
        static constexpr unsigned char GREEN_PIXELS[W * H * 4] = {
            51,153,38,255, 51,153,38,255, 51,153,38,255, 51,153,38,255,
            51,153,38,255, 51,153,38,255, 51,153,38,255, 51,153,38,255,
            51,153,38,255, 51,153,38,255, 51,153,38,255, 51,153,38,255,
            51,153,38,255, 51,153,38,255, 51,153,38,255, 51,153,38,255,
        };
        rhi::TextureDesc desc{};
        desc.width     = W;
        desc.height    = H;
        desc.format    = rhi::TextureFormat::RGBA8;
        desc.filter    = rhi::TextureFilter::NEAREST;
        desc.wrap      = rhi::TextureWrap::REPEAT;
        desc.pixelData = GREEN_PIXELS;
        m_fallbackTexture = rhi::createTexture(desc);
    }

    // --- Unit cube VBO shared by trunk and crown VAOs ---
    {
        GLuint cubeVbo = 0;
        glGenBuffers(1, &cubeVbo);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(sizeof(CUBE_VERTS)),
                     CUBE_VERTS, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_cubeVbo = cubeVbo;
        m_cubeIndexCount = 36;
    }

    // --- Trunk instance VBO (MAX_TREES x 80 bytes, GL_DYNAMIC_DRAW) ---
    {
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(MAX_TREES * sizeof(InstanceData)),
                     nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_trunkInstanceVbo = vbo;
    }

    // --- Crown instance VBO (MAX_TREES x 80 bytes, GL_DYNAMIC_DRAW) ---
    {
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(MAX_TREES * sizeof(InstanceData)),
                     nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_crownInstanceVbo = vbo;
    }

    // --- Trunk VAO ---
    // Cube geometry at slot 0 (position) and slot 1 (normal).
    // Instance mat4 columns at slots 8-11 from m_trunkInstanceVbo (divisor=1).
    {
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
        // slot 0: vec3 a_position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(CubeVertex)),
                              nullptr);
        // slot 1: vec3 a_normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(CubeVertex)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(CubeVertex, nx))));

        // Per-instance model matrix (mat4, 4 x vec4, slots 8-11, divisor=1)
        glBindBuffer(GL_ARRAY_BUFFER, m_trunkInstanceVbo);
        for (u32 col = 0; col < INSTANCE_MAT4_SLOT_COUNT; ++col) {
            const u32 slot = INSTANCE_ATTR_SLOT_BASE + col;
            glEnableVertexAttribArray(slot);
            glVertexAttribPointer(slot, 4, GL_FLOAT, GL_FALSE,
                                  static_cast<GLsizei>(sizeof(InstanceData)),
                                  reinterpret_cast<const void*>(
                                      static_cast<uintptr_t>(col * sizeof(glm::vec4))));
            glVertexAttribDivisor(slot, 1);
        }

        // Per-instance color (vec4, slot 12, divisor=1)
        glEnableVertexAttribArray(INSTANCE_COLOR_SLOT);
        glVertexAttribPointer(INSTANCE_COLOR_SLOT, 4, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(InstanceData)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(InstanceData, instanceColor))));
        glVertexAttribDivisor(INSTANCE_COLOR_SLOT, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_trunkVao = vao;
    }

    // --- Crown VAO ---
    // Same cube geometry layout, but instance VBO is m_crownInstanceVbo.
    {
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(CubeVertex)),
                              nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(CubeVertex)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(CubeVertex, nx))));

        glBindBuffer(GL_ARRAY_BUFFER, m_crownInstanceVbo);
        for (u32 col = 0; col < INSTANCE_MAT4_SLOT_COUNT; ++col) {
            const u32 slot = INSTANCE_ATTR_SLOT_BASE + col;
            glEnableVertexAttribArray(slot);
            glVertexAttribPointer(slot, 4, GL_FLOAT, GL_FALSE,
                                  static_cast<GLsizei>(sizeof(InstanceData)),
                                  reinterpret_cast<const void*>(
                                      static_cast<uintptr_t>(col * sizeof(glm::vec4))));
            glVertexAttribDivisor(slot, 1);
        }

        // Per-instance color (vec4, slot 12, divisor=1)
        glEnableVertexAttribArray(INSTANCE_COLOR_SLOT);
        glVertexAttribPointer(INSTANCE_COLOR_SLOT, 4, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(InstanceData)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(InstanceData, instanceColor))));
        glVertexAttribDivisor(INSTANCE_COLOR_SLOT, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_crownVao = vao;
    }

    m_initialised = true;
    FFE_LOG_INFO("Vegetation", "VegetationSystem initialised (MAX_PATCHES=%u MAX_TREES=%u)",
                 MAX_VEGETATION_PATCHES, MAX_TREES);
}

void VegetationSystem::shutdown() {
    if (!m_initialised) { return; }

    // Remove all active grass patches and free their GPU resources.
    for (u32 i = 0; i < MAX_VEGETATION_PATCHES; ++i) {
        if (m_patches[i].active) {
            removePatch(m_patches[i].handle);
        }
    }

    m_treeCount = 0;

    if (m_trunkVao != 0) {
        glDeleteVertexArrays(1, &m_trunkVao);
        m_trunkVao = 0;
    }
    if (m_crownVao != 0) {
        glDeleteVertexArrays(1, &m_crownVao);
        m_crownVao = 0;
    }
    if (m_cubeVbo != 0) {
        glDeleteBuffers(1, &m_cubeVbo);
        m_cubeVbo = 0;
    }
    if (m_trunkInstanceVbo != 0) {
        glDeleteBuffers(1, &m_trunkInstanceVbo);
        m_trunkInstanceVbo = 0;
    }
    if (m_crownInstanceVbo != 0) {
        glDeleteBuffers(1, &m_crownInstanceVbo);
        m_crownInstanceVbo = 0;
    }

    if (rhi::isValid(m_fallbackTexture)) {
        rhi::destroyTexture(m_fallbackTexture);
        m_fallbackTexture = {0};
    }

    m_vegetationShader    = {0};
    m_meshInstancedShader = {0};
    m_initialised         = false;
    FFE_LOG_INFO("Vegetation", "VegetationSystem shut down");
}

// ---------------------------------------------------------------------------
// VegetationSystem — shader handle injection
// ---------------------------------------------------------------------------

void VegetationSystem::setShaderHandles(const rhi::ShaderHandle vegetationShader,
                                        const rhi::ShaderHandle meshInstancedShader) {
    m_vegetationShader    = vegetationShader;
    m_meshInstancedShader = meshInstancedShader;
}

// ---------------------------------------------------------------------------
// VegetationSystem — internal helpers
// ---------------------------------------------------------------------------

u32 VegetationSystem::findSlot(const VegetationHandle handle) const {
    if (handle.id == 0) { return MAX_VEGETATION_PATCHES; }
    for (u32 i = 0; i < MAX_VEGETATION_PATCHES; ++i) {
        if (m_patches[i].active && m_patches[i].handle.id == handle.id) {
            return i;
        }
    }
    return MAX_VEGETATION_PATCHES; // sentinel = not found
}

void VegetationSystem::uploadQuadGeometry(PatchSlot& slot) {
    if (slot.quadVbo == 0) {
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        slot.quadVbo = vbo;
    }
    glBindBuffer(GL_ARRAY_BUFFER, slot.quadVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(GRASS_QUAD_VERTS)),
                 GRASS_QUAD_VERTS, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VegetationSystem::scatterAndUploadInstances(PatchSlot& slot) {
    if (!isValid(slot.terrain)) { return; }

    const u32 count = slot.instanceCount;

    // Scatter positions within the terrain's actual bounds (captured at addPatch() time).
    // Scatter within [-halfWidth, +halfWidth] on X and [-halfDepth, +halfDepth] on Z.
    // getTerrainHeight returns 0.0 outside terrain bounds — acceptable for edge grass.
    const float halfWidth = slot.halfWidth;
    const float halfDepth = slot.halfDepth;

    // Stack-allocate: MAX_GRASS_INSTANCES * sizeof(GrassInstance) = 256 * 24 = 6 KB.
    GrassInstance instances[MAX_GRASS_INSTANCES];

    // Seed from handle id and terrain id for determinism and patch independence.
    u32 rng = (slot.handle.id * 2654435761u) ^ (slot.terrain.id * 2246822519u);

    const float baseScale = (slot.config.grassHeight > 0.01f)
                            ? slot.config.grassHeight
                            : 0.5f;

    for (u32 i = 0; i < count; ++i) {
        const float fx  = (lcgFloat(rng) * 2.0f - 1.0f) * halfWidth;
        const float fz  = (lcgFloat(rng) * 2.0f - 1.0f) * halfDepth;
        const float fy  = getTerrainHeight(slot.terrain, fx, fz);
        const float rot = lcgFloat(rng) * 6.2831853f; // [0, 2pi)

        instances[i].position = glm::vec3(fx, fy, fz);
        instances[i].scale    = baseScale;
        instances[i].rotation = rot;
        instances[i]._pad     = 0.0f;
    }

    if (slot.instanceVbo == 0) {
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        slot.instanceVbo = vbo;
    }
    glBindBuffer(GL_ARRAY_BUFFER, slot.instanceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(GrassInstance)),
                 instances, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// VegetationSystem — grass
// ---------------------------------------------------------------------------

VegetationHandle VegetationSystem::addPatch(const TerrainHandle terrain,
                                            const VegetationConfig& cfg) {
    if (!m_initialised) {
        FFE_LOG_WARN("Vegetation", "addPatch called before init()");
        return VegetationHandle{0};
    }
    if (!isValid(terrain)) {
        FFE_LOG_WARN("Vegetation", "addPatch: invalid terrain handle");
        return VegetationHandle{0};
    }

    // Find a free slot.
    u32 slotIdx = MAX_VEGETATION_PATCHES;
    for (u32 i = 0; i < MAX_VEGETATION_PATCHES; ++i) {
        if (!m_patches[i].active) {
            slotIdx = i;
            break;
        }
    }
    if (slotIdx == MAX_VEGETATION_PATCHES) {
        FFE_LOG_WARN("Vegetation", "addPatch: MAX_VEGETATION_PATCHES (%u) reached",
                     MAX_VEGETATION_PATCHES);
        return VegetationHandle{0};
    }

    // Clamp density to [1, MAX_GRASS_INSTANCES].
    const u32 density = (cfg.density < 1u)             ? 1u
                      : (cfg.density > MAX_GRASS_INSTANCES) ? MAX_GRASS_INSTANCES
                      : cfg.density;

    // Capture terrain half-extents now so scatterAndUploadInstances uses actual bounds.
    // getTerrainConfig returns a default-constructed TerrainConfig{} (worldWidth=256,
    // worldDepth=256) for inactive or unloaded terrain handles. Since the default values
    // are non-zero they are indistinguishable from a legitimately 256-unit terrain, so we
    // cannot silently correct the situation — warn instead so callers see the problem.
    const TerrainConfig terrainCfg = getTerrainConfig(terrain);
    if (terrainCfg.worldWidth <= 0.0f || terrainCfg.worldDepth <= 0.0f) {
        // This can only happen if the terrain slot was somehow loaded with invalid
        // dimensions (validateConfig would normally reject this). Warn and fall back.
        FFE_LOG_WARN("Vegetation",
                     "addPatch: terrain handle %u returned non-positive world dimensions "
                     "(%.1f x %.1f); defaulting to 256x256. The terrain may not be active.",
                     terrain.id, static_cast<double>(terrainCfg.worldWidth),
                     static_cast<double>(terrainCfg.worldDepth));
    } else if (terrainCfg.chunkCountX == 0 || terrainCfg.chunkCountZ == 0) {
        // getTerrainConfig returns TerrainConfig{} with chunkCountX/Z = 16 for inactive
        // slots, so this branch only fires for genuinely malformed configs. Log it.
        FFE_LOG_WARN("Vegetation",
                     "addPatch: terrain handle %u has zero chunk count; "
                     "grass scatter bounds may be incorrect.",
                     terrain.id);
    }
    const float halfWidth = terrainCfg.worldWidth * 0.5f;
    const float halfDepth = terrainCfg.worldDepth * 0.5f;

    PatchSlot& slot       = m_patches[slotIdx];
    slot.active           = true;
    slot.handle           = VegetationHandle{m_nextHandleId++};
    slot.terrain          = terrain;
    slot.instanceCount    = density;
    slot.config           = cfg;
    slot.config.density   = density;
    slot.halfWidth        = halfWidth;
    slot.halfDepth        = halfDepth;
    slot.vao              = 0;
    slot.quadVbo          = 0;
    slot.instanceVbo      = 0;

    // Skip all GL calls in headless mode.
    if (glad_glGenVertexArrays == nullptr) {
        return slot.handle;
    }

    uploadQuadGeometry(slot);
    scatterAndUploadInstances(slot);

    // Build VAO: quad vertices at slots 0-1, instance data at slots 4-6.
    {
        GLuint vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        // Per-vertex quad data
        glBindBuffer(GL_ARRAY_BUFFER, slot.quadVbo);
        // slot 0: vec3 a_pos (x,y,z — local quad position)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(QuadVertex)),
                              nullptr);
        // slot 1: vec2 a_uv
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(QuadVertex)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(QuadVertex, u))));

        // Per-instance data (divisor=1)
        glBindBuffer(GL_ARRAY_BUFFER, slot.instanceVbo);
        // slot 4: vec3 i_position (bytes 0-11)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(GrassInstance)),
                              nullptr);
        glVertexAttribDivisor(4, 1);
        // slot 5: float i_scale (bytes 12-15)
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(GrassInstance)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(GrassInstance, scale))));
        glVertexAttribDivisor(5, 1);
        // slot 6: float i_rotation (bytes 16-19)
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(GrassInstance)),
                              reinterpret_cast<const void*>(
                                  static_cast<uintptr_t>(offsetof(GrassInstance, rotation))));
        glVertexAttribDivisor(6, 1);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        slot.vao = vao;
    }

    FFE_LOG_INFO("Vegetation", "Added grass patch id=%u density=%u",
                 slot.handle.id, density);
    return slot.handle;
}

void VegetationSystem::removePatch(const VegetationHandle handle) {
    const u32 idx = findSlot(handle);
    if (idx == MAX_VEGETATION_PATCHES) { return; } // No-op on invalid handle.

    PatchSlot& slot = m_patches[idx];

    if (slot.vao != 0) {
        glDeleteVertexArrays(1, &slot.vao);
        slot.vao = 0;
    }
    if (slot.quadVbo != 0) {
        glDeleteBuffers(1, &slot.quadVbo);
        slot.quadVbo = 0;
    }
    if (slot.instanceVbo != 0) {
        glDeleteBuffers(1, &slot.instanceVbo);
        slot.instanceVbo = 0;
    }

    slot.active        = false;
    slot.handle        = VegetationHandle{0};
    slot.terrain       = TerrainHandle{0};
    slot.instanceCount = 0;
    slot.config        = VegetationConfig{};
}

void VegetationSystem::renderGrass(const glm::mat4& view, const glm::mat4& proj,
                                   const glm::vec3& cameraPos,
                                   const float fadeStartDist,
                                   const float fadeEndDist) {
    if (!m_initialised) { return; }
    if (glad_glGenVertexArrays == nullptr) { return; } // Headless guard.
    if (!rhi::isValid(m_vegetationShader)) {
        FFE_LOG_WARN("Vegetation",
                     "renderGrass: vegetation shader not set — call setShaderHandles()");
        return;
    }

    rhi::bindShader(m_vegetationShader);

    // Uniforms common to all patches.
    rhi::setUniformMat4(m_vegetationShader, "u_view",        view);
    rhi::setUniformMat4(m_vegetationShader, "u_projection",  proj);
    rhi::setUniformVec3(m_vegetationShader, "u_cameraPos",   cameraPos);
    rhi::setUniformFloat(m_vegetationShader, "u_fadeStart",  fadeStartDist);
    rhi::setUniformFloat(m_vegetationShader, "u_fadeEnd",    fadeEndDist);
    rhi::setUniformInt(m_vegetationShader,   "u_grassTex",   0);
    // Simplified lighting: grass uses a fixed sun direction and neutral colours.
    // renderGrass does not accept per-call lighting params to keep the API minimal.
    rhi::setUniformVec3(m_vegetationShader, "u_lightColor",   glm::vec3(1.0f, 1.0f, 0.95f));
    rhi::setUniformVec3(m_vegetationShader, "u_ambientColor", glm::vec3(0.3f, 0.35f, 0.3f));

    for (u32 i = 0; i < MAX_VEGETATION_PATCHES; ++i) {
        const PatchSlot& slot = m_patches[i];
        if (!slot.active || slot.vao == 0 || slot.instanceCount == 0) { continue; }

        // Bind grass texture; fall back to solid-green texture if none provided.
        const rhi::TextureHandle tex = rhi::isValid(slot.config.textureHandle)
                                     ? slot.config.textureHandle
                                     : m_fallbackTexture;
        rhi::bindTexture(tex, 0);
        rhi::setUniformInt(m_vegetationShader, "u_hasTexture",
                           rhi::isValid(slot.config.textureHandle) ? 1 : 0);

        glBindVertexArray(slot.vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                              static_cast<GLsizei>(slot.instanceCount));
    }

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// VegetationSystem — trees
// ---------------------------------------------------------------------------

bool VegetationSystem::addTree(const float x, const float y, const float z,
                               const float scale) {
    if (!m_initialised) {
        FFE_LOG_WARN("Vegetation", "addTree called before init()");
        return false;
    }
    if (m_treeCount >= MAX_TREES) {
        FFE_LOG_WARN("Vegetation", "addTree: MAX_TREES (%u) reached", MAX_TREES);
        return false;
    }

    const float s = (scale > 0.0f) ? scale : 1.0f;

    TreeInstance& t = m_trees[m_treeCount];
    t.x             = x;
    t.y             = y;
    t.z             = z;
    t.scale         = s;
    ++m_treeCount;

    rebuildTreeBuffers();
    return true;
}

void VegetationSystem::clearTrees() {
    m_treeCount = 0;
    // No GL buffer upload needed when count is zero — renderTrees() early-outs.
}

void VegetationSystem::rebuildTreeBuffers() {
    if (!m_initialised) { return; }
    if (glad_glGenVertexArrays == nullptr) { return; } // Headless guard.
    if (m_trunkInstanceVbo == 0 || m_crownInstanceVbo == 0) { return; }
    if (m_treeCount == 0) { return; }

    // Stack-allocate trunk and crown instance data.
    // MAX_TREES=512 -> 512*80=40 KB each -> 80 KB total. Cold path only.
    InstanceData trunkData[MAX_TREES];
    InstanceData crownData[MAX_TREES];

    for (u32 i = 0; i < m_treeCount; ++i) {
        const TreeInstance& t = m_trees[i];
        const float s = t.scale;

        // Trunk: translate(x,y,z) * scale(0.15s, 1.0s, 0.15s)
        trunkData[i].modelMatrix =
            glm::translate(glm::mat4(1.0f), glm::vec3(t.x, t.y, t.z)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.15f * s, 1.0f * s, 0.15f * s));
        trunkData[i].instanceColor = glm::vec4(1.0f); // white — shader uses u_diffuseColor

        // Crown: translate(x, y + 1.0s, z) * scale(0.7s, 0.7s, 0.7s)
        crownData[i].modelMatrix =
            glm::translate(glm::mat4(1.0f), glm::vec3(t.x, t.y + 1.0f * s, t.z)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.7f * s, 0.7f * s, 0.7f * s));
        crownData[i].instanceColor = glm::vec4(1.0f); // white — shader uses u_diffuseColor
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_trunkInstanceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_treeCount * sizeof(InstanceData)),
                 trunkData, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_crownInstanceVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_treeCount * sizeof(InstanceData)),
                 crownData, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VegetationSystem::renderTrees(const glm::mat4& view, const glm::mat4& proj,
                                   const glm::vec3& lightDir,
                                   const glm::vec3& lightColor,
                                   const glm::vec3& ambientColor,
                                   const glm::vec3& cameraPos) {
    if (!m_initialised) { return; }
    if (glad_glGenVertexArrays == nullptr) { return; } // Headless guard.
    if (m_treeCount == 0) { return; }
    if (!rhi::isValid(m_meshInstancedShader)) {
        FFE_LOG_WARN("Vegetation",
                     "renderTrees: mesh instanced shader not set — call setShaderHandles()");
        return;
    }
    if (m_trunkVao == 0 || m_crownVao == 0) { return; }

    const glm::mat4 viewProj = proj * view;

    rhi::bindShader(m_meshInstancedShader);

    // Set uniforms shared by trunk and crown passes.
    rhi::setUniformMat4(m_meshInstancedShader, "u_viewProjection",   viewProj);
    // lightSpaceMatrix is required by the vertex shader — set to identity (no shadows).
    rhi::setUniformMat4(m_meshInstancedShader, "u_lightSpaceMatrix", glm::mat4(1.0f));
    // clipPlane: zero vector disables clipping in the vertex shader.
    rhi::setUniformVec4(m_meshInstancedShader, "u_clipPlane",        glm::vec4(0.0f));

    // Fragment shader lighting uniforms.
    rhi::setUniformVec3(m_meshInstancedShader, "u_lightDir",          lightDir);
    rhi::setUniformVec3(m_meshInstancedShader, "u_lightColor",        lightColor);
    rhi::setUniformVec3(m_meshInstancedShader, "u_ambientColor",      ambientColor);
    rhi::setUniformVec3(m_meshInstancedShader, "u_viewPos",           cameraPos);
    rhi::setUniformVec3(m_meshInstancedShader, "u_specularColor",     glm::vec3(0.0f));
    rhi::setUniformFloat(m_meshInstancedShader, "u_shininess",         1.0f);

    // Disable all optional features.
    rhi::setUniformInt(m_meshInstancedShader, "u_shadowsEnabled",    0);
    rhi::setUniformInt(m_meshInstancedShader, "u_hasNormalMap",      0);
    rhi::setUniformInt(m_meshInstancedShader, "u_hasSpecularMap",    0);
    rhi::setUniformInt(m_meshInstancedShader, "u_pointLightCount",   0);
    rhi::setUniformInt(m_meshInstancedShader, "u_fogEnabled",        0);

    // No diffuse texture — use diffuse color directly.
    rhi::setUniformInt(m_meshInstancedShader, "u_diffuseTexture",    0);

    const auto count = static_cast<GLsizei>(m_treeCount);

    // --- Trunk pass: brown ---
    rhi::setUniformVec4(m_meshInstancedShader, "u_diffuseColor",
                        glm::vec4(0.42f, 0.27f, 0.13f, 1.0f));
    glBindVertexArray(m_trunkVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);

    // --- Crown pass: green ---
    rhi::setUniformVec4(m_meshInstancedShader, "u_diffuseColor",
                        glm::vec4(0.15f, 0.55f, 0.10f, 1.0f));
    glBindVertexArray(m_crownVao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);

    glBindVertexArray(0);
}

} // namespace ffe::renderer
