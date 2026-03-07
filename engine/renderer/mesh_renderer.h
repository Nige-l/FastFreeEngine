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

// Maximum point lights supported on LEGACY tier (OpenGL 3.3).
// Fixed-size array — no heap allocation in the render path.
inline constexpr u32 MAX_POINT_LIGHTS = 8;

// Point light data. Position in world space, color, and attenuation radius.
// Attenuation uses 1 / (1 + (d/radius)*2 + (d/radius)^2) where d = distance.
struct PointLight {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 color    = {1.0f, 1.0f, 1.0f};
    f32       radius   = 10.0f;   // Attenuation radius (world units)
    bool      active   = false;   // Whether this light slot is in use
};

// Scene-global directional lighting parameters.
// Stored in the ECS context (emplaced by Application::startup).
// Settable from Lua via ffe.setLightDirection, ffe.setLightColor, ffe.setAmbientColor.
struct SceneLighting3D {
    glm::vec3 lightDir     = glm::normalize(glm::vec3{0.5f, -1.0f, 0.3f});
    glm::vec3 lightColor   = {1.0f, 1.0f, 1.0f};
    glm::vec3 ambientColor = {0.15f, 0.15f, 0.15f};

    // Point lights — fixed-size array, no heap allocation.
    PointLight pointLights[MAX_POINT_LIGHTS] = {};
    u32        activePointLightCount = 0;  // Cached count of active lights for shader upload
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
