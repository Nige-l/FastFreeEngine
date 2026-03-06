// mesh_renderer.cpp — 3D mesh render system implementation for FFE.
//
// Queries ECS for entities with Transform3D + Mesh components.
// Sets up depth test, uploads per-entity uniforms, and issues indexed draw calls.
// After all mesh entities, restores 2D-compatible pipeline state.
//
// Tier: LEGACY (OpenGL 3.3 core). No DSA. No instancing.

#include "renderer/mesh_renderer.h"
#include "renderer/mesh_loader.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/shader_library.h"
#include "renderer/camera.h"
#include "core/logging.h"

#include <glad/glad.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace ffe::renderer {

void meshRenderSystem(World& world, const Camera& camera3d) {
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

    // --- Bind mesh shader ---
    rhi::bindShader(meshShader);

    // --- Upload scene-global uniforms (same for all mesh entities) ---
    rhi::setUniformVec3(meshShader, "u_lightDir",     lighting->lightDir);
    rhi::setUniformVec3(meshShader, "u_lightColor",   lighting->lightColor);
    rhi::setUniformVec3(meshShader, "u_ambientColor", lighting->ambientColor);
    rhi::setUniformVec3(meshShader, "u_viewPos",      camera3d.position);

    // u_viewProjection is uploaded by setViewProjection + bindShader above (cached in RHI).
    // Explicitly upload here too for clarity — the RHI caches it in the shader program.
    rhi::setUniformMat4(meshShader, "u_viewProjection", vpMatrix);

    // --- Per-entity draw loop ---
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
        rhi::setUniformMat4(meshShader, "u_model",        model);
        rhi::setUniformMat3(meshShader, "u_normalMatrix",  normalMatrix);

        // --- Per-entity material (from Material3D if present, else defaults) ---
        glm::vec4 diffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
        rhi::TextureHandle diffuseTex = defaultWhite;

        const Material3D* mat = world.registry().try_get<Material3D>(entity);
        if (mat != nullptr) {
            diffuseColor = mat->diffuseColor;
            if (rhi::isValid(mat->diffuseTexture)) {
                diffuseTex = mat->diffuseTexture;
            }
        }

        rhi::setUniformVec4(meshShader, "u_diffuseColor", diffuseColor);

        // Bind diffuse texture to unit 0
        rhi::bindTexture(diffuseTex, 0);
        rhi::setUniformInt(meshShader, "u_diffuseTexture", 0);

        // --- Issue indexed draw call via the mesh VAO ---
        // The VAO already has VBO attribs and IBO bound.
        glBindVertexArray(rec->vaoId);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(rec->indexCount),
                       GL_UNSIGNED_INT,
                       nullptr);
        glBindVertexArray(0);
    }

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
