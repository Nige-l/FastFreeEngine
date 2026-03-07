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
#include "renderer/frustum.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/shader_library.h"
#include "renderer/camera.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
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

// Helper: select LOD level for a chunk based on camera distance.
// Returns the LOD index (0 = highest detail, lodCount-1 = lowest).
static u32 selectLod(const TerrainChunkGpu& chunk, const TerrainLodConfig& lodConfig,
                     const glm::vec3& cameraPos) {
    const f32 dist = glm::length(cameraPos - chunk.center);
    u32 lod = 0;
    if (dist > lodConfig.lodDistances[0] && chunk.lodCount > 1) { lod = 1; }
    if (dist > lodConfig.lodDistances[1] && chunk.lodCount > 2) { lod = 2; }
    return std::min(lod, chunk.lodCount - 1);
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
                    if (chunk.lodCount == 0) { continue; }
                    // Shadow pass uses LOD 0 (full detail) for accuracy.
                    const TerrainChunkLod& lod = chunk.lods[0];
                    if (lod.vaoId == 0 || lod.indexCount == 0) { continue; }

                    glBindVertexArray(lod.vaoId);
                    glDrawElements(GL_TRIANGLES,
                                   static_cast<GLsizei>(lod.indexCount),
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

    // --- Retrieve TERRAIN shader for splat-map path ---
    const rhi::ShaderHandle terrainShader = getShader(*shaderLib, BuiltinShader::TERRAIN);

    // --- Extract view frustum for culling ---
    const Frustum frustum = extractFrustum(vpMatrix);

    // --- Draw terrain entities ---
    for (const auto [entity, transform3d, terrainComp] : view.each()) {
        const TerrainAsset* asset = getTerrainAsset(terrainComp.terrainHandle);
        if (asset == nullptr) { continue; }

        const glm::mat4 model = buildTerrainModelMatrix(transform3d);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

        // Decide which shader path: TERRAIN (splat map) or MESH_BLINN_PHONG (fallback)
        const bool useSplatMap = rhi::isValid(asset->material.splatTexture) &&
                                 rhi::isValid(terrainShader);

        if (useSplatMap) {
            // --- M2 splat-map rendering path (TERRAIN shader) ---
            rhi::bindShader(terrainShader);

            // Scene-global uniforms (same as MESH_BLINN_PHONG)
            rhi::setUniformVec3(terrainShader, "u_lightDir",        lighting->lightDir);
            rhi::setUniformVec3(terrainShader, "u_lightColor",      lighting->lightColor);
            rhi::setUniformVec3(terrainShader, "u_ambientColor",    lighting->ambientColor);
            rhi::setUniformVec3(terrainShader, "u_viewPos",         camera3d.position);
            rhi::setUniformMat4(terrainShader, "u_viewProjection",  vpMatrix);

            // Point light uniforms
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

                rhi::setUniformInt(terrainShader, "u_pointLightCount", static_cast<i32>(activeCount));

                char uniformName[64];
                for (u32 i = 0; i < activeCount; ++i) {
                    const auto uIdx = static_cast<unsigned int>(i);
                    std::snprintf(uniformName, sizeof(uniformName), "u_pointLightPos[%u]", uIdx);
                    rhi::setUniformVec3(terrainShader, uniformName, positions[i]);
                    std::snprintf(uniformName, sizeof(uniformName), "u_pointLightColor[%u]", uIdx);
                    rhi::setUniformVec3(terrainShader, uniformName, colors[i]);
                    std::snprintf(uniformName, sizeof(uniformName), "u_pointLightRadius[%u]", uIdx);
                    rhi::setUniformFloat(terrainShader, uniformName, radii[i]);
                }
            }

            // Shadow map uniforms
            if (shadowCfg.enabled && shadowMap.depthTexture != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);
                rhi::setUniformInt(terrainShader, "u_shadowMap", 0);
                rhi::setUniformInt(terrainShader, "u_shadowsEnabled", 1);
                rhi::setUniformFloat(terrainShader, "u_shadowBias", shadowCfg.bias);
                rhi::setUniformMat4(terrainShader, "u_lightSpaceMatrix", lightSpaceMat);
            } else {
                rhi::setUniformInt(terrainShader, "u_shadowsEnabled", 0);
                rhi::setUniformMat4(terrainShader, "u_lightSpaceMatrix", glm::mat4{1.0f});
            }

            // Fog uniforms
            if (fog.enabled) {
                rhi::setUniformInt(terrainShader, "u_fogEnabled", 1);
                rhi::setUniformVec3(terrainShader, "u_fogColor", glm::vec3(fog.r, fog.g, fog.b));
                rhi::setUniformFloat(terrainShader, "u_fogNear", fog.nearDist);
                rhi::setUniformFloat(terrainShader, "u_fogFar", fog.farDist);
            } else {
                rhi::setUniformInt(terrainShader, "u_fogEnabled", 0);
            }

            // Per-terrain uniforms
            rhi::setUniformMat4(terrainShader, "u_model", model);
            rhi::setUniformMat3(terrainShader, "u_normalMatrix", normalMatrix);

            // Bind splat map to texture unit 1
            rhi::bindTexture(asset->material.splatTexture, 1);
            rhi::setUniformInt(terrainShader, "u_splatMap", 1);

            // Bind layer textures to units 2-5
            for (u32 li = 0; li < 4; ++li) {
                if (rhi::isValid(asset->material.layers[li].texture)) {
                    rhi::bindTexture(asset->material.layers[li].texture, 2 + li);
                } else if (rhi::isValid(defaultWhite)) {
                    rhi::bindTexture(defaultWhite, 2 + li);
                } else {
                    rhi::bindTexture(rhi::TextureHandle{0}, 2 + li);
                }
            }
            rhi::setUniformInt(terrainShader, "u_layer0", 2);
            rhi::setUniformInt(terrainShader, "u_layer1", 3);
            rhi::setUniformInt(terrainShader, "u_layer2", 4);
            rhi::setUniformInt(terrainShader, "u_layer3", 5);

            // Layer UV scales
            rhi::setUniformFloat(terrainShader, "u_layerScale0", asset->material.layers[0].uvScale);
            rhi::setUniformFloat(terrainShader, "u_layerScale1", asset->material.layers[1].uvScale);
            rhi::setUniformFloat(terrainShader, "u_layerScale2", asset->material.layers[2].uvScale);
            rhi::setUniformFloat(terrainShader, "u_layerScale3", asset->material.layers[3].uvScale);

            // Triplanar uniforms
            rhi::setUniformInt(terrainShader, "u_triplanarEnabled",
                               asset->material.triplanarEnabled ? 1 : 0);
            rhi::setUniformFloat(terrainShader, "u_triplanarThreshold",
                                 asset->material.triplanarThreshold);

            // Draw visible chunks with LOD selection
            for (u32 ci = 0; ci < asset->chunkCount; ++ci) {
                const TerrainChunkGpu& chunk = asset->chunks[ci];
                if (chunk.lodCount == 0) { continue; }

                // Frustum cull
                if (!isAABBVisible(frustum, chunk.aabbMin, chunk.aabbMax)) { continue; }

                // LOD selection based on distance
                const u32 lodIdx = selectLod(chunk, asset->lodConfig, camera3d.position);
                const TerrainChunkLod& lod = chunk.lods[lodIdx];
                if (lod.vaoId == 0 || lod.indexCount == 0) { continue; }

                glBindVertexArray(lod.vaoId);
                glDrawElements(GL_TRIANGLES,
                               static_cast<GLsizei>(lod.indexCount),
                               GL_UNSIGNED_INT, nullptr);
            }

            // Unbind splat-map textures
            for (u32 li = 0; li < 6; ++li) {
                glActiveTexture(GL_TEXTURE0 + li);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // Re-bind mesh shader for any subsequent entities using fallback path
            rhi::bindShader(meshShader);
        } else {
            // --- M1 fallback path (MESH_BLINN_PHONG shader) ---
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

            // Draw visible chunks with LOD selection
            for (u32 ci = 0; ci < asset->chunkCount; ++ci) {
                const TerrainChunkGpu& chunk = asset->chunks[ci];
                if (chunk.lodCount == 0) { continue; }

                // Frustum cull
                if (!isAABBVisible(frustum, chunk.aabbMin, chunk.aabbMax)) { continue; }

                // LOD selection based on distance
                const u32 lodIdx = selectLod(chunk, asset->lodConfig, camera3d.position);
                const TerrainChunkLod& lod = chunk.lods[lodIdx];
                if (lod.vaoId == 0 || lod.indexCount == 0) { continue; }

                glBindVertexArray(lod.vaoId);
                glDrawElements(GL_TRIANGLES,
                               static_cast<GLsizei>(lod.indexCount),
                               GL_UNSIGNED_INT, nullptr);
            }
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
