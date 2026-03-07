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
// Tier: LEGACY (OpenGL 3.3 core). No DSA. No instancing.

#include "renderer/mesh_renderer.h"
#include "renderer/mesh_loader.h"
#include "renderer/render_system.h"
#include "renderer/skeleton.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/shader_library.h"
#include "renderer/shadow_map.h"
#include "renderer/camera.h"
#include "core/logging.h"

#include <glad/glad.h>

// GL_TEXTURE_CUBE_MAP is core in OpenGL 3.3 but not included in our minimal GLAD header.
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif

#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ffe::renderer {

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

    // --- Shadow depth pre-pass ---
    // Render all mesh entities from the light's perspective into the shadow FBO.
    // Only executed when shadows are enabled and a valid shadow FBO exists.
    if (shadowCfg.enabled && shadowMap.fbo != 0) {
        const rhi::ShaderHandle shadowShader = getShader(*shaderLib, BuiltinShader::SHADOW_DEPTH);
        const rhi::ShaderHandle shadowSkinnedShader = getShader(*shaderLib, BuiltinShader::SHADOW_DEPTH_SKINNED);
        if (rhi::isValid(shadowShader)) {
            // Save the current viewport dimensions to restore after the shadow pass.
            const i32 savedVpW = rhi::getViewportWidth();
            const i32 savedVpH = rhi::getViewportHeight();

            // Bind shadow FBO and set viewport to shadow map resolution.
            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
            glViewport(0, 0,
                       static_cast<GLsizei>(shadowMap.resolution),
                       static_cast<GLsizei>(shadowMap.resolution));
            glClear(GL_DEPTH_BUFFER_BIT);

            // Enable depth test + write for the shadow pass.
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);

            // Use front-face culling during shadow pass to reduce shadow acne
            // (Peter Pan artifacts). Back faces write depth instead of front faces.
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);

            // Per-entity: build model matrix and draw.
            // Use skinned shadow shader for entities with Skeleton component.
            for (const auto [entity, transform3d, meshComp] : view.each()) {
                const MeshHandle handle = meshComp.meshHandle;
                if (!isValid(handle)) {
                    continue;
                }

                const MeshGpuRecord* rec = getMeshGpuRecord(handle);
                if (rec == nullptr) {
                    continue;
                }

                // Check for skeleton component to select the right shadow shader
                const bool hasSkeleton = rec->hasSkeleton &&
                    world.registry().all_of<ffe::Skeleton>(entity);
                const rhi::ShaderHandle activeShader = (hasSkeleton && rhi::isValid(shadowSkinnedShader))
                    ? shadowSkinnedShader : shadowShader;

                rhi::bindShader(activeShader);
                rhi::setUniformMat4(activeShader, "u_lightSpaceMatrix", lightSpaceMat);

                // Build model matrix: translate * rotate * scale
                glm::mat4 model = glm::mat4(1.0f);
                model = glm::translate(model, transform3d.position);
                model = model * glm::mat4_cast(transform3d.rotation);
                model = glm::scale(model, transform3d.scale);

                rhi::setUniformMat4(activeShader, "u_model", model);

                // Upload bone matrices for skinned entities
                if (hasSkeleton) {
                    const auto& skeleton = world.registry().get<ffe::Skeleton>(entity);
                    rhi::setUniformMat4Array(activeShader, "u_boneMatrices[0]",
                                             skeleton.boneMatrices,
                                             skeleton.boneCount > 0 ? skeleton.boneCount : 1);
                }

                glBindVertexArray(rec->vaoId);
                glDrawElements(GL_TRIANGLES,
                               static_cast<GLsizei>(rec->indexCount),
                               GL_UNSIGNED_INT,
                               nullptr);
            }
            glBindVertexArray(0);

            // Restore: unbind shadow FBO, restore viewport, reset cull face.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    // --- Get skinned mesh shader ---
    const rhi::ShaderHandle skinnedShader = getShader(*shaderLib, BuiltinShader::MESH_SKINNED);

    // --- Bind mesh shader (static — will switch per-entity if skinned) ---
    rhi::bindShader(meshShader);

    // --- Upload scene-global uniforms (same for all mesh entities) ---
    rhi::setUniformVec3(meshShader, "u_lightDir",     lighting->lightDir);
    rhi::setUniformVec3(meshShader, "u_lightColor",   lighting->lightColor);
    rhi::setUniformVec3(meshShader, "u_ambientColor", lighting->ambientColor);
    rhi::setUniformVec3(meshShader, "u_viewPos",      camera3d.position);

    // u_viewProjection is uploaded by setViewProjection + bindShader above (cached in RHI).
    // Explicitly upload here too for clarity — the RHI caches it in the shader program.
    rhi::setUniformMat4(meshShader, "u_viewProjection", vpMatrix);

    // Upload scene-global uniforms to skinned shader too (if available).
    if (rhi::isValid(skinnedShader)) {
        rhi::bindShader(skinnedShader);
        rhi::setUniformVec3(skinnedShader, "u_lightDir",     lighting->lightDir);
        rhi::setUniformVec3(skinnedShader, "u_lightColor",   lighting->lightColor);
        rhi::setUniformVec3(skinnedShader, "u_ambientColor", lighting->ambientColor);
        rhi::setUniformVec3(skinnedShader, "u_viewPos",      camera3d.position);
        rhi::setUniformMat4(skinnedShader, "u_viewProjection", vpMatrix);
        // Re-bind static shader as default — will switch per-entity as needed
        rhi::bindShader(meshShader);
    }

    // --- Upload point light uniforms ---
    // Count active point lights and upload their data to the shader.
    // Fixed-size arrays — no heap allocation.
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

        // Upload point light count and arrays to both static and skinned shaders.
        // Both use the same fragment shader (MESH_BLINN_PHONG_FRAG_SOURCE) which
        // references these uniforms.
        const rhi::ShaderHandle pointLightShaders[] = { meshShader, skinnedShader };
        const u32 plShaderCount = rhi::isValid(skinnedShader) ? 2u : 1u;

        for (u32 si = 0; si < plShaderCount; ++si) {
            const rhi::ShaderHandle sh = pointLightShaders[si];
            rhi::bindShader(sh);
            rhi::setUniformInt(sh, "u_pointLightCount", static_cast<i32>(activeCount));

            // Upload arrays via individual indexed uniforms (GL 3.3 compatible).
            // Only upload active lights to minimise uniform calls.
            char uniformName[64];
            for (u32 i = 0; i < activeCount; ++i) {
                const auto idx = static_cast<unsigned int>(i);
                std::snprintf(uniformName, sizeof(uniformName), "u_pointLightPos[%u]", idx);
                rhi::setUniformVec3(sh, uniformName, positions[i]);

                std::snprintf(uniformName, sizeof(uniformName), "u_pointLightColor[%u]", idx);
                rhi::setUniformVec3(sh, uniformName, colors[i]);

                std::snprintf(uniformName, sizeof(uniformName), "u_pointLightRadius[%u]", idx);
                rhi::setUniformFloat(sh, uniformName, radii[i]);
            }
        }
        // Restore static shader as current
        rhi::bindShader(meshShader);
    }

    // --- Shadow map uniforms for both static and skinned Blinn-Phong passes ---
    if (shadowCfg.enabled && shadowMap.depthTexture != 0) {
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);

        rhi::bindShader(meshShader);
        rhi::setUniformInt(meshShader, "u_shadowMap", 1);
        rhi::setUniformInt(meshShader, "u_shadowsEnabled", 1);
        rhi::setUniformFloat(meshShader, "u_shadowBias", shadowCfg.bias);
        rhi::setUniformMat4(meshShader, "u_lightSpaceMatrix", lightSpaceMat);

        if (rhi::isValid(skinnedShader)) {
            rhi::bindShader(skinnedShader);
            rhi::setUniformInt(skinnedShader, "u_shadowMap", 1);
            rhi::setUniformInt(skinnedShader, "u_shadowsEnabled", 1);
            rhi::setUniformFloat(skinnedShader, "u_shadowBias", shadowCfg.bias);
            rhi::setUniformMat4(skinnedShader, "u_lightSpaceMatrix", lightSpaceMat);
        }
        rhi::bindShader(meshShader);
    } else {
        rhi::setUniformInt(meshShader, "u_shadowsEnabled", 0);
        if (rhi::isValid(skinnedShader)) {
            rhi::bindShader(skinnedShader);
            rhi::setUniformInt(skinnedShader, "u_shadowsEnabled", 0);
            rhi::bindShader(meshShader);
        }
    }

    // --- Fog uniforms for both static and skinned Blinn-Phong passes ---
    {
        const rhi::ShaderHandle fogShaders[] = { meshShader, skinnedShader };
        const u32 fogShaderCount = rhi::isValid(skinnedShader) ? 2u : 1u;

        for (u32 si = 0; si < fogShaderCount; ++si) {
            const rhi::ShaderHandle sh = fogShaders[si];
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
        rhi::bindShader(meshShader);
    }

    // --- Per-entity draw loop ---
    rhi::ShaderHandle currentShader = meshShader;
    for (const auto [entity, transform3d, meshComp] : view.each()) {
        // Validate mesh handle
        const MeshHandle handle = meshComp.meshHandle;
        if (!isValid(handle)) {
            continue;
        }

        const MeshGpuRecord* rec = getMeshGpuRecord(handle);
        if (rec == nullptr) {
            continue;
        }

        // --- Detect skinned entity and select appropriate shader ---
        const bool hasSkeleton = rec->hasSkeleton &&
            world.registry().all_of<ffe::Skeleton>(entity);
        const rhi::ShaderHandle targetShader = (hasSkeleton && rhi::isValid(skinnedShader))
            ? skinnedShader : meshShader;

        if (targetShader.id != currentShader.id) {
            rhi::bindShader(targetShader);
            currentShader = targetShader;

            // Re-upload shadow uniforms after shader switch
            if (shadowCfg.enabled && shadowMap.depthTexture != 0) {
                glActiveTexture(GL_TEXTURE0 + 1);
                glBindTexture(GL_TEXTURE_2D, shadowMap.depthTexture);
                rhi::setUniformInt(currentShader, "u_shadowMap", 1);
                rhi::setUniformInt(currentShader, "u_shadowsEnabled", 1);
                rhi::setUniformFloat(currentShader, "u_shadowBias", shadowCfg.bias);
                rhi::setUniformMat4(currentShader, "u_lightSpaceMatrix", lightSpaceMat);
            } else {
                rhi::setUniformInt(currentShader, "u_shadowsEnabled", 0);
            }
        }

        // --- Build model matrix from Transform3D ---
        // translate * rotate * scale
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform3d.position);
        model = model * glm::mat4_cast(transform3d.rotation);
        model = glm::scale(model, transform3d.scale);

        // --- Normal matrix: transpose(inverse(mat3(model))) ---
        // Required for non-uniform scale to keep normals correct.
        // Per-entity cold-path compute — acceptable at entity count <= MAX_MESH_ASSETS.
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

        // --- Upload per-entity uniforms ---
        rhi::setUniformMat4(currentShader, "u_model",        model);
        rhi::setUniformMat3(currentShader, "u_normalMatrix",  normalMatrix);

        // --- Upload bone matrices for skinned entities ---
        if (hasSkeleton) {
            const auto& skeleton = world.registry().get<ffe::Skeleton>(entity);
            rhi::setUniformMat4Array(currentShader, "u_boneMatrices[0]",
                                     skeleton.boneMatrices,
                                     skeleton.boneCount > 0 ? skeleton.boneCount : 1);
        }

        // --- Per-entity material (from Material3D if present, else defaults) ---
        glm::vec4 diffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
        rhi::TextureHandle diffuseTex = defaultWhite;
        glm::vec3 specularColor{1.0f, 1.0f, 1.0f};
        f32 shininess = 32.0f;
        rhi::TextureHandle normalMapTex{};
        rhi::TextureHandle specularMapTex{};

        const Material3D* mat = world.registry().try_get<Material3D>(entity);
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

        rhi::setUniformVec4(currentShader, "u_diffuseColor", diffuseColor);

        // Bind diffuse texture to unit 0
        rhi::bindTexture(diffuseTex, 0);
        rhi::setUniformInt(currentShader, "u_diffuseTexture", 0);

        // Upload specular material properties
        rhi::setUniformVec3(currentShader, "u_specularColor", specularColor);
        rhi::setUniformFloat(currentShader, "u_shininess", shininess);

        // Bind normal map to unit 2 (unit 1 is shadow map)
        if (rhi::isValid(normalMapTex)) {
            glActiveTexture(GL_TEXTURE0 + 2);
            glBindTexture(GL_TEXTURE_2D, normalMapTex.id);
            rhi::setUniformInt(currentShader, "u_normalMap", 2);
            rhi::setUniformInt(currentShader, "u_hasNormalMap", 1);
        } else {
            rhi::setUniformInt(currentShader, "u_hasNormalMap", 0);
        }

        // Bind specular map to unit 3
        if (rhi::isValid(specularMapTex)) {
            glActiveTexture(GL_TEXTURE0 + 3);
            glBindTexture(GL_TEXTURE_2D, specularMapTex.id);
            rhi::setUniformInt(currentShader, "u_specularMap", 3);
            rhi::setUniformInt(currentShader, "u_hasSpecularMap", 1);
        } else {
            rhi::setUniformInt(currentShader, "u_hasSpecularMap", 0);
        }

        // --- Issue indexed draw call via the mesh VAO ---
        // The VAO already has VBO attribs and IBO bound.
        glBindVertexArray(rec->vaoId);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(rec->indexCount),
                       GL_UNSIGNED_INT,
                       nullptr);
    }
    glBindVertexArray(0);

    // --- Unbind textures from units 1-3 after all entities drawn ---
    // Unit 1: shadow map, Unit 2: normal map, Unit 3: specular map
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
    // Depth test disabled, culling disabled, alpha blending re-enabled.
    rhi::PipelineState ps2d;
    ps2d.blend      = rhi::BlendMode::ALPHA;
    ps2d.depth      = rhi::DepthFunc::NONE;
    ps2d.cull       = rhi::CullMode::NONE;
    ps2d.depthWrite = false;
    ps2d.wireframe  = false;
    rhi::applyPipelineState(ps2d);
}

} // namespace ffe::renderer
