// water.cpp -- Planar water rendering implementation for FFE.
//
// Reflection FBO management, water quad VAO, reflection pass orchestration,
// and water draw call with fresnel + animated distortion.
//
// All GPU resources are stored as file-static variables. No per-frame heap
// allocation. No virtual functions.
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

#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// File-static GPU resources
// ---------------------------------------------------------------------------

static bool s_initialised = false;
static i32  s_width  = 0;
static i32  s_height = 0;

// Reflection FBO (half resolution)
static GLuint s_reflFbo       = 0;
static GLuint s_reflColorTex  = 0;
static GLuint s_reflDepthRbo  = 0;
static i32    s_reflWidth     = 0;
static i32    s_reflHeight    = 0;

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
    if (s_reflWidth < 1) s_reflWidth = 1;
    if (s_reflHeight < 1) s_reflHeight = 1;

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

    // Depth attachment: GL_DEPTH_COMPONENT16 renderbuffer
    glGenRenderbuffers(1, &s_reflDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s_reflDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
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
    // Guard: no GL context in headless mode.
    if (glad_glGenVertexArrays == nullptr) {
        return;
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
        0, 1, 2,
        0, 2, 3,
    };

    glGenVertexArrays(1, &s_waterVao);
    glGenBuffers(1, &s_waterVbo);
    glGenBuffers(1, &s_waterIbo);

    glBindVertexArray(s_waterVao);

    glBindBuffer(GL_ARRAY_BUFFER, s_waterVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VERTICES), VERTICES, GL_STATIC_DRAW);

    // Position: location 0 (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex),
                          nullptr);

    // TexCoord: location 1 (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WaterVertex),
                          reinterpret_cast<const void*>(
                              static_cast<uintptr_t>(offsetof(WaterVertex, texCoord))));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_waterIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(INDICES), INDICES, GL_STATIC_DRAW);

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
    Camera reflected = camera;
    // Flip position Y across water level
    reflected.position.y = 2.0f * waterLevel - camera.position.y;
    // Flip target Y across water level
    reflected.target.y = 2.0f * waterLevel - camera.target.y;
    // Invert up vector Y to maintain handedness
    reflected.up.y = -camera.up.y;
    return reflected;
}

// ---------------------------------------------------------------------------
// Public API
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
    if (!s_initialised) return;
    if (width <= 0 || height <= 0) return;
    if (width == s_width && height == s_height) return;

    s_width  = width;
    s_height = height;

    destroyReflectionFbo();
    createReflectionFbo(width, height);

    FFE_LOG_INFO("Water", "Water FBOs resized to %dx%d (reflection %dx%d)",
                 width, height, s_reflWidth, s_reflHeight);
}

void renderWaterReflection(World& world, const Camera& camera3d,
                           const FogParams& fog, const WaterConfig& waterCfg) {
    if (!s_initialised) return;
    if (!waterCfg.enabled) return;
    if (s_reflFbo == 0) return;

    // Check if any Water entity exists
    const auto waterView = world.registry().view<const Water>();
    if (waterView.begin() == waterView.end()) return;

    // --- Retrieve shader library from ECS context ---
    const auto* shaderLib = world.registry().ctx().find<ShaderLibrary>();
    if (shaderLib == nullptr) return;

    // Compute reflected camera
    const Camera reflCamera = computeReflectionCamera(camera3d, waterCfg.waterLevel);

    // Save current FBO and viewport
    GLint savedFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);
    const i32 savedVpW = rhi::getViewportWidth();
    const i32 savedVpH = rhi::getViewportHeight();

    // Bind reflection FBO
    glBindFramebuffer(GL_FRAMEBUFFER, s_reflFbo);
    glViewport(0, 0, static_cast<GLsizei>(s_reflWidth),
               static_cast<GLsizei>(s_reflHeight));
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Enable clip plane for reflection pass
    glEnable(GL_CLIP_DISTANCE0);

    // Set clip plane uniform on all relevant shaders
    // Clip plane: vec4(0, 1, 0, -waterLevel) clips everything below the water plane
    const glm::vec4 clipPlane{0.0f, 1.0f, 0.0f, -waterCfg.waterLevel};

    // Upload clip plane to mesh shaders
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

    // Render scene from reflected viewpoint (no shadow pass in reflection)
    const ShadowConfig noShadow{}; // disabled
    const ShadowMap emptyShadowMap{};
    meshRenderSystem(world, reflCamera, noShadow, emptyShadowMap, fog);
    terrainRenderSystem(world, reflCamera, noShadow, emptyShadowMap, fog);

    // Render skybox into reflection
    const auto* const* skyboxCfgPtr = world.registry().ctx().find<SkyboxConfig*>();
    if (skyboxCfgPtr != nullptr && *skyboxCfgPtr != nullptr) {
        renderSkybox(world, reflCamera, **skyboxCfgPtr);
    }

    // Disable clip plane
    glDisable(GL_CLIP_DISTANCE0);

    // Reset clip plane to disabled on all shaders
    const glm::vec4 noClip{0.0f, 0.0f, 0.0f, 0.0f};
    for (const auto& sh : meshShaders) {
        if (rhi::isValid(sh)) {
            rhi::bindShader(sh);
            rhi::setUniformVec4(sh, "u_clipPlane", noClip);
        }
    }

    // Restore previous FBO and viewport
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo));
    glViewport(0, 0, static_cast<GLsizei>(savedVpW),
               static_cast<GLsizei>(savedVpH));
}

void renderWater(World& world, const Camera& camera3d,
                 const FogParams& fog, const WaterConfig& waterCfg,
                 const f32 time) {
    if (!s_initialised) return;
    if (!waterCfg.enabled) return;
    if (!rhi::isValid(s_waterShader)) return;
    if (s_waterVao == 0) return;

    // Check for Water entity with Transform3D
    const auto waterView = world.registry().view<const Water, const Transform3D>();
    if (waterView.begin() == waterView.end()) return;

    // Use first Water entity only (one-plane constraint)
    const auto firstEntity = *waterView.begin();
    const auto& transform = world.registry().get<const Transform3D>(firstEntity);

    // Build model matrix: translate to water level, scale to water extent
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(transform.position.x,
                                             waterCfg.waterLevel,
                                             transform.position.z));
    model = glm::scale(model, glm::vec3(transform.scale.x * DEFAULT_WATER_EXTENT,
                                         1.0f,
                                         transform.scale.z * DEFAULT_WATER_EXTENT));

    const glm::mat4 vpMatrix = computeViewProjectionMatrix(camera3d);

    // Set water pipeline state: alpha blend, depth test read (no write)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE); // Read depth but do not write -- water is transparent
    glDisable(GL_CULL_FACE);

    rhi::bindShader(s_waterShader);

    // Transform uniforms
    rhi::setUniformMat4(s_waterShader, "u_model", model);
    rhi::setUniformMat4(s_waterShader, "u_viewProjection", vpMatrix);
    rhi::setUniformFloat(s_waterShader, "u_waterLevel", waterCfg.waterLevel);

    // Bind reflection texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_reflColorTex);
    rhi::setUniformInt(s_waterShader, "u_reflectionTex", 0);

    // Bind scene depth texture to unit 1 (from post-process pipeline)
    const u32 sceneDepthTex = getSceneDepthTexture();
    glActiveTexture(GL_TEXTURE0 + 1);
    if (sceneDepthTex != 0) {
        glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    }
    rhi::setUniformInt(s_waterShader, "u_depthTex", 1);
    rhi::setUniformInt(s_waterShader, "u_hasDepthTex", sceneDepthTex != 0 ? 1 : 0);

    // Water config uniforms
    rhi::setUniformFloat(s_waterShader, "u_time", time);
    rhi::setUniformVec3(s_waterShader, "u_cameraPos", camera3d.position);
    rhi::setUniformVec4(s_waterShader, "u_shallowColor", waterCfg.shallowColor);
    rhi::setUniformVec4(s_waterShader, "u_deepColor", waterCfg.deepColor);
    rhi::setUniformFloat(s_waterShader, "u_maxDepth", waterCfg.maxDepth);
    rhi::setUniformFloat(s_waterShader, "u_waveSpeed", waterCfg.waveSpeed);
    rhi::setUniformFloat(s_waterShader, "u_waveScale", waterCfg.waveScale);
    rhi::setUniformFloat(s_waterShader, "u_fresnelPower", waterCfg.fresnelPower);
    rhi::setUniformFloat(s_waterShader, "u_fresnelBias", waterCfg.fresnelBias);
    rhi::setUniformFloat(s_waterShader, "u_reflDistortion", waterCfg.reflectionDistortion);

    // Camera planes for depth linearisation
    rhi::setUniformFloat(s_waterShader, "u_nearPlane", camera3d.nearPlane);
    rhi::setUniformFloat(s_waterShader, "u_farPlane", camera3d.farPlane);

    // Fog uniforms
    if (fog.enabled) {
        rhi::setUniformInt(s_waterShader, "u_fogEnabled", 1);
        rhi::setUniformVec3(s_waterShader, "u_fogColor",
                            glm::vec3(fog.r, fog.g, fog.b));
        rhi::setUniformFloat(s_waterShader, "u_fogNear", fog.nearDist);
        rhi::setUniformFloat(s_waterShader, "u_fogFar", fog.farDist);
    } else {
        rhi::setUniformInt(s_waterShader, "u_fogEnabled", 0);
    }

    // Draw water quad
    glBindVertexArray(s_waterVao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Unbind textures
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore 2D-compatible pipeline state
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void shutdownWater() {
    if (!s_initialised) return;

    destroyReflectionFbo();
    destroyWaterQuad();

    // Clear shader handle -- do NOT destroy it here. ShaderLibrary owns it.
    s_waterShader = {};

    s_initialised = false;
    s_width  = 0;
    s_height = 0;

    FFE_LOG_INFO("Water", "Water system shutdown");
}

void setWaterShader(const rhi::ShaderHandle water) {
    s_waterShader = water;
}

} // namespace ffe::renderer
