// mesh_renderer.cpp — 3D mesh render system implementation for FFE.
//
// Queries ECS for entities with Transform3D + Mesh components.
// Sets up depth test, uploads per-entity uniforms, and issues indexed draw calls.
// After all mesh entities, restores 2D-compatible pipeline state.
//
// Shadow mapping: if shadowCfg.enabled and shadowMap.fbo != 0, a depth-only
// pre-pass renders all mesh entities into the shadow FBO using the SHADOW_DEPTH
// shader. The resulting depth texture is then bound to texture unit 1 during
// the main Blinn-Phong pass and sampled with 3x3 PCF in the fragment shader.
//
// GPU instancing: entities sharing the same MeshHandle are batched into a single
// glDrawElementsInstanced call. Model matrices are uploaded via a shared instance
// VBO with vertex attribute divisors (slots 8-11). Skinned entities are excluded
// from instancing (they need per-entity bone matrices).
//
// Tier: LEGACY (OpenGL 3.3 core). No DSA.

#include "renderer/mesh_renderer.h"
#include "renderer/gpu_instancing.h"
#include "renderer/mesh_loader.h"
#include "renderer/pbr_material.h"
#include "renderer/render_system.h"
#include "renderer/skeleton.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/shader_library.h"
#include "renderer/shadow_map.h"
#include "renderer/camera.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <algorithm>

// GL_TEXTURE_CUBE_MAP is core in OpenGL 3.3 but not included in our minimal GLAD header.
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// GPU instancing state — shared instance VBO for per-instance model matrices.
// Created once at init time, updated per-frame via glBufferSubData.
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static GLuint s_instanceVbo = 0;

void initInstancing() {
    if (s_instanceVbo != 0) {
        return; // Already initialised.
    }
    // Guard: no GL context in headless mode.
    if (glad_glGenBuffers == nullptr) {
        return;
    }

    glGenBuffers(1, &s_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, INSTANCE_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void shutdownInstancing() {
    if (s_instanceVbo != 0) {
        glDeleteBuffers(1, &s_instanceVbo);
        s_instanceVbo = 0;
    }
}

// Bind the instance VBO to a mesh VAO and configure attribute divisors for
// slots 8-11 (mat4 model matrix, one vec4 per column) and slot 12 (instance color).
// Must be called with the target VAO already bound.
static void setupInstanceAttribs() {
    glBindBuffer(GL_ARRAY_BUFFER, s_instanceVbo);

    // Model matrix: 4 vec4 columns at slots 8-11
    for (u32 col = 0; col < INSTANCE_MAT4_SLOT_COUNT; ++col) {
        const u32 slot = INSTANCE_ATTR_SLOT_BASE + col;
        glEnableVertexAttribArray(slot);
        glVertexAttribPointer(
            slot,
            4,                                          // vec4 per column
            GL_FLOAT,
            GL_FALSE,
            sizeof(InstanceData),                       // stride = 80 bytes
            reinterpret_cast<const void*>(              // offset into InstanceData
                static_cast<uintptr_t>(col * sizeof(glm::vec4)))
        );
        glVertexAttribDivisor(slot, 1);                 // advance per instance
    }

    // Instance color: vec4 at slot 12, offset = sizeof(mat4) = 64 bytes
    glEnableVertexAttribArray(INSTANCE_COLOR_SLOT);
    glVertexAttribPointer(
        INSTANCE_COLOR_SLOT,
        4,                                              // vec4
        GL_FLOAT,
        GL_FALSE,
        sizeof(InstanceData),                           // stride = 80 bytes
        reinterpret_cast<const void*>(
            static_cast<uintptr_t>(sizeof(glm::mat4)))  // offset past model matrix
    );
    glVertexAttribDivisor(INSTANCE_COLOR_SLOT, 1);
}

// Disable instance attribute slots on a VAO to restore single-draw state.
// Must be called with the target VAO already bound.
static void teardownInstanceAttribs() {
    for (u32 col = 0; col < INSTANCE_MAT4_SLOT_COUNT; ++col) {
        const u32 slot = INSTANCE_ATTR_SLOT_BASE + col;
        glDisableVertexAttribArray(slot);
        glVertexAttribDivisor(slot, 0);
    }
    glDisableVertexAttribArray(INSTANCE_COLOR_SLOT);
    glVertexAttribDivisor(INSTANCE_COLOR_SLOT, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

u32 getInstanceCount(World& world, const u32 meshHandleId) {
    u32 count = 0;
    auto view = world.registry().view<const Mesh>();
    for (const auto [entity, meshComp] : view.each()) {
        if (meshComp.meshHandle.id == meshHandleId) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Skybox unit cube — created once, never destroyed.
// 36 vertices (6 faces * 2 triangles * 3 verts), no index buffer.
// Positions form a [-1, +1] cube centered at origin. The vertex shader
// strips the view translation so the cube moves with the camera.
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static GLuint s_skyboxVao = 0;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static GLuint s_skyboxVbo = 0;

static void ensureSkyboxCube() {
    if (s_skyboxVao != 0) {
        return; // Already initialised.
    }
    // Guard: no GL context in headless mode.
    if (glad_glGenVertexArrays == nullptr) {
        return;
    }

    // 36 vertices = 6 faces * 2 triangles * 3 verts * 3 floats = 108 floats
    static constexpr float CUBE_VERTICES[] = {
        // Back face (-Z)
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        // Front face (+Z)
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        // Left face (-X)
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        // Right face (+X)
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        // Top face (+Y)
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        // Bottom face (-Y)
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
    };

    glGenVertexArrays(1, &s_skyboxVao);
    glGenBuffers(1, &s_skyboxVbo);
    glBindVertexArray(s_skyboxVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_skyboxVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTICES), CUBE_VERTICES, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void renderSkybox(World& world, const Camera& camera3d, const SkyboxConfig& skyboxCfg) {
    // Early out if skybox is not active.
    if (!skyboxCfg.enabled || skyboxCfg.cubemapTexture == 0) {
        return;
    }

    // Retrieve shader library from ECS context.
    const auto* shaderLib = world.registry().ctx().find<renderer::ShaderLibrary>();
    if (shaderLib == nullptr) {
        return;
    }

    const rhi::ShaderHandle skyboxShader = getShader(*shaderLib, BuiltinShader::SKYBOX);
    if (!rhi::isValid(skyboxShader)) {
        return;
    }

    // Lazy-init the cube VAO (one-time allocation).
    ensureSkyboxCube();
    if (s_skyboxVao == 0) {
        return; // GL not available (headless).
    }

    // Build view matrix with translation stripped (mat3 -> mat4 drops position).
    const glm::mat4 viewFull = computeViewMatrix(camera3d);
    const glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(viewFull));
    const glm::mat4 projection = computeProjectionMatrix(camera3d);

    // Enable depth test (meshRenderSystem may have restored 2D state with depth disabled).
    // Use LEQUAL so skybox fragments pass at depth = 1.0 (the vertex shader outputs
    // z = w, which becomes depth = 1.0 after perspective divide).
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); // Do not write to depth buffer — skybox is always at max depth.

    rhi::bindShader(skyboxShader);
    rhi::setUniformMat4(skyboxShader, "u_viewNoTranslation", viewNoTranslation);
    rhi::setUniformMat4(skyboxShader, "u_projection", projection);

    // Bind cubemap texture to unit 0.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCfg.cubemapTexture);
    rhi::setUniformInt(skyboxShader, "u_skybox", 0);

    // Draw the skybox cube.
    glBindVertexArray(s_skyboxVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // Unbind cubemap.
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    // Restore 2D-compatible state: depth disabled, depth write off.
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------------------
// Helper: upload material uniforms for a single entity.
// Separated to share between single-draw and instanced batch (first entity's material).
// ---------------------------------------------------------------------------
static void uploadPbrMaterial(const rhi::ShaderHandle shader,
                              const renderer::PBRMaterial& mat,
                              const rhi::TextureHandle defaultWhite) {
    rhi::setUniformVec4(shader,  "u_albedo",          mat.albedo);
    rhi::setUniformFloat(shader, "u_metallic",        mat.metallic);
    rhi::setUniformFloat(shader, "u_roughness",       mat.roughness);
    rhi::setUniformFloat(shader, "u_normalScale",     mat.normalScale);
    rhi::setUniformFloat(shader, "u_ao",              mat.ao);
    rhi::setUniformVec3(shader,  "u_emissiveFactor",  mat.emissiveFactor);

    if (rhi::isValid(mat.albedoMap)) {
        rhi::bindTexture(mat.albedoMap, 0);
        rhi::setUniformInt(shader, "u_albedoMap", 0);
        rhi::setUniformInt(shader, "u_hasAlbedoMap", 1);
    } else {
        rhi::bindTexture(defaultWhite, 0);
        rhi::setUniformInt(shader, "u_albedoMap", 0);
        rhi::setUniformInt(shader, "u_hasAlbedoMap", 0);
    }

    if (rhi::isValid(mat.normalMap)) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, mat.normalMap.id);
        rhi::setUniformInt(shader, "u_normalMap", 2);
        rhi::setUniformInt(shader, "u_hasNormalMap", 1);
    } else {
        rhi::setUniformInt(shader, "u_hasNormalMap", 0);
    }

    if (rhi::isValid(mat.metallicRoughnessMap)) {
        glActiveTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, mat.metallicRoughnessMap.id);
        rhi::setUniformInt(shader, "u_metallicRoughnessMap", 3);
        rhi::setUniformInt(shader, "u_hasMetallicRoughnessMap", 1);
    } else {
        rhi::setUniformInt(shader, "u_hasMetallicRoughnessMap", 0);
    }

    if (rhi::isValid(mat.aoMap)) {
        glActiveTexture(GL_TEXTURE0 + 4);
        glBindTexture(GL_TEXTURE_2D, mat.aoMap.id);
        rhi::setUniformInt(shader, "u_aoMap", 4);
        rhi::setUniformInt(shader, "u_hasAoMap", 1);
    } else {
        rhi::setUniformInt(shader, "u_hasAoMap", 0);
    }

    if (rhi::isValid(mat.emissiveMap)) {
        glActiveTexture(GL_TEXTURE0 + 5);
        glBindTexture(GL_TEXTURE_2D, mat.emissiveMap.id);
        rhi::setUniformInt(shader, "u_emissiveMap", 5);
        rhi::setUniformInt(shader, "u_hasEmissiveMap", 1);
    } else {
        rhi::setUniformInt(shader, "u_hasEmissiveMap", 0);
    }
}

static void uploadBlinnPhongMaterial(const rhi::ShaderHandle shader,
                                     const Material3D* mat,
                                     const rhi::TextureHandle defaultWhite) {
    glm::vec4 diffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
    rhi::TextureHandle diffuseTex = defaultWhite;
    glm::vec3 specularColor{1.0f, 1.0f, 1.0f};
    f32 shininess = 32.0f;
    rhi::TextureHandle normalMapTex{};
    rhi::TextureHandle specularMapTex{};

    if (mat != nullptr) {
        diffuseColor = mat->diffuseColor;
        if (rhi::isValid(mat->diffuseTexture)) {
            diffuseTex = mat->diffuseTexture;
        }
        specularColor = mat->specularColor;
        shininess     = mat->shininess;
        normalMapTex  = mat->normalMapTexture;
        specularMapTex = mat->specularMapTexture;
    }

    rhi::setUniformVec4(shader, "u_diffuseColor", diffuseColor);
    rhi::bindTexture(diffuseTex, 0);
    rhi::setUniformInt(shader, "u_diffuseTexture", 0);
    rhi::setUniformVec3(shader, "u_specularColor", specularColor);
    rhi::setUniformFloat(shader, "u_shininess", shininess);

    if (rhi::isValid(normalMapTex)) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, normalMapTex.id);
        rhi::setUniformInt(shader, "u_normalMap", 2);
        rhi::setUniformInt(shader, "u_hasNormalMap", 1);
    } else {
        rhi::setUniformInt(shader, "u_hasNormalMap", 0);
    }

    if (rhi::isValid(specularMapTex)) {
        glActiveTexture(GL_TEXTURE0 + 3);
        glBindTexture(GL_TEXTURE_2D, specularMapTex.id);
        rhi::setUniformInt(shader, "u_specularMap", 3);
        rhi::setUniformInt(shader, "u_hasSpecularMap", 1);
    } else {
        rhi::setUniformInt(shader, "u_hasSpecularMap", 0);
    }
}

// Helper: build model matrix from Transform3D.
static glm::mat4 buildModelMatrix(const Transform3D& t) {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

void meshRenderSystem(World& world, const Camera& camera3d,
                      const ShadowConfig& shadowCfg, const ShadowMap& shadowMap,
                      const FogParams& fog) {
    // --- Retrieve shader library from ECS context ---
    const auto* shaderLib = world.registry().ctx().find<renderer::ShaderLibrary>();
    if (shaderLib == nullptr) {
        FFE_LOG_WARN("MeshRenderer", "meshRenderSystem: no ShaderLibrary in ECS context");
        return;
    }

    const rhi::ShaderHandle meshShader = getShader(*shaderLib, BuiltinShader::MESH_BLINN_PHONG);
    if (!rhi::isValid(meshShader)) {
        FFE_LOG_WARN("MeshRenderer", "meshRenderSystem: MESH_BLINN_PHONG shader not loaded");
        return;
    }

    // --- Retrieve scene lighting from ECS context ---
    const SceneLighting3D* lighting = world.registry().ctx().find<SceneLighting3D>();
    SceneLighting3D defaultLighting;
    if (lighting == nullptr) {
        lighting = &defaultLighting;
    }

    // --- Retrieve default white texture from ECS context ---
    rhi::TextureHandle defaultWhite{};
    {
        const rhi::TextureHandle* texPtr = world.registry().ctx().find<rhi::TextureHandle>();
        if (texPtr != nullptr) {
            defaultWhite = *texPtr;
        }
    }

    // --- Enumerate 3D entities (those with Transform3D + Mesh) ---
    // Early-out: if no such entities, skip pipeline state changes entirely.
    auto view = world.registry().view<const Transform3D, const Mesh>();
    if (view.begin() == view.end()) {
        return;
    }

    // --- Compute light-space matrix once (reused by shadow pre-pass and main pass) ---
    glm::mat4 lightSpaceMat{1.0f};
    if (shadowCfg.enabled) {
        lightSpaceMat = computeLightSpaceMatrix(lighting->lightDir, shadowCfg);
    }

    // --- Build sorted entity list for instancing ---
    // Collect all valid entities with their mesh handle ID for sorting.
    // Fixed-size arrays — no heap allocation. MAX_MESH_ASSETS * MAX_INSTANCES_PER_BATCH
    // is the theoretical max, but we cap at a reasonable fixed limit.
    // 4096 entities is plenty for LEGACY tier.
    static constexpr u32 MAX_RENDER_ENTITIES = 4096;
    struct RenderEntity {
        entt::entity entity;
        u32          meshId;     // MeshHandle::id for sorting
        bool         skinned;    // true if entity has Skeleton (excluded from instancing)
    };
    RenderEntity renderEntities[MAX_RENDER_ENTITIES];
    u32 renderEntityCount = 0;

    for (const auto [entity, transform3d, meshComp] : view.each()) {
        if (renderEntityCount >= MAX_RENDER_ENTITIES) {
            break;
        }
        const MeshHandle handle = meshComp.meshHandle;
        if (!isValid(handle)) {
            continue;
        }
        const MeshGpuRecord* rec = getMeshGpuRecord(handle);
        if (rec == nullptr) {
            continue;
        }
        const bool hasSkel = rec->hasSkeleton &&
            world.registry().all_of<ffe::Skeleton>(entity);
        renderEntities[renderEntityCount++] = {entity, handle.id, hasSkel};
    }

    if (renderEntityCount == 0) {
        return;
    }

    // Sort by meshId so identical meshes are adjacent (enables instanced batching).
    // Skinned entities sort to their own meshId groups but won't be instanced.
    std::sort(renderEntities, renderEntities + renderEntityCount,
              [](const RenderEntity& a, const RenderEntity& b) {
                  if (a.meshId != b.meshId) return a.meshId < b.meshId;
                  // Group skinned entities separately within same meshId
                  return static_cast<int>(a.skinned) < static_cast<int>(b.skinned);
              });

    // --- Shadow depth pre-pass ---
    if (shadowCfg.enabled && shadowMap.fbo != 0) {
        const rhi::ShaderHandle shadowShader = getShader(*shaderLib, BuiltinShader::SHADOW_DEPTH);
        const rhi::ShaderHandle shadowSkinnedShader = getShader(*shaderLib, BuiltinShader::SHADOW_DEPTH_SKINNED);
        const rhi::ShaderHandle shadowInstancedShader = getShader(*shaderLib, BuiltinShader::SHADOW_DEPTH_INSTANCED);
        if (rhi::isValid(shadowShader)) {
            const i32 savedVpW = rhi::getViewportWidth();
            const i32 savedVpH = rhi::getViewportHeight();
            GLint savedFbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);

            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
            glViewport(0, 0,
                       static_cast<GLsizei>(shadowMap.resolution),
                       static_cast<GLsizei>(shadowMap.resolution));
            glClear(GL_DEPTH_BUFFER_BIT);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            // Process shadow pass with instancing support
            u32 idx = 0;
            while (idx < renderEntityCount) {
                const u32 currentMeshId = renderEntities[idx].meshId;
                const MeshGpuRecord* rec = getMeshGpuRecord(MeshHandle{currentMeshId});
                if (rec == nullptr) {
                    ++idx;
                    continue;
                }

                // Collect non-skinned entities for this mesh (instancing candidates)
                u32 batchStart = idx;
                u32 nonSkinnedCount = 0;
                while (idx < renderEntityCount && renderEntities[idx].meshId == currentMeshId
                       && !renderEntities[idx].skinned) {
                    ++nonSkinnedCount;
                    ++idx;
                }

                // Instanced shadow draw for non-skinned entities
                if (nonSkinnedCount >= INSTANCING_THRESHOLD && s_instanceVbo != 0
                    && rhi::isValid(shadowInstancedShader)) {
                    rhi::bindShader(shadowInstancedShader);
                    rhi::setUniformMat4(shadowInstancedShader, "u_lightSpaceMatrix", lightSpaceMat);

                    // Fill instance buffer in batches of MAX_INSTANCES_PER_BATCH
                    u32 remaining = nonSkinnedCount;
                    u32 batchIdx = batchStart;
                    static InstanceData s_shadowInstanceScratch[MAX_INSTANCES_PER_BATCH];
                    while (remaining > 0) {
                        const u32 batchSize = (remaining > MAX_INSTANCES_PER_BATCH)
                                              ? MAX_INSTANCES_PER_BATCH : remaining;
                        for (u32 i = 0; i < batchSize; ++i) {
                            const auto ent = renderEntities[batchIdx + i].entity;
                            const auto& t3d = world.registry().get<const Transform3D>(ent);
                            s_shadowInstanceScratch[i].modelMatrix = buildModelMatrix(t3d);
                            s_shadowInstanceScratch[i].instanceColor = glm::vec4(1.0f); // shadows don't use color
                        }
                        // Upload instance data
                        glBindBuffer(GL_ARRAY_BUFFER, s_instanceVbo);
                        glBufferSubData(GL_ARRAY_BUFFER, 0,
                                        static_cast<GLsizeiptr>(batchSize * sizeof(InstanceData)),
                                        s_shadowInstanceScratch);

                        glBindVertexArray(rec->vaoId);
                        setupInstanceAttribs();
                        glDrawElementsInstanced(GL_TRIANGLES,
                                                static_cast<GLsizei>(rec->indexCount),
                                                GL_UNSIGNED_INT,
                                                nullptr,
                                                static_cast<GLsizei>(batchSize));
                        teardownInstanceAttribs();

                        batchIdx += batchSize;
                        remaining -= batchSize;
                    }
                } else if (nonSkinnedCount > 0) {
                    // Single-draw fallback for non-skinned (1 entity or no instance VBO)
                    // Bind shader and set light-space matrix once before the loop.
                    rhi::bindShader(shadowShader);
                    rhi::setUniformMat4(shadowShader, "u_lightSpaceMatrix", lightSpaceMat);
                    glBindVertexArray(rec->vaoId);
                    for (u32 i = batchStart; i < batchStart + nonSkinnedCount; ++i) {
                        const auto ent = renderEntities[i].entity;
                        const auto& t3d = world.registry().get<const Transform3D>(ent);
                        rhi::setUniformMat4(shadowShader, "u_model", buildModelMatrix(t3d));
                        glDrawElements(GL_TRIANGLES,
                                       static_cast<GLsizei>(rec->indexCount),
                                       GL_UNSIGNED_INT, nullptr);
                    }
                }

                // Skinned entities: always single-draw (need bone matrices)
                while (idx < renderEntityCount && renderEntities[idx].meshId == currentMeshId
                       && renderEntities[idx].skinned) {
                    const auto ent = renderEntities[idx].entity;
                    const auto& t3d = world.registry().get<const Transform3D>(ent);
                    const rhi::ShaderHandle activeShader =
                        rhi::isValid(shadowSkinnedShader) ? shadowSkinnedShader : shadowShader;
                    rhi::bindShader(activeShader);
                    rhi::setUniformMat4(activeShader, "u_lightSpaceMatrix", lightSpaceMat);
                    rhi::setUniformMat4(activeShader, "u_model", buildModelMatrix(t3d));
                    const auto& skeleton = world.registry().get<ffe::Skeleton>(ent);
                    rhi::setUniformMat4Array(activeShader, "u_boneMatrices[0]",
                                             skeleton.boneMatrices,
                                             skeleton.boneCount > 0 ? skeleton.boneCount : 1);
                    glBindVertexArray(rec->vaoId);
                    glDrawElements(GL_TRIANGLES,
                                   static_cast<GLsizei>(rec->indexCount),
                                   GL_UNSIGNED_INT, nullptr);
                    ++idx;
                }
            }
            glBindVertexArray(0);

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(savedFbo));
            glViewport(0, 0, static_cast<GLsizei>(savedVpW), static_cast<GLsizei>(savedVpH));
            glCullFace(GL_BACK);
        }
    }

    // --- Set 3D pipeline state: depth test LESS, cull BACK, no blend ---
    rhi::PipelineState ps3d;
    ps3d.blend      = rhi::BlendMode::NONE;
    ps3d.depth      = rhi::DepthFunc::LESS;
    ps3d.cull       = rhi::CullMode::BACK;
    ps3d.depthWrite = true;
    ps3d.wireframe  = false;
    rhi::applyPipelineState(ps3d);

    // --- Upload VP matrix for the 3D perspective camera ---
    const glm::mat4 vpMatrix = computeViewProjectionMatrix(camera3d);
    rhi::setViewProjection(vpMatrix);

    // --- Get all shaders ---
    const rhi::ShaderHandle skinnedShader = getShader(*shaderLib, BuiltinShader::MESH_SKINNED);
    const rhi::ShaderHandle pbrShader = getShader(*shaderLib, BuiltinShader::MESH_PBR);
    const rhi::ShaderHandle pbrSkinnedShader = getShader(*shaderLib, BuiltinShader::MESH_PBR_SKINNED);
    const rhi::ShaderHandle blinnPhongInstancedShader = getShader(*shaderLib, BuiltinShader::MESH_BLINN_PHONG_INSTANCED);
    const rhi::ShaderHandle pbrInstancedShader = getShader(*shaderLib, BuiltinShader::MESH_PBR_INSTANCED);

    // --- Get skybox config for IBL (PBR ambient specular from cubemap) ---
    const auto* const* skyboxCfgPtr = world.registry().ctx().find<renderer::SkyboxConfig*>();
    const SkyboxConfig* skyboxCfg = (skyboxCfgPtr != nullptr) ? *skyboxCfgPtr : nullptr;

    // --- Upload scene-global uniforms to ALL mesh shaders ---
    // Collect all valid shaders into a fixed-size array for batch uniform upload.
    rhi::ShaderHandle allShaders[8];
    u32 allShaderCount = 0;
    allShaders[allShaderCount++] = meshShader;
    if (rhi::isValid(skinnedShader))               allShaders[allShaderCount++] = skinnedShader;
    if (rhi::isValid(pbrShader))                   allShaders[allShaderCount++] = pbrShader;
    if (rhi::isValid(pbrSkinnedShader))            allShaders[allShaderCount++] = pbrSkinnedShader;
    if (rhi::isValid(blinnPhongInstancedShader))   allShaders[allShaderCount++] = blinnPhongInstancedShader;
    if (rhi::isValid(pbrInstancedShader))          allShaders[allShaderCount++] = pbrInstancedShader;

    for (u32 si = 0; si < allShaderCount; ++si) {
        const rhi::ShaderHandle sh = allShaders[si];
        rhi::bindShader(sh);
        rhi::setUniformVec3(sh, "u_lightDir",     lighting->lightDir);
        rhi::setUniformVec3(sh, "u_lightColor",   lighting->lightColor);
        rhi::setUniformVec3(sh, "u_ambientColor", lighting->ambientColor);
        rhi::setUniformVec3(sh, "u_viewPos",      camera3d.position);
        rhi::setUniformMat4(sh, "u_viewProjection", vpMatrix);
    }

    // --- Upload point light uniforms ---
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

        // Pre-computed uniform name tables — avoids per-frame snprintf.
        static const char* const s_pointLightPosNames[MAX_POINT_LIGHTS] = {
            "u_pointLightPos[0]", "u_pointLightPos[1]",
            "u_pointLightPos[2]", "u_pointLightPos[3]",
            "u_pointLightPos[4]", "u_pointLightPos[5]",
            "u_pointLightPos[6]", "u_pointLightPos[7]"
        };
        static const char* const s_pointLightColorNames[MAX_POINT_LIGHTS] = {
            "u_pointLightColor[0]", "u_pointLightColor[1]",
            "u_pointLightColor[2]", "u_pointLightColor[3]",
            "u_pointLightColor[4]", "u_pointLightColor[5]",
            "u_pointLightColor[6]", "u_pointLightColor[7]"
        };
        static const char* const s_pointLightRadiusNames[MAX_POINT_LIGHTS] = {
            "u_pointLightRadius[0]", "u_pointLightRadius[1]",
            "u_pointLightRadius[2]", "u_pointLightRadius[3]",
            "u_pointLightRadius[4]", "u_pointLightRadius[5]",
            "u_pointLightRadius[6]", "u_pointLightRadius[7]"
        };

        for (u32 si = 0; si < allShaderCount; ++si) {
            const rhi::ShaderHandle sh = allShaders[si];
            rhi::bindShader(sh);
            rhi::setUniformInt(sh, "u_pointLightCount", static_cast<i32>(activeCount));

            for (u32 i = 0; i < activeCount; ++i) {
                rhi::setUniformVec3(sh, s_pointLightPosNames[i], positions[i]);
                rhi::setUniformVec3(sh, s_pointLightColorNames[i], colors[i]);
                rhi::setUniformFloat(sh, s_pointLightRadiusNames[i], radii[i]);
            }
        }
    }

    // --- Shadow map uniforms for all mesh shaders ---
    {
        if (shadowCfg.enabled && shadowMap.depthTexture != 0) {
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);

            for (u32 si = 0; si < allShaderCount; ++si) {
                const rhi::ShaderHandle sh = allShaders[si];
                rhi::bindShader(sh);
                rhi::setUniformInt(sh, "u_shadowMap", 1);
                rhi::setUniformInt(sh, "u_shadowsEnabled", 1);
                rhi::setUniformFloat(sh, "u_shadowBias", shadowCfg.bias);
                rhi::setUniformMat4(sh, "u_lightSpaceMatrix", lightSpaceMat);
            }
        } else {
            for (u32 si = 0; si < allShaderCount; ++si) {
                const rhi::ShaderHandle sh = allShaders[si];
                rhi::bindShader(sh);
                rhi::setUniformInt(sh, "u_shadowsEnabled", 0);
            }
        }
    }

    // --- Fog uniforms for all mesh shaders ---
    {
        for (u32 si = 0; si < allShaderCount; ++si) {
            const rhi::ShaderHandle sh = allShaders[si];
            rhi::bindShader(sh);
            if (fog.enabled) {
                rhi::setUniformInt(sh, "u_fogEnabled", 1);
                rhi::setUniformVec3(sh, "u_fogColor", glm::vec3(fog.r, fog.g, fog.b));
                rhi::setUniformFloat(sh, "u_fogNear", fog.nearDist);
                rhi::setUniformFloat(sh, "u_fogFar", fog.farDist);
            } else {
                rhi::setUniformInt(sh, "u_fogEnabled", 0);
            }
        }
    }

    // --- Upload skybox/IBL uniforms to PBR shaders ---
    {
        const bool hasSkybox = (skyboxCfg != nullptr) && skyboxCfg->enabled
                               && (skyboxCfg->cubemapTexture != 0);

        rhi::ShaderHandle iblShaders[4];
        u32 iblShaderCount = 0;
        if (rhi::isValid(pbrShader))           iblShaders[iblShaderCount++] = pbrShader;
        if (rhi::isValid(pbrSkinnedShader))    iblShaders[iblShaderCount++] = pbrSkinnedShader;
        if (rhi::isValid(pbrInstancedShader))  iblShaders[iblShaderCount++] = pbrInstancedShader;

        if (hasSkybox) {
            glActiveTexture(GL_TEXTURE0 + 6);
            glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxCfg->cubemapTexture);
        }

        for (u32 si = 0; si < iblShaderCount; ++si) {
            const rhi::ShaderHandle sh = iblShaders[si];
            rhi::bindShader(sh);
            rhi::setUniformInt(sh, "u_hasSkybox", hasSkybox ? 1 : 0);
            if (hasSkybox) {
                rhi::setUniformInt(sh, "u_skybox", 6);
            }
        }
    }

    // --- Main draw loop (with instancing) ---
    rhi::bindShader(meshShader);
    rhi::ShaderHandle currentShader = meshShader;

    u32 idx = 0;
    while (idx < renderEntityCount) {
        const u32 currentMeshId = renderEntities[idx].meshId;
        const MeshGpuRecord* rec = getMeshGpuRecord(MeshHandle{currentMeshId});
        if (rec == nullptr) {
            ++idx;
            continue;
        }

        // --- Collect non-skinned entities for this mesh ---
        const u32 batchStart = idx;
        u32 nonSkinnedCount = 0;
        while (idx < renderEntityCount && renderEntities[idx].meshId == currentMeshId
               && !renderEntities[idx].skinned) {
            ++nonSkinnedCount;
            ++idx;
        }

        // --- Instanced draw path for non-skinned entities with 2+ instances ---
        bool didInstance = false;
        if (nonSkinnedCount >= INSTANCING_THRESHOLD && s_instanceVbo != 0) {
            // Determine shader: PBR or Blinn-Phong based on first entity's material
            const auto firstEnt = renderEntities[batchStart].entity;
            const auto* pbrMat = world.registry().try_get<renderer::PBRMaterial>(firstEnt);
            const bool usePbr = (pbrMat != nullptr) && rhi::isValid(pbrInstancedShader);

            const rhi::ShaderHandle batchShader = usePbr
                ? pbrInstancedShader : blinnPhongInstancedShader;

            if (rhi::isValid(batchShader)) {
                didInstance = true;
                if (batchShader.id != currentShader.id) {
                    rhi::bindShader(batchShader);
                    currentShader = batchShader;
                }

                // Upload shared material properties (textures, specular, etc.)
                // from the first entity. Per-instance diffuse color is in the
                // instance buffer — the shader reads it from vertex attribs.
                if (usePbr) {
                    uploadPbrMaterial(batchShader, *pbrMat, defaultWhite);
                } else {
                    const Material3D* mat = world.registry().try_get<Material3D>(firstEnt);
                    uploadBlinnPhongMaterial(batchShader, mat, defaultWhite);
                }

                // Fill instance buffer in batches of MAX_INSTANCES_PER_BATCH
                u32 remaining = nonSkinnedCount;
                u32 batchIdx = batchStart;
                static InstanceData s_mainInstanceScratch[MAX_INSTANCES_PER_BATCH];
                while (remaining > 0) {
                    const u32 batchSize = (remaining > MAX_INSTANCES_PER_BATCH)
                                          ? MAX_INSTANCES_PER_BATCH : remaining;
                    for (u32 i = 0; i < batchSize; ++i) {
                        const auto ent = renderEntities[batchIdx + i].entity;
                        const auto& t3d = world.registry().get<const Transform3D>(ent);
                        s_mainInstanceScratch[i].modelMatrix = buildModelMatrix(t3d);

                        // Per-instance color from Material3D (default white if absent)
                        const Material3D* entMat = world.registry().try_get<Material3D>(ent);
                        s_mainInstanceScratch[i].instanceColor = (entMat != nullptr)
                            ? entMat->diffuseColor
                            : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    // Upload instance data
                    glBindBuffer(GL_ARRAY_BUFFER, s_instanceVbo);
                    glBufferSubData(GL_ARRAY_BUFFER, 0,
                                    static_cast<GLsizeiptr>(batchSize * sizeof(InstanceData)),
                                    s_mainInstanceScratch);

                    glBindVertexArray(rec->vaoId);
                    setupInstanceAttribs();
                    glDrawElementsInstanced(GL_TRIANGLES,
                                            static_cast<GLsizei>(rec->indexCount),
                                            GL_UNSIGNED_INT,
                                            nullptr,
                                            static_cast<GLsizei>(batchSize));
                    teardownInstanceAttribs();

                    batchIdx += batchSize;
                    remaining -= batchSize;
                }
            }
        }

        if (!didInstance && nonSkinnedCount > 0) {
            // --- Single-draw path for non-skinned entities (1 entity or no instanced shader) ---
            for (u32 i = batchStart; i < batchStart + nonSkinnedCount; ++i) {
                const auto ent = renderEntities[i].entity;
                const auto& t3d = world.registry().get<const Transform3D>(ent);

                const auto* pbrMat = world.registry().try_get<renderer::PBRMaterial>(ent);
                const bool usePbr = (pbrMat != nullptr) && rhi::isValid(pbrShader);
                const rhi::ShaderHandle targetShader = usePbr ? pbrShader : meshShader;

                if (targetShader.id != currentShader.id) {
                    rhi::bindShader(targetShader);
                    currentShader = targetShader;
                }

                const glm::mat4 model = buildModelMatrix(t3d);
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
                rhi::setUniformMat4(currentShader, "u_model", model);
                rhi::setUniformMat3(currentShader, "u_normalMatrix", normalMatrix);

                if (usePbr) {
                    uploadPbrMaterial(currentShader, *pbrMat, defaultWhite);
                } else {
                    const Material3D* mat = world.registry().try_get<Material3D>(ent);
                    uploadBlinnPhongMaterial(currentShader, mat, defaultWhite);
                }

                glBindVertexArray(rec->vaoId);
                glDrawElements(GL_TRIANGLES,
                               static_cast<GLsizei>(rec->indexCount),
                               GL_UNSIGNED_INT, nullptr);
            }
        }

        // --- Skinned entities: always single-draw (need per-entity bone matrices) ---
        while (idx < renderEntityCount && renderEntities[idx].meshId == currentMeshId
               && renderEntities[idx].skinned) {
            const auto ent = renderEntities[idx].entity;
            const auto& t3d = world.registry().get<const Transform3D>(ent);

            const auto* pbrMat = world.registry().try_get<renderer::PBRMaterial>(ent);
            const bool usePbr = (pbrMat != nullptr) && rhi::isValid(pbrSkinnedShader);
            const rhi::ShaderHandle targetShader = usePbr
                ? pbrSkinnedShader
                : (rhi::isValid(skinnedShader) ? skinnedShader : meshShader);

            if (targetShader.id != currentShader.id) {
                rhi::bindShader(targetShader);
                currentShader = targetShader;
            }

            const glm::mat4 model = buildModelMatrix(t3d);
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
            rhi::setUniformMat4(currentShader, "u_model", model);
            rhi::setUniformMat3(currentShader, "u_normalMatrix", normalMatrix);

            const auto& skeleton = world.registry().get<ffe::Skeleton>(ent);
            rhi::setUniformMat4Array(currentShader, "u_boneMatrices[0]",
                                     skeleton.boneMatrices,
                                     skeleton.boneCount > 0 ? skeleton.boneCount : 1);

            if (usePbr) {
                uploadPbrMaterial(currentShader, *pbrMat, defaultWhite);
            } else {
                const Material3D* mat = world.registry().try_get<Material3D>(ent);
                uploadBlinnPhongMaterial(currentShader, mat, defaultWhite);
            }

            glBindVertexArray(rec->vaoId);
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(rec->indexCount),
                           GL_UNSIGNED_INT, nullptr);
            ++idx;
        }
    }
    glBindVertexArray(0);

    // --- Unbind textures from units 1-6 after all entities drawn ---
    glActiveTexture(GL_TEXTURE0 + 6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, 0);
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
