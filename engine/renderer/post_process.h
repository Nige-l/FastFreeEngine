#pragma once

// post_process.h -- Post-processing pipeline for FFE.
//
// Provides HDR scene capture (GL_RGBA16F FBO), bloom (half-res ping-pong),
// tone mapping (Reinhard / ACES), and gamma correction.
//
// All GPU resources (FBOs, textures, VAO) are created at init/resize only.
// No per-frame heap allocation. No virtual functions.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).
// GL_RGBA16F is core in GL 3.0+. No extensions needed.

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <cmath>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// PostProcessConfig — ECS singleton controlling post-processing behaviour.
// POD. No heap. No pointers. Stored in the ECS registry context.
// ---------------------------------------------------------------------------
struct PostProcessConfig {
    bool  bloomEnabled     = false;    // Master bloom switch
    f32   bloomThreshold   = 1.0f;     // Luminance threshold for bright-pass extract
    f32   bloomIntensity   = 0.5f;     // Additive bloom blend strength
    i32   toneMapper       = 0;        // 0 = none, 1 = Reinhard, 2 = ACES filmic
    bool  gammaEnabled     = true;     // Apply pow(color, 1/gamma) in final pass
    f32   gamma            = 2.2f;     // Gamma exponent

    // Anti-aliasing settings
    i32   aaMode           = 0;        // 0 = none, 1 = MSAA, 2 = FXAA
    i32   msaaSamples      = 2;        // MSAA sample count: 2, 4, or 8 (clamped)

    // SSAO: the AO texture GL name is passed via this field each frame.
    // 0 = no SSAO (ambient light at full strength).
    u32   ssaoTexture      = 0;
};

// ---------------------------------------------------------------------------
// Pipeline lifecycle — file-static GPU resources, no class needed.
// ---------------------------------------------------------------------------

/// Create HDR scene FBO, bloom ping-pong FBOs (half-res), and fullscreen quad VAO.
/// Call once after GL context is available. Returns false on failure.
bool initPostProcessing(i32 width, i32 height);

/// Recreate FBOs at a new resolution. Called from framebuffer resize callback.
/// Safe to call before initPostProcessing (no-op).
void resizePostProcessing(i32 width, i32 height);

/// Bind the HDR FBO so the scene renders into it instead of the default framebuffer.
/// Call at the start of the render pass (before meshRenderSystem / skybox / 2D).
void beginSceneCapture();

/// Run the post-process chain: bloom extract -> blur -> composite + tone map + gamma.
/// Outputs to the default framebuffer (FBO 0).
/// If no effects are enabled, blits the HDR FBO to the default framebuffer directly.
void executePostProcessing(const PostProcessConfig& config);

/// Delete all FBOs, textures, and VAO. Safe to call multiple times.
void shutdownPostProcessing();

/// Returns the GL name of the HDR scene FBO (for editor FBO integration if needed).
u32 getSceneFBOHandle();

/// Returns the GL name of the HDR scene depth texture.
/// Used by SSAO to sample the depth buffer. Returns 0 if not initialised.
u32 getSceneDepthTexture();

/// Returns true if the post-processing pipeline has been initialised.
bool isPostProcessingInitialised();

/// Set post-process shader handles. Called from initShaderLibrary after
/// the post-process shaders are compiled. The pipeline uses these handles
/// each frame — no ownership transfer (ShaderLibrary still owns them).
void setPostProcessShaders(rhi::ShaderHandle threshold,
                           rhi::ShaderHandle blur,
                           rhi::ShaderHandle final_);

/// Set the FXAA post-process shader handle. Called from initShaderLibrary.
void setFxaaShader(rhi::ShaderHandle fxaa);

/// Notify the post-process pipeline that the AA mode or MSAA sample count
/// has changed. Recreates the MSAA FBO if needed. Safe to call any time
/// after initPostProcessing.
void updateAntiAliasingConfig(const PostProcessConfig& config);

// ---------------------------------------------------------------------------
// Math utilities (exposed for unit testing without GL context).
// ---------------------------------------------------------------------------

/// Reinhard tone mapping: c / (1 + c), per channel.
/// Input and output are linear HDR values.
inline void tonemapReinhard(f32& r, f32& g, f32& b) {
    r = r / (1.0f + r);
    g = g / (1.0f + g);
    b = b / (1.0f + b);
}

/// ACES filmic tone mapping (Stephen Hill fitted approximation).
/// Input and output are linear HDR values.
inline void tonemapACES(f32& r, f32& g, f32& b) {
    auto aces = [](f32 x) -> f32 {
        return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    };
    r = aces(r);
    g = aces(g);
    b = aces(b);
}

/// Gamma correction: pow(c, 1/gamma), per channel.
inline void gammaCorrect(f32& r, f32& g, f32& b, const f32 gamma) {
    const f32 invGamma = 1.0f / gamma;
    auto safePow = [](f32 base, f32 exponent) -> f32 {
        if (base <= 0.0f) return 0.0f;
        return std::pow(base, exponent);
    };
    r = safePow(r, invGamma);
    g = safePow(g, invGamma);
    b = safePow(b, invGamma);
}

/// Luminance (ITU BT.709 weights). Used for bloom threshold test.
inline f32 luminance(const f32 r, const f32 g, const f32 b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/// FXAA luma: dot(rgb, vec3(0.299, 0.587, 0.114)).
/// Uses the same weights as the FXAA shader for perceptual luminance.
/// Exposed for unit testing the luma calculation without a GL context.
inline f32 fxaaLuma(const f32 r, const f32 g, const f32 b) {
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

/// Clamp an MSAA sample count to the nearest valid power-of-two: 2, 4, or 8.
/// Rounds down to the nearest valid value:
/// <=2 -> 2, 3 -> 2, 4 -> 4, 5-7 -> 4, 8+ -> 8.
inline i32 clampMsaaSamples(const i32 requested) {
    if (requested >= 8) return 8;
    if (requested >= 4) return 4;
    return 2;
}

} // namespace ffe::renderer
