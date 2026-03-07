#pragma once

// mesh_renderer.h — 3D mesh rendering system for FFE.
//
// Queries the ECS for entities with Transform3D + Mesh components and draws
// them with the Blinn-Phong shader. Called from Application::render() before
// the 2D sprite batch pass.
//
// Shadow mapping: if a ShadowConfig (enabled=true) and valid ShadowMap are
// stored in the ECS context, the renderer performs a depth-only shadow pass
// before the main Blinn-Phong pass. The shadow depth texture is bound to
// texture unit 1 during the lit pass and sampled with 3x3 PCF.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).
// Depth test is enabled for the 3D pass and restored to disabled afterward.

#include "core/types.h"
#include "core/ecs.h"
#include "renderer/camera.h"
#include "renderer/shadow_map.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

// Scene-global directional lighting parameters.
// Stored in the ECS context (emplaced by Application::startup).
// Settable from Lua via ffe.setLightDirection, ffe.setLightColor, ffe.setAmbientColor.
struct SceneLighting3D {
    glm::vec3 lightDir     = glm::normalize(glm::vec3{0.5f, -1.0f, 0.3f});
    glm::vec3 lightColor   = {1.0f, 1.0f, 1.0f};
    glm::vec3 ambientColor = {0.15f, 0.15f, 0.15f};
};

// Render all entities with Transform3D + Mesh components using the Blinn-Phong shader.
//
// If shadowCfg.enabled is true and shadowMap.fbo != 0, a depth-only shadow pass
// is executed first (to shadowMap.fbo), then the main pass reads the shadow texture.
//
// Sets up depth test (LESS), face culling (BACK), and disables blending.
// After rendering all mesh entities, restores state for the 2D pass:
//   - Depth test disabled
//   - Culling disabled
//   - Blend mode ALPHA re-enabled
//
// Scene lighting is read from SceneLighting3D stored in the ECS context.
// Camera position is read from the Camera struct (for specular highlights).
//
// Not a per-frame allocation path — all data is read from ECS components.
void meshRenderSystem(World& world, const Camera& camera3d,
                      const ShadowConfig& shadowCfg, const ShadowMap& shadowMap);

} // namespace ffe::renderer
