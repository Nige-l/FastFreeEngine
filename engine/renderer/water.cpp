// water.cpp -- Planar water rendering implementation for FFE.
//
// Two APIs live here:
//
//   1. Legacy flat-function API (initWater / renderWaterReflection / renderWater /
//      shutdownWater / resizeWaterFBOs / setWaterShader / computeReflectionCamera).
//      Used by application.cpp and Lua scripting bindings. One global water plane
//      per scene, driven by the WaterConfig ECS singleton and a Water-tagged entity.
//
//   2. WaterManager class API (init / createWater / destroyWater / render /
//      renderReflection / update / shutdown).
//      Future-facing multi-surface API. Up to MAX_WATER_SURFACES simultaneous planes.
//      Each surface has its own VAO/VBO and optionally a half-res reflection FBO.
//
// No per-frame heap allocation in either API. No virtual functions. No RTTI.
// No exceptions. No std::function.
//
// Tier: LEGACY (OpenGL 3.3 core). No tessellation, no compute shaders.

#include "renderer/water.h"
#include "renderer/mesh_renderer.h"
#include "renderer/terrain_renderer.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/shader_library.h"
#include "renderer/camera.h"
#include "renderer/post_process.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef> // offsetof

#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif

namespace ffe::renderer {

// ===========================================================================
// Legacy flat-function implementation
// ===========================================================================

// ---------------------------------------------------------------------------
// File-static GPU resources (one global water plane)
// ---------------------------------------------------------------------------

static bool   s_initialised  = false;
static i32    s_width        = 0;
static i32    s_height       = 0;

// Reflection FBO (half resolution)
static GLuint s_reflFbo      = 0;
static GLuint s_reflColorTex = 0;
static GLuint s_reflDepthRbo = 0;
static i32    s_reflWidth    = 0;
static i32    s_reflHeight   = 0;

// Water quad geometry (1 VAO, 1 VBO, 1 IBO)
static GLuint s_waterVao = 0;
static GLuint s_waterVbo = 0;
static GLuint s_waterIbo = 0;

// Water shader handle (compiled via ShaderLibrary)
static rhi::ShaderHandle s_waterShader{};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void createReflectionFbo(const i32 w, const i32 h) {
    s_reflWidth  = w / 2;
    s_reflHeight = h / 2;
    if (s_reflWidth  < 1) { s_reflWidth  = 1; }
    if (s_reflHeight < 1) { s_reflHeight = 1; }

    // Color attachment: GL_RGBA8
    glGenTextures(1, &s_reflColorTex);
    glBindTexture(GL_TEXTURE_2D, s_reflColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(s_reflWidth),
                 static_cast<GLsizei>(s_reflHeight),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth attachment: GL_DEPTH_COMPONENT24 renderbuffer (explicit precision required by OpenGL spec).
    glGenRenderbuffers(1, &s_reflDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s_reflDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          static_cast<GLsizei>(s_reflWidth),
                          static_cast<GLsizei>(s_reflHeight));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Assemble FBO
    glGenFramebuffers(1, &s_reflFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_reflFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, s_reflColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, s_reflDepthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("Water", "Reflection FBO incomplete (status 0x%X)",
                      glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void destroyReflectionFbo() {
    if (s_reflFbo != 0) {
        glDeleteFramebuffers(1, &s_reflFbo);
        s_reflFbo = 0;
    }
    if (s_reflColorTex != 0) {
        glDeleteTextures(1, &s_reflColorTex);
        s_reflColorTex = 0;
    }
    if (s_reflDepthRbo != 0) {
        glDeleteRenderbuffers(1, &s_reflDepthRbo);
        s_reflDepthRbo = 0;
    }
}

static void createWaterQuad() {
    if (glad_glGenVertexArrays == nullptr) {
        return; // Headless: skip GL object creation.
    }

    // XZ plane at Y=0, extent [-0.5, 0.5] in both X and Z.
    // Model matrix scales and translates to world coordinates.
    static constexpr WaterVertex VERTICES[] = {
        {{-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, 0.0f, -0.5f}, {1.0f, 0.0f}},
        {{ 0.5f, 0.0f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f, 0.0f,  0.5f}, {0.0f, 1.0f}},
    };

    static constexpr u32 INDICES[] = {
        0u, 1u, 2u,
        0u, 2u, 3u,
    };

    glGenVertexArrays(1, &s_waterVao);
    glGenBuffers(1, &s_waterVbo);
    glGenBuffers(1, &s_waterIbo);

    glBindVertexArray(s_waterVao);

    glBindBuffer(GL_ARRAY_BUFFER, s_waterVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(VERTICES)),
                 VERTICES, GL_STATIC_DRAW);

    // Position: location 0 (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(WaterVertex)),
                          nullptr);

    // TexCoord: location 1 (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(WaterVertex)),
                          reinterpret_cast<const void*>(
                              static_cast<uintptr_t>(offsetof(WaterVertex, texCoord))));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_waterIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(INDICES)),
                 INDICES, GL_STATIC_DRAW);

    glBindVertexArray(0);
}

static void destroyWaterQuad() {
    if (s_waterVao != 0) {
        glDeleteVertexArrays(1, &s_waterVao);
        s_waterVao = 0;
    }
    if (s_waterVbo != 0) {
        glDeleteBuffers(1, &s_waterVbo);
        s_waterVbo = 0;
    }
    if (s_waterIbo != 0) {
        glDeleteBuffers(1, &s_waterIbo);
        s_waterIbo = 0;
    }
}

// ---------------------------------------------------------------------------
// Reflection camera computation
// ---------------------------------------------------------------------------

Camera computeReflectionCamera(const Camera& camera, const f32 waterLevel) {
    Camera reflected     = camera;
    reflected.position.y = 2.0f * waterLevel - camera.position.y;
    reflected.target.y   = 2.0f * waterLevel - camera.target.y;
    reflected.up.y       = -camera.up.y;
    return reflected;
}

// ---------------------------------------------------------------------------
// Legacy public API
// ---------------------------------------------------------------------------

bool initWater(const i32 width, const i32 height) {
    if (rhi::isHeadless()) {
        s_initialised = false;
        return true; // Silently succeed -- no GPU work possible.
    }

    if (glad_glGenFramebuffers == nullptr) {
        FFE_LOG_WARN("Water", "GL not available -- skipping init");
        return false;
    }

    s_width  = width;
    s_height = height;

    createReflectionFbo(width, height);
    createWaterQuad();

    s_initialised = true;

    FFE_LOG_INFO("Water", "Water system initialised (%dx%d, reflection %dx%d)",
                 width, height, s_reflWidth, s_reflHeight);
    return true;
}

void resizeWaterFBOs(const i32 width, const i32 height) {
    if (!s_initialised) { return; }
    if (width <= 0 || height <= 0) { return; }
    if (width == s_width && height == s_height) { return; }

    s_width  = width;
    s_height = height;

    destroyReflectionFbo();
    createReflectionFbo(width, height);

    FFE_LOG_INFO("Water", "Water FBOs resized to %dx%d (reflection %dx%d)",
                 width, height, s_reflWidth, s_reflHeight);
}

void renderWaterReflection(World& world, const Camera& camera3d,
                           const FogParams& fog, const WaterConfig& waterCfg) {
    if (!s_initialised) { return; }
    if (!waterCfg.enabled) { return; }
    if (s_reflFbo == 0) { return; }

    // Check if any Water entity exists.
    const auto waterView = world.registry().view<const Water>();
    if (waterView.begin() == waterView.end()) { return; }

    // Retrieve shader library from ECS context.
    const auto* shaderLib = world.registry().ctx().find<ShaderLibrary>();
    if (shaderLib == nullptr) { return; }

    // Compute reflected camera.
    const Camera reflCamera = computeReflectionCamera(camera3d, waterCfg.waterLevel);

    // Save current FBO and viewport.
    GLint savedFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);
    const i32 savedVpW = rhi::getViewportWidth();
    const i32 savedVpH = rhi::getViewportHeight();

    // Bind reflection FBO.
    glBindFramebuffer(GL_FRAMEBUFFER, s_reflFbo);
    glViewport(0, 0,
               static_cast<GLsizei>(s_reflWidth),
               static_cast<GLsizei>(s_reflHeight));
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Enable clip plane for reflection pass.
    glEnable(GL_CLIP_DISTANCE0);

    // Clip plane: vec4(0, 1, 0, -waterLevel) clips everything below the water plane.
    const glm::vec4 clipPlane{0.0f, 1.0f, 0.0f, -waterCfg.waterLevel};

    // Upload clip plane to all affected mesh shaders.
    const rhi::ShaderHandle meshShaders[] = {
        getShader(*shaderLib, BuiltinShader::MESH_BLINN_PHONG),
        getShader(*shaderLib, BuiltinShader::MESH_SKINNED),
        getShader(*shaderLib, BuiltinShader::MESH_PBR),
        getShader(*shaderLib, BuiltinShader::MESH_PBR_SKINNED),
        getShader(*shaderLib, BuiltinShader::MESH_BLINN_PHONG_INSTANCED),
        getShader(*shaderLib, BuiltinShader::MESH_PBR_INSTANCED),
        getShader(*shaderLib, BuiltinShader::TERRAIN),
        getShader(*shaderLib, BuiltinShader::SKYBOX),
    };

    for (const auto& sh : meshShaders) {
        if (rhi::isValid(sh)) {
            rhi::bindShader(sh);
            rhi::setUniformVec4(sh, "u_clipPlane", clipPlane);
        }
    }

    // Render scene from reflected viewpoint (no shadow pass in reflection).
    const ShadowConfig noShadow{};
    const ShadowMap    emptyShadowMap{};
    meshRenderSystem(world, reflCamera, noShadow, emptyShadowMap, fog);
    terrainRenderSystem(world, reflCamera, noShadow, emptyShadowMap, fog);

    // Render skybox into reflection.
    const auto* const* skyboxCfgPtr = world.registry().ctx().find<SkyboxConfig*>();
    if (skyboxCfgPtr != nullptr && *skyboxCfgPtr != nullptr) {
        renderSkybox(world, reflCamera, **skyboxCfgPtr);
    }

    // Disable clip plane.
    glDisable(GL_CLIP_DISTANCE0);

    // Reset clip plane to disabled on all shaders.
    const glm::vec4 noClip{0.0f, 0.0f, 0.0f, 0.0f};
    for (const auto& sh : meshShaders) {
        if (rhi::isValid(sh)) {
            rhi::bindShader(sh);
            rhi::setUniformVec4(sh, "u_clipPlane", noClip);
        }
    }

    // Restore previous FBO and viewport.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo));
    glViewport(0, 0,
               static_cast<GLsizei>(savedVpW),
               static_cast<GLsizei>(savedVpH));
}

void renderWater(World& world, const Camera& camera3d,
                 const FogParams& fog, const WaterConfig& waterCfg,
                 const f32 time) {
    if (!s_initialised) { return; }
    if (!waterCfg.enabled) { return; }
    if (!rhi::isValid(s_waterShader)) { return; }
    if (s_waterVao == 0) { return; }

    // Check for Water entity with Transform3D.
    const auto waterView = world.registry().view<const Water, const Transform3D>();
    if (waterView.begin() == waterView.end()) { return; }

    const auto firstEntity   = *waterView.begin();
    const auto& transform    = world.registry().get<const Transform3D>(firstEntity);

    // Build model matrix: translate to water level, scale to water extent.
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(transform.position.x,
                                             waterCfg.waterLevel,
                                             transform.position.z));
    model = glm::scale(model, glm::vec3(transform.scale.x * DEFAULT_WATER_EXTENT,
                                         1.0f,
                                         transform.scale.z * DEFAULT_WATER_EXTENT));

    const glm::mat4 vpMatrix = computeViewProjectionMatrix(camera3d);

    // Water pipeline state: alpha blend, depth test read (no write).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    rhi::bindShader(s_waterShader);

    // Transform uniforms.
    rhi::setUniformMat4(s_waterShader, "u_model",           model);
    rhi::setUniformMat4(s_waterShader, "u_viewProjection",  vpMatrix);
    rhi::setUniformFloat(s_waterShader, "u_waterLevel",     waterCfg.waterLevel);

    // Bind reflection texture to unit 0.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_reflColorTex);
    rhi::setUniformInt(s_waterShader, "u_reflectionTex", 0);

    // Bind scene depth texture to unit 1 (from post-process pipeline).
    const u32 sceneDepthTex = getSceneDepthTexture();
    glActiveTexture(GL_TEXTURE0 + 1);
    if (sceneDepthTex != 0) {
        glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    }
    rhi::setUniformInt(s_waterShader, "u_depthTex",    1);
    rhi::setUniformInt(s_waterShader, "u_hasDepthTex", sceneDepthTex != 0 ? 1 : 0);

    // Water config uniforms.
    rhi::setUniformFloat(s_waterShader, "u_time",            time);
    rhi::setUniformVec3(s_waterShader,  "u_cameraPos",       camera3d.position);
    rhi::setUniformVec4(s_waterShader,  "u_shallowColor",    waterCfg.shallowColor);
    rhi::setUniformVec4(s_waterShader,  "u_deepColor",       waterCfg.deepColor);
    rhi::setUniformFloat(s_waterShader, "u_maxDepth",        waterCfg.maxDepth);
    rhi::setUniformFloat(s_waterShader, "u_waveSpeed",       waterCfg.waveSpeed);
    rhi::setUniformFloat(s_waterShader, "u_waveScale",       waterCfg.waveScale);
    rhi::setUniformFloat(s_waterShader, "u_fresnelPower",    waterCfg.fresnelPower);
    rhi::setUniformFloat(s_waterShader, "u_fresnelBias",     waterCfg.fresnelBias);
    rhi::setUniformFloat(s_waterShader, "u_reflDistortion",  waterCfg.reflectionDistortion);

    // Camera planes for depth linearisation.
    rhi::setUniformFloat(s_waterShader, "u_nearPlane", camera3d.nearPlane);
    rhi::setUniformFloat(s_waterShader, "u_farPlane",  camera3d.farPlane);

    // Fog uniforms.
    if (fog.enabled) {
        rhi::setUniformInt(s_waterShader,   "u_fogEnabled", 1);
        rhi::setUniformVec3(s_waterShader,  "u_fogColor",
                            glm::vec3(fog.r, fog.g, fog.b));
        rhi::setUniformFloat(s_waterShader, "u_fogNear", fog.nearDist);
        rhi::setUniformFloat(s_waterShader, "u_fogFar",  fog.farDist);
    } else {
        rhi::setUniformInt(s_waterShader, "u_fogEnabled", 0);
    }

    // Draw water quad.
    glBindVertexArray(s_waterVao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Unbind textures.
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore pipeline state.
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void shutdownWater() {
    if (!s_initialised) { return; }

    destroyReflectionFbo();
    destroyWaterQuad();

    // Clear shader handle -- do NOT destroy it here. ShaderLibrary owns it.
    s_waterShader = {};

    s_initialised = false;
    s_width       = 0;
    s_height      = 0;

    FFE_LOG_INFO("Water", "Water system shutdown");
}

void setWaterShader(const rhi::ShaderHandle water) {
    s_waterShader = water;
}

// ===========================================================================
// WaterManager implementation
// ===========================================================================

// ---------------------------------------------------------------------------
// Internal vertex type for WaterManager quads.
// Same layout as WaterVertex: vec3 position (12 bytes) + vec2 texCoord (8 bytes).
// ---------------------------------------------------------------------------

namespace {

struct WMVertex {
    float px, py, pz; // position
    float u, v;       // texcoord
};
static_assert(sizeof(WMVertex) == 20);

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WaterManager::init(const int screenWidth, const int screenHeight) {
    m_screenWidth  = screenWidth;
    m_screenHeight = screenHeight;
    m_time         = 0.0f;
    m_nextHandleId = 1;

    // Zero-initialise all slots.
    for (u32 i = 0; i < MAX_WATER_SURFACES; ++i) {
        m_slots[i] = WaterSlot{};
    }

    m_initialised = true;
    FFE_LOG_INFO("WaterManager", "Initialised (%dx%d)", screenWidth, screenHeight);
}

void WaterManager::shutdown() {
    if (!m_initialised) { return; }

    for (u32 i = 0; i < MAX_WATER_SURFACES; ++i) {
        WaterSlot& slot = m_slots[i];
        if (!slot.active) { continue; }

        freeReflectionFbo(slot);

        if (slot.vao != 0) {
            glDeleteVertexArrays(1, &slot.vao);
            slot.vao = 0;
        }
        if (slot.vbo != 0) {
            glDeleteBuffers(1, &slot.vbo);
            slot.vbo = 0;
        }
        if (slot.ibo != 0) {
            glDeleteBuffers(1, &slot.ibo);
            slot.ibo = 0;
        }
        slot.active = false;
        slot.handle = {};
    }

    m_waterShader = {0};
    m_initialised = false;
    FFE_LOG_INFO("WaterManager", "Shutdown");
}

// ---------------------------------------------------------------------------
// Shader handle injection
// ---------------------------------------------------------------------------

void WaterManager::setShaderHandle(const rhi::ShaderHandle waterShader) {
    m_waterShader = waterShader;
}

// ---------------------------------------------------------------------------
// Surface management
// ---------------------------------------------------------------------------

WaterHandle WaterManager::createWater(const WaterPlane& plane,
                                      const WaterSurfaceConfig& cfg) {
    if (!m_initialised) {
        FFE_LOG_WARN("WaterManager", "createWater: not initialised");
        return WaterHandle{0};
    }

    // Find a free slot.
    u32 slotIdx = MAX_WATER_SURFACES;
    for (u32 i = 0; i < MAX_WATER_SURFACES; ++i) {
        if (!m_slots[i].active) {
            slotIdx = i;
            break;
        }
    }
    if (slotIdx == MAX_WATER_SURFACES) {
        FFE_LOG_WARN("WaterManager", "createWater: MAX_WATER_SURFACES (%u) reached",
                     MAX_WATER_SURFACES);
        return WaterHandle{0};
    }

    WaterSlot& slot  = m_slots[slotIdx];
    slot.active      = true;
    slot.handle      = WaterHandle{m_nextHandleId++};
    slot.plane       = plane;
    slot.config      = cfg;
    slot.vao         = 0;
    slot.vbo         = 0;
    slot.ibo         = 0;
    slot.reflFbo     = 0;
    slot.reflColorTex = 0;
    slot.reflDepthRbo = 0;
    slot.reflTexture  = {0};

    // Skip GL calls in headless mode.
    if (glad_glGenVertexArrays != nullptr) {
        uploadQuadGeometry(slot);

        // Allocate reflection FBO on STANDARD+ (not software renderer, not headless).
        if (cfg.reflectionEnabled && !rhi::isSoftwareRenderer()) {
            const bool ok = allocReflectionFbo(slot);
            if (!ok) {
                FFE_LOG_WARN("WaterManager", "createWater: reflection FBO incomplete for slot %u",
                             slotIdx);
            }
        }
    }

    FFE_LOG_INFO("WaterManager", "Created water surface id=%u at (%.1f,%.1f,%.1f) %0.fx%0.f",
                 slot.handle.id, plane.x, plane.y, plane.z, plane.width, plane.depth);
    return slot.handle;
}

void WaterManager::destroyWater(const WaterHandle handle) {
    const u32 idx = findSlot(handle);
    if (idx == MAX_WATER_SURFACES) { return; } // No-op on invalid handle.

    WaterSlot& slot = m_slots[idx];

    freeReflectionFbo(slot);

    if (slot.vao != 0) {
        glDeleteVertexArrays(1, &slot.vao);
        slot.vao = 0;
    }
    if (slot.vbo != 0) {
        glDeleteBuffers(1, &slot.vbo);
        slot.vbo = 0;
    }
    if (slot.ibo != 0) {
        glDeleteBuffers(1, &slot.ibo);
        slot.ibo = 0;
    }

    slot.active = false;
    slot.handle = {};
    slot.plane  = {};
    slot.config = {};
}

void WaterManager::setWaterConfig(const WaterHandle handle,
                                  const WaterSurfaceConfig& cfg) {
    const u32 idx = findSlot(handle);
    if (idx == MAX_WATER_SURFACES) { return; }
    m_slots[idx].config = cfg;
}

WaterSurfaceConfig WaterManager::getWaterConfig(const WaterHandle handle) const {
    const u32 idx = findSlot(handle);
    if (idx == MAX_WATER_SURFACES) { return WaterSurfaceConfig{}; }
    return m_slots[idx].config;
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void WaterManager::update(const float dt) {
    m_time += dt;
}

// ---------------------------------------------------------------------------
// Reflection pass
// ---------------------------------------------------------------------------

void WaterManager::renderReflection(void (* const renderScene)(void* userData),
                                    void* const userData) {
    if (!m_initialised) { return; }
    if (rhi::isSoftwareRenderer()) { return; }
    if (glad_glGenVertexArrays == nullptr) { return; } // Headless guard.

    for (u32 i = 0; i < MAX_WATER_SURFACES; ++i) {
        const WaterSlot& slot = m_slots[i];
        if (!slot.active) { continue; }
        if (!slot.config.reflectionEnabled) { continue; }
        if (slot.reflFbo == 0) { continue; }

        // Save current FBO and viewport.
        GLint savedFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);
        const i32 savedVpW = rhi::getViewportWidth();
        const i32 savedVpH = rhi::getViewportHeight();

        const i32 reflW = (m_screenWidth  / 2 > 0) ? m_screenWidth  / 2 : 1;
        const i32 reflH = (m_screenHeight / 2 > 0) ? m_screenHeight / 2 : 1;

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(slot.reflFbo));
        glViewport(0, 0, static_cast<GLsizei>(reflW), static_cast<GLsizei>(reflH));
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Invoke the caller-supplied scene render callback.
        if (renderScene != nullptr) {
            renderScene(userData);
        }

        // Restore previous FBO and viewport.
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo));
        glViewport(0, 0,
                   static_cast<GLsizei>(savedVpW),
                   static_cast<GLsizei>(savedVpH));
    }
}

// ---------------------------------------------------------------------------
// Water render pass
// ---------------------------------------------------------------------------

void WaterManager::render(const glm::mat4& view, const glm::mat4& proj,
                          const glm::vec3& cameraPos,
                          const glm::vec3& lightDir,
                          const glm::vec3& lightColor,
                          const glm::vec3& ambientColor,
                          const bool isSoftwareRenderer) {
    if (!m_initialised) { return; }
    if (glad_glGenVertexArrays == nullptr) { return; } // Headless guard.
    if (!rhi::isValid(m_waterShader)) {
        FFE_LOG_WARN("WaterManager", "render: water shader not set -- call setShaderHandle()");
        return;
    }

    const glm::mat4 viewProj = proj * view;

    rhi::bindShader(m_waterShader);

    // Uniforms shared by all surfaces.
    rhi::setUniformVec3(m_waterShader,  "u_cameraPos", cameraPos);
    rhi::setUniformFloat(m_waterShader, "u_time",      m_time);

    // Lighting (passed to surface colour calculation).
    // The existing WATER shader doesn't expose lighting uniforms in the
    // current GLSL source, but we set them here so future shader extensions
    // can pick them up without changing the C++ side.
    // These are ignored by GLSL if no matching uniform location exists.
    rhi::setUniformVec3(m_waterShader, "u_lightDir",     lightDir);
    rhi::setUniformVec3(m_waterShader, "u_lightColor",   lightColor);
    rhi::setUniformVec3(m_waterShader, "u_ambientColor", ambientColor);

    // Disable fog for WaterManager surfaces (no FogParams available in this API).
    rhi::setUniformInt(m_waterShader, "u_fogEnabled", 0);

    // No depth texture available in the WaterManager API (the post-process
    // depth texture is managed by the flat-function API). Disable depth fade.
    rhi::setUniformInt(m_waterShader, "u_hasDepthTex", 0);
    rhi::setUniformInt(m_waterShader, "u_depthTex",    1);

    // Camera planes for depth linearisation (use reasonable defaults).
    rhi::setUniformFloat(m_waterShader, "u_nearPlane", 0.1f);
    rhi::setUniformFloat(m_waterShader, "u_farPlane",  1000.0f);

    for (u32 i = 0; i < MAX_WATER_SURFACES; ++i) {
        const WaterSlot& slot = m_slots[i];
        if (!slot.active || slot.vao == 0) { continue; }

        const WaterSurfaceConfig& cfg = slot.config;
        const WaterPlane& pl          = slot.plane;

        // Build model matrix: translate to plane position, scale to plane extents.
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(pl.x, pl.y, pl.z));
        model = glm::scale(model, glm::vec3(pl.width, 1.0f, pl.depth));

        rhi::setUniformMat4(m_waterShader,  "u_model",          model);
        rhi::setUniformMat4(m_waterShader,  "u_viewProjection", viewProj);
        rhi::setUniformFloat(m_waterShader, "u_waterLevel",     pl.y);

        // Wave parameters mapped to existing WATER shader uniforms.
        rhi::setUniformFloat(m_waterShader, "u_waveSpeed",   cfg.waveSpeed);
        rhi::setUniformFloat(m_waterShader, "u_waveScale",   cfg.waveAmplitude);
        rhi::setUniformFloat(m_waterShader, "u_fresnelPower", cfg.fresnelPower);
        rhi::setUniformFloat(m_waterShader, "u_fresnelBias",  0.1f); // fixed sensible default

        // Water tint colours (converted to vec4 with fixed alpha for existing shader).
        const glm::vec4 shallowCol{cfg.waterColor, 0.6f};
        const glm::vec4 deepCol{cfg.deepColor, 0.9f};
        rhi::setUniformVec4(m_waterShader, "u_shallowColor", shallowCol);
        rhi::setUniformVec4(m_waterShader, "u_deepColor",    deepCol);
        rhi::setUniformFloat(m_waterShader, "u_maxDepth",    10.0f);

        // Reflection distortion derived from waveScale.
        const float reflDistort = cfg.waveAmplitude * 0.5f;
        rhi::setUniformFloat(m_waterShader, "u_reflDistortion", reflDistort);

        // Bind reflection texture (unit 0).
        glActiveTexture(GL_TEXTURE0);
        if (slot.reflFbo != 0 && !isSoftwareRenderer) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(slot.reflColorTex));
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        rhi::setUniformInt(m_waterShader, "u_reflectionTex", 0);

        // Enable alpha blending for the draw call.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glBindVertexArray(static_cast<GLuint>(slot.vao));
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        // Restore state after each surface.
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// WaterManager internal helpers
// ---------------------------------------------------------------------------

u32 WaterManager::findSlot(const WaterHandle handle) const {
    if (handle.id == 0) { return MAX_WATER_SURFACES; }
    for (u32 i = 0; i < MAX_WATER_SURFACES; ++i) {
        if (m_slots[i].active && m_slots[i].handle.id == handle.id) {
            return i;
        }
    }
    return MAX_WATER_SURFACES; // sentinel = not found
}

void WaterManager::uploadQuadGeometry(WaterSlot& slot) {
    // Unit quad: XZ plane at Y=0, extent [-0.5, 0.5].
    // Model matrix scales to WaterPlane extents at draw time.
    static constexpr WMVertex VERTS[4] = {
        {-0.5f, 0.0f, -0.5f,  0.0f, 0.0f},
        { 0.5f, 0.0f, -0.5f,  1.0f, 0.0f},
        { 0.5f, 0.0f,  0.5f,  1.0f, 1.0f},
        {-0.5f, 0.0f,  0.5f,  0.0f, 1.0f},
    };
    static constexpr u32 IDX[6] = {0u, 1u, 2u, 0u, 2u, 3u};

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(VERTS)),
                 VERTS, GL_STATIC_DRAW);

    // slot 0: vec3 position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(WMVertex)),
                          nullptr);

    // slot 1: vec2 texcoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(WMVertex)),
                          reinterpret_cast<const void*>(
                              static_cast<uintptr_t>(3u * sizeof(float))));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(IDX)),
                 IDX, GL_STATIC_DRAW);

    glBindVertexArray(0);

    slot.vao = static_cast<u32>(vao);
    slot.vbo = static_cast<u32>(vbo);
    slot.ibo = static_cast<u32>(ibo);
}

bool WaterManager::allocReflectionFbo(WaterSlot& slot) {
    const i32 w = (m_screenWidth  / 2 > 0) ? m_screenWidth  / 2 : 1;
    const i32 h = (m_screenHeight / 2 > 0) ? m_screenHeight / 2 : 1;

    // Color attachment: GL_RGBA8, half-res.
    GLuint colorTex = 0;
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(w),
                 static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth attachment: GL_DEPTH_COMPONENT24 renderbuffer (explicit precision required by OpenGL spec).
    GLuint depthRbo = 0;
    glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          static_cast<GLsizei>(w),
                          static_cast<GLsizei>(h));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Assemble FBO.
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthRbo);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("WaterManager", "allocReflectionFbo: incomplete (status 0x%X)",
                      static_cast<unsigned>(status));
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);
        glDeleteRenderbuffers(1, &depthRbo);
        return false;
    }

    slot.reflFbo      = static_cast<u32>(fbo);
    slot.reflColorTex = static_cast<u32>(colorTex);
    slot.reflDepthRbo = static_cast<u32>(depthRbo);
    // reflTexture is left as {0} -- the WaterManager render() path samples
    // slot.reflColorTex directly via glBindTexture rather than through the RHI
    // texture handle table. This avoids registering an external GL name into
    // the RHI texture pool, which has a fixed capacity.
    slot.reflTexture  = {0};
    return true;
}

void WaterManager::freeReflectionFbo(WaterSlot& slot) {
    if (slot.reflFbo != 0) {
        glDeleteFramebuffers(1, &slot.reflFbo);
        slot.reflFbo = 0;
    }
    if (slot.reflColorTex != 0) {
        glDeleteTextures(1, &slot.reflColorTex);
        slot.reflColorTex = 0;
    }
    if (slot.reflDepthRbo != 0) {
        glDeleteRenderbuffers(1, &slot.reflDepthRbo);
        slot.reflDepthRbo = 0;
    }
    slot.reflTexture = {0};
}

} // namespace ffe::renderer
