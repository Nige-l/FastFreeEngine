#pragma once

// water.h -- Planar water rendering system for FFE.
//
// Provides a single reflective, animated water surface per scene.
// The water plane is axis-aligned at a configurable Y height,
// extending to the edges of the visible world.
//
// The reflection pass uses the standard camera-flip technique with a
// half-resolution FBO. Surface animation is entirely procedural in the
// fragment shader (layered sinusoidal distortion -- no external textures).
//
// WaterConfig is stored as an ECS singleton in the registry context
// (same pattern as PostProcessConfig, ShadowConfig).
//
// Tier support: LEGACY (OpenGL 3.3 core profile).
// No tessellation, no compute shaders, no refraction FBO.

#include "core/types.h"
#include "core/ecs.h"
#include "renderer/camera.h"
#include "renderer/shadow_map.h"
#include "renderer/mesh_renderer.h" // FogParams, SceneLighting3D, SkyboxConfig
#include "renderer/rhi_types.h"     // rhi::ShaderHandle

#include <glm/glm.hpp>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// WaterConfig -- ECS singleton controlling water appearance and behaviour.
// POD. No heap. No pointers. Stored in the ECS registry context.
// ---------------------------------------------------------------------------
struct WaterConfig {
    bool      enabled             = false;       // Master switch
    f32       waterLevel          = 0.0f;        // World-space Y height of the water plane
    glm::vec4 shallowColor       = {0.1f, 0.4f, 0.6f, 0.6f};  // RGBA at shallow edges
    glm::vec4 deepColor           = {0.0f, 0.1f, 0.3f, 0.9f};  // RGBA at full depth
    f32       maxDepth            = 10.0f;       // Depth below waterLevel for full deep color
    f32       waveSpeed           = 0.03f;       // Scroll speed for animated distortion
    f32       waveScale           = 0.02f;       // Distortion amplitude in UV space
    f32       fresnelPower        = 2.0f;        // Exponent for Schlick's approximation
    f32       fresnelBias         = 0.1f;        // Minimum reflectivity at normal incidence
    f32       reflectionDistortion = 0.02f;      // Wave distortion applied to reflection UVs
};

// ---------------------------------------------------------------------------
// Water ECS component -- tag that marks an entity as the water plane.
// Per-entity component. Lightweight -- appearance is controlled by the
// WaterConfig singleton, not per-entity data.
// ---------------------------------------------------------------------------
struct Water {
    u32 _tag = 1; // Tag component -- presence triggers water rendering
};
static_assert(sizeof(Water) == 4, "Water must be 4 bytes");

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
inline constexpr f32 DEFAULT_WATER_LEVEL           = 0.0f;
inline constexpr f32 DEFAULT_WAVE_SPEED            = 0.03f;
inline constexpr f32 DEFAULT_WAVE_SCALE            = 0.02f;
inline constexpr f32 DEFAULT_FRESNEL_POWER         = 2.0f;
inline constexpr f32 DEFAULT_FRESNEL_BIAS          = 0.1f;
inline constexpr f32 DEFAULT_MAX_DEPTH             = 10.0f;
inline constexpr f32 DEFAULT_REFLECTION_DISTORTION = 0.02f;

// Default water quad extent in world units (XZ plane).
inline constexpr f32 DEFAULT_WATER_EXTENT = 1000.0f;

// ---------------------------------------------------------------------------
// Water vertex -- position + texcoord (20 bytes per vertex).
// Simpler than MeshVertex (32 bytes) -- water does not need normals or
// tangents in the vertex data (the shader computes procedural normals).
// ---------------------------------------------------------------------------
struct WaterVertex {
    glm::vec3 position;  // 12 bytes
    glm::vec2 texCoord;  //  8 bytes
};
static_assert(sizeof(WaterVertex) == 20, "WaterVertex must be 20 bytes");

// ---------------------------------------------------------------------------
// Reflection camera computation -- exposed for unit testing.
// ---------------------------------------------------------------------------

// Compute a reflected camera across the water plane at the given Y height.
// Flips the camera position and target across the water level:
//   reflectedY = 2 * waterLevel - originalY
// Returns a Camera with the reflected position, target, and inverted up.y.
Camera computeReflectionCamera(const Camera& camera, f32 waterLevel);

// ---------------------------------------------------------------------------
// Pipeline lifecycle -- file-static GPU resources, no class needed.
// Same pattern as post_process.h.
// ---------------------------------------------------------------------------

/// Create reflection FBO (half-resolution) and water quad VAO.
/// Call once after GL context is available. Returns false on failure.
bool initWater(i32 width, i32 height);

/// Recreate reflection FBO at a new resolution. Called from framebuffer resize.
/// Safe to call before initWater (no-op).
void resizeWaterFBOs(i32 width, i32 height);

/// Render the scene into the reflection FBO with a flipped camera.
/// Must be called BEFORE the main scene pass.
/// Only renders if WaterConfig::enabled is true and a Water entity exists.
///
/// This function re-renders the scene (meshes, terrain, skybox) from a
/// reflected viewpoint, using a clip plane to discard geometry below water.
void renderWaterReflection(World& world, const Camera& camera3d,
                           const FogParams& fog, const WaterConfig& waterCfg);

/// Render the water quad with reflection, fresnel, and animated distortion.
/// Must be called AFTER all opaque geometry and BEFORE post-processing.
/// Uses alpha blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA).
void renderWater(World& world, const Camera& camera3d,
                 const FogParams& fog, const WaterConfig& waterCfg,
                 f32 time);

/// Delete all water GPU resources. Safe to call multiple times.
void shutdownWater();

/// Set the water shader handle. Called from initShaderLibrary.
void setWaterShader(rhi::ShaderHandle water);

} // namespace ffe::renderer
