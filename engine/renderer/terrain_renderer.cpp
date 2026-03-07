// terrain_renderer.cpp -- Terrain ECS render system implementation for FFE.
//
// Queries ECS for entities with Transform3D + Terrain components.
// Sets up depth test, uploads per-entity uniforms, and issues indexed draw calls
// per chunk using the MESH_BLINN_PHONG shader.
//
// Participates in shadow mapping: if shadowCfg.enabled and shadowMap.fbo != 0,
// a depth-only pre-pass renders terrain chunks into the shadow FBO.
//
// No per-frame allocations. All GPU resources are owned by the terrain asset cache.
//
// Tier: LEGACY (OpenGL 3.3 core). No DSA.

#include "renderer/terrain_renderer.h"
#include "renderer/terrain_internal.h"
#include "renderer/terrain.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/shader_library.h"
#include "renderer/camera.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

namespace ffe::renderer {

// Helper: build model matrix from Transform3D.
static glm::mat4 buildTerrainModelMatrix(const Transform3D& t) {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

void terrainRenderSystem(World& world, const Camera& camera3d,
                         const ShadowConfig& shadowCfg, const ShadowMap& shadowMap,
                         const FogParams& fog) {
    // --- Query ECS for terrain entities ---
    auto view = world.registry().view<const Transform3D, const Terrain>();
    if (view.begin() == view.end()) {
        return; // No terrain entities -- skip all pipeline state changes.
    }

    // --- Retrieve shader library from ECS context ---
    const auto* shaderLib = world.registry().ctx().find<renderer::ShaderLibrary>();
    if (shaderLib == nullptr) {
        return;
    }

    const rhi::ShaderHandle meshShader = getShader(*shaderLib, BuiltinShader::MESH_BLINN_PHONG);
    if (!rhi::isValid(meshShader)) {
        return;
    }

    // --- Retrieve scene lighting ---
    const SceneLighting3D* lighting = world.registry().ctx().find<SceneLighting3D>();
    SceneLighting3D defaultLighting;
    if (lighting == nullptr) {
        lighting = &defaultLighting;
    }

    // --- Retrieve default white texture ---
    rhi::TextureHandle defaultWhite{};
    {
        const rhi::TextureHandle* texPtr = world.registry().ctx().find<rhi::TextureHandle>();
        if (texPtr != nullptr) {
            defaultWhite = *texPtr;
        }
    }

    // --- Compute light-space matrix for shadows ---
    glm::mat4 lightSpaceMat{1.0f};
    if (shadowCfg.enabled) {
        lightSpaceMat = computeLightSpaceMatrix(lighting->lightDir, shadowCfg);
    }

    // --- Shadow depth pre-pass ---
    if (shadowCfg.enabled && shadowMap.fbo != 0) {
        const rhi::ShaderHandle shadowShader = getShader(*shaderLib, BuiltinShader::SHADOW_DEPTH);
        if (rhi::isValid(shadowShader)) {
            const i32 savedVpW = rhi::getViewportWidth();
            const i32 savedVpH = rhi::getViewportHeight();
            GLint savedFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);

            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
            glViewport(0, 0,
                       static_cast<GLsizei>(shadowMap.resolution),
                       static_cast<GLsizei>(shadowMap.resolution));
            // NOTE: do NOT clear here -- meshRenderSystem already cleared the shadow FBO.
            // We append terrain geometry to the existing shadow depth buffer.

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            rhi::bindShader(shadowShader);
            rhi::setUniformMat4(shadowShader, "u_lightSpaceMatrix", lightSpaceMat);

            for (const auto [entity, transform3d, terrainComp] : view.each()) {
                const TerrainAsset* asset = getTerrainAsset(terrainComp.terrainHandle);
                if (asset == nullptr) { continue; }

                const glm::mat4 model = buildTerrainModelMatrix(transform3d);
                rhi::setUniformMat4(shadowShader, "u_model", model);

                for (u32 ci = 0; ci < asset->chunkCount; ++ci) {
                    const TerrainChunkGpu& chunk = asset->chunks[ci];
                    if (chunk.vaoId == 0 || chunk.indexCount == 0) { continue; }

                    glBindVertexArray(chunk.vaoId);
                    glDrawElements(GL_TRIANGLES,
                                   static_cast<GLsizei>(chunk.indexCount),
                                   GL_UNSIGNED_INT, nullptr);
                }
            }
            glBindVertexArray(0);

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo));
            glViewport(0, 0, static_cast<GLsizei>(savedVpW), static_cast<GLsizei>(savedVpH));
            glCullFace(GL_BACK);
        }
    }

    // --- Main terrain render pass ---
    // Set 3D pipeline state
    rhi::PipelineState ps3d;
    ps3d.blend      = rhi::BlendMode::NONE;
    ps3d.depth      = rhi::DepthFunc::LESS;
    ps3d.cull       = rhi::CullMode::BACK;
    ps3d.depthWrite = true;
    ps3d.wireframe  = false;
    rhi::applyPipelineState(ps3d);

    const glm::mat4 vpMatrix = computeViewProjectionMatrix(camera3d);

    rhi::bindShader(meshShader);

    // Upload scene-global uniforms
    rhi::setUniformVec3(meshShader, "u_lightDir",        lighting->lightDir);
    rhi::setUniformVec3(meshShader, "u_lightColor",      lighting->lightColor);
    rhi::setUniformVec3(meshShader, "u_ambientColor",    lighting->ambientColor);
    rhi::setUniformVec3(meshShader, "u_viewPos",         camera3d.position);
    rhi::setUniformMat4(meshShader, "u_viewProjection",  vpMatrix);

    // Upload point light uniforms
    {
        u32 activeCount = 0;
        glm::vec3 positions[MAX_POINT_LIGHTS];
        glm::vec3 colors[MAX_POINT_LIGHTS];
        float radii[MAX_POINT_LIGHTS];

        for (u32 i = 0; i < MAX_POINT_LIGHTS; ++i) {
            if (lighting->pointLights[i].active) {
                positions[activeCount] = lighting->pointLights[i].position;
                colors[activeCount]    = lighting->pointLights[i].color;
                radii[activeCount]     = lighting->pointLights[i].radius;
                ++activeCount;
            }
        }

        rhi::setUniformInt(meshShader, "u_pointLightCount", static_cast<i32>(activeCount));

        char uniformName[64];
        for (u32 i = 0; i < activeCount; ++i) {
            const auto uIdx = static_cast<unsigned int>(i);
            std::snprintf(uniformName, sizeof(uniformName), "u_pointLightPos[%u]", uIdx);
            rhi::setUniformVec3(meshShader, uniformName, positions[i]);
            std::snprintf(uniformName, sizeof(uniformName), "u_pointLightColor[%u]", uIdx);
            rhi::setUniformVec3(meshShader, uniformName, colors[i]);
            std::snprintf(uniformName, sizeof(uniformName), "u_pointLightRadius[%u]", uIdx);
            rhi::setUniformFloat(meshShader, uniformName, radii[i]);
        }
    }

    // Shadow map uniforms
    if (shadowCfg.enabled && shadowMap.depthTexture != 0) {
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);
        rhi::setUniformInt(meshShader, "u_shadowMap", 1);
        rhi::setUniformInt(meshShader, "u_shadowsEnabled", 1);
        rhi::setUniformFloat(meshShader, "u_shadowBias", shadowCfg.bias);
        rhi::setUniformMat4(meshShader, "u_lightSpaceMatrix", lightSpaceMat);
    } else {
        rhi::setUniformInt(meshShader, "u_shadowsEnabled", 0);
    }

    // Fog uniforms
    if (fog.enabled) {
        rhi::setUniformInt(meshShader, "u_fogEnabled", 1);
        rhi::setUniformVec3(meshShader, "u_fogColor", glm::vec3(fog.r, fog.g, fog.b));
        rhi::setUniformFloat(meshShader, "u_fogNear", fog.nearDist);
        rhi::setUniformFloat(meshShader, "u_fogFar", fog.farDist);
    } else {
        rhi::setUniformInt(meshShader, "u_fogEnabled", 0);
    }

    // --- Draw terrain entities ---
    for (const auto [entity, transform3d, terrainComp] : view.each()) {
        const TerrainAsset* asset = getTerrainAsset(terrainComp.terrainHandle);
        if (asset == nullptr) { continue; }

        const glm::mat4 model = buildTerrainModelMatrix(transform3d);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        rhi::setUniformMat4(meshShader, "u_model", model);
        rhi::setUniformMat3(meshShader, "u_normalMatrix", normalMatrix);

        // Material: use terrain diffuse texture or default green-ish color
        if (rhi::isValid(asset->diffuseTexture)) {
            rhi::bindTexture(asset->diffuseTexture, 0);
            rhi::setUniformInt(meshShader, "u_diffuseTexture", 0);
            rhi::setUniformVec4(meshShader, "u_diffuseColor", {1.0f, 1.0f, 1.0f, 1.0f});
        } else if (rhi::isValid(defaultWhite)) {
            rhi::bindTexture(defaultWhite, 0);
            rhi::setUniformInt(meshShader, "u_diffuseTexture", 0);
            // Default terrain color: earthy green
            rhi::setUniformVec4(meshShader, "u_diffuseColor", {0.3f, 0.5f, 0.2f, 1.0f});
        }

        // Default specular: low shininess for terrain
        rhi::setUniformVec3(meshShader, "u_specularColor", {0.2f, 0.2f, 0.2f});
        rhi::setUniformFloat(meshShader, "u_shininess", 8.0f);
        rhi::setUniformInt(meshShader, "u_hasNormalMap", 0);
        rhi::setUniformInt(meshShader, "u_hasSpecularMap", 0);

        // Draw all chunks
        for (u32 ci = 0; ci < asset->chunkCount; ++ci) {
            const TerrainChunkGpu& chunk = asset->chunks[ci];
            if (chunk.vaoId == 0 || chunk.indexCount == 0) { continue; }

            glBindVertexArray(chunk.vaoId);
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(chunk.indexCount),
                           GL_UNSIGNED_INT, nullptr);
        }
    }
    glBindVertexArray(0);

    // --- Unbind shadow texture ---
    if (shadowCfg.enabled && shadowMap.depthTexture != 0) {
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    // --- Restore 2D-compatible pipeline state ---
    rhi::PipelineState ps2d;
    ps2d.blend      = rhi::BlendMode::ALPHA;
    ps2d.depth      = rhi::DepthFunc::NONE;
    ps2d.cull       = rhi::CullMode::NONE;
    ps2d.depthWrite = false;
    ps2d.wireframe  = false;
    rhi::applyPipelineState(ps2d);
}

} // namespace ffe::renderer
