#pragma once

// shadow_map.h — Directional light shadow mapping for FFE.
//
// Creates and manages a depth-only FBO for rendering a shadow map from the
// directional light's point of view. The resulting depth texture is bound
// to texture unit 1 during the main Blinn-Phong pass and sampled with a
// 3x3 PCF kernel to produce soft shadows.
//
// Tier support: LEGACY (OpenGL 3.3 core). Framebuffer objects are core in 3.3.
// Default disabled — call createShadowMap() and set ShadowConfig::enabled = true
// to activate. Calling any function without a GL context (headless mode) is safe:
// createShadowMap returns {0,0,0} and all other functions are no-ops.

#include <cstdint>
#include <glm/glm.hpp>

namespace ffe {

// Configuration for the shadow pass.
// Stored per-scene; settable from Lua via ffe.enableShadows / ffe.setShadowBias /
// ffe.setShadowArea. Default: disabled (no shadow pass overhead).
struct ShadowConfig {
    int   resolution = 1024;   // Shadow map width and height in texels
    float bias       = 0.005f; // Depth bias to prevent shadow acne
    float areaWidth  = 20.0f;  // Orthographic projection width (world units)
    float areaHeight = 20.0f;  // Orthographic projection height (world units)
    float nearPlane  = 0.1f;   // Near clip plane for the light frustum
    float farPlane   = 50.0f;  // Far clip plane for the light frustum
    bool  enabled    = false;  // Master switch: no overhead when false
};

// GPU-side shadow map: depth texture + FBO.
// Zero-initialised means no GL resources allocated (safe to pass to destroyShadowMap).
struct ShadowMap {
    uint32_t fbo          = 0; // Framebuffer object
    uint32_t depthTexture = 0; // GL_DEPTH_COMPONENT24 2D texture
    int    resolution   = 0; // Width == height
};

// Create a square depth-only FBO of the given resolution.
// In headless mode (no GL context), returns a zero-initialised ShadowMap.
// On GL error (FBO incomplete), destroys partial resources and returns zero.
ShadowMap createShadowMap(int resolution);

// Destroy the shadow map's GPU resources. Safe to call on a zero-initialised
// ShadowMap (no-op). Zeros the struct after destruction.
void destroyShadowMap(ShadowMap& sm);

// Compute the orthographic light-space matrix used for the shadow pass.
// lightDir is the direction light rays travel (normalised internally).
// Returns: glm::ortho(frustum) * glm::lookAt(lightEyePos, origin, up).
glm::mat4 computeLightSpaceMatrix(const glm::vec3& lightDir, const ShadowConfig& cfg);

} // namespace ffe
