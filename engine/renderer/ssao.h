#pragma once

// ssao.h -- Screen-Space Ambient Occlusion for FFE.
//
// Implements John Chapman's hemisphere-sampling SSAO algorithm:
// 1. Generate a hemisphere sample kernel + 4x4 noise texture at init
// 2. Render SSAO at half resolution using depth buffer reconstruction
// 3. Blur the raw AO with a 4x4 box blur to reduce noise
// 4. Output a single-channel AO texture for the final composite
//
// All GPU resources (FBOs, textures, kernel) are created at init/resize only.
// No per-frame heap allocation. No virtual functions.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).
// SSAO uses basic texture sampling + FBOs, all available in GL 3.3.

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// SSAOConfig -- ECS singleton controlling SSAO behaviour.
// POD. No heap. No pointers. Stored in the ECS registry context.
// ---------------------------------------------------------------------------
struct SSAOConfig {
    bool enabled     = false;
    f32  radius      = 0.5f;     // sample hemisphere radius in view space
    f32  bias        = 0.025f;   // depth bias to prevent self-occlusion
    i32  sampleCount = 32;       // sample count: 16, 32, or 64
    f32  intensity   = 1.0f;     // AO darkening multiplier
};

// ---------------------------------------------------------------------------
// Pipeline lifecycle -- file-static GPU resources, no class needed.
// ---------------------------------------------------------------------------

/// Create SSAO FBO (half-res), blur FBO, sample kernel, and noise texture.
/// Call once after GL context is available. Returns false on failure.
bool initSSAO(i32 width, i32 height);

/// Recreate FBOs at a new resolution. Called from framebuffer resize callback.
/// Safe to call before initSSAO (no-op).
void resizeSSAO(i32 width, i32 height);

/// Run the SSAO pass: sample hemisphere -> write occlusion -> blur.
/// Reads from the scene depth texture (HDR FBO depth). Outputs a blurred
/// single-channel AO texture. Must be called after scene render, before
/// the final post-process composite.
void renderSSAOPass(const glm::mat4& viewMatrix,
                    const glm::mat4& projMatrix,
                    const SSAOConfig& config);

/// Returns the GL texture name of the blurred AO result.
/// Bind this during the final composite to multiply ambient occlusion.
/// Returns 0 if SSAO is not initialised or not available.
u32 getSSAOTexture();

/// Delete all FBOs, textures, and kernel data. Safe to call multiple times.
void shutdownSSAO();

/// Returns true if the SSAO pipeline has been initialised.
bool isSSAOInitialised();

/// Set SSAO shader handles. Called from initShaderLibrary after
/// the SSAO shaders are compiled.
void setSSAOShaders(rhi::ShaderHandle ssao, rhi::ShaderHandle blur);

/// Notify the SSAO pipeline of the scene depth texture (from HDR FBO).
/// Must be called each frame before renderSSAOPass so SSAO can read depth.
void setSceneDepthTexture(u32 depthTextureGlName);

// ---------------------------------------------------------------------------
// Validation utilities (exposed for unit testing without GL context).
// ---------------------------------------------------------------------------

/// Clamp a sample count to one of the valid values: 16, 32, or 64.
/// Rounds to nearest valid value:
/// <=16 -> 16, 17-48 -> 32, 49+ -> 64.
inline i32 clampSSAOSamples(const i32 requested) {
    if (requested >= 49) return 64;
    if (requested >= 17) return 32;
    return 16;
}

/// Clamp radius to a reasonable range [0.01, 5.0].
inline f32 clampSSAORadius(const f32 radius) {
    if (radius < 0.01f) return 0.01f;
    if (radius > 5.0f) return 5.0f;
    return radius;
}

/// Clamp bias to a reasonable range [0.0, 0.5].
inline f32 clampSSAOBias(const f32 bias) {
    if (bias < 0.0f) return 0.0f;
    if (bias > 0.5f) return 0.5f;
    return bias;
}

/// Clamp intensity to a reasonable range [0.0, 5.0].
inline f32 clampSSAOIntensity(const f32 intensity) {
    if (intensity < 0.0f) return 0.0f;
    if (intensity > 5.0f) return 5.0f;
    return intensity;
}

} // namespace ffe::renderer
