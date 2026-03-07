// ssao.cpp -- Screen-Space Ambient Occlusion implementation for FFE.
//
// John Chapman's hemisphere-sampling SSAO:
// 1. Half-res SSAO pass: reconstruct view-space position from depth,
//    sample hemisphere oriented along surface normal, compare depths.
// 2. 4x4 box blur pass: smooth the raw AO to reduce noise.
//
// All GPU resources stored as file-static variables. No heap per-frame.
//
// Tier: LEGACY (OpenGL 3.3 core). All operations use basic texture
// sampling and FBOs, which are core in GL 3.3.

#include "renderer/ssao.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// File-static GPU resources
// ---------------------------------------------------------------------------

static bool s_initialised = false;
static i32  s_width  = 0;
static i32  s_height = 0;
static i32  s_halfWidth  = 0;
static i32  s_halfHeight = 0;

// SSAO FBO (half resolution, single-channel R8)
static GLuint s_ssaoFbo       = 0;
static GLuint s_ssaoColorTex  = 0;

// Blur FBO (half resolution, single-channel R8)
static GLuint s_blurFbo       = 0;
static GLuint s_blurColorTex  = 0;

// 4x4 noise texture (random rotation vectors, GL_REPEAT)
static GLuint s_noiseTex      = 0;

// Hemisphere sample kernel (generated once at init)
static constexpr i32 MAX_KERNEL_SIZE = 64;
static glm::vec3 s_kernel[MAX_KERNEL_SIZE];
static i32 s_kernelSize = 32;

// Shader handles (compiled via ShaderLibrary)
static rhi::ShaderHandle s_ssaoShader{};
static rhi::ShaderHandle s_blurShader{};

// Scene depth texture GL name (set each frame before renderSSAOPass)
static GLuint s_sceneDepthTex = 0;

// Fullscreen empty VAO (shared with post_process — we create our own here
// to avoid cross-module coupling)
static GLuint s_fullscreenVao = 0;

// ---------------------------------------------------------------------------
// Deterministic pseudo-random for kernel generation
// ---------------------------------------------------------------------------

static u32 s_rngSeed = 0x12345678u;

static f32 rngFloat() {
    s_rngSeed = s_rngSeed * 1103515245u + 12345u;
    return static_cast<f32>((s_rngSeed >> 16) & 0x7FFF) / 32768.0f;
}

static f32 rngFloatRange(const f32 lo, const f32 hi) {
    return lo + rngFloat() * (hi - lo);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void generateKernel(const i32 count) {
    s_kernelSize = count;
    for (i32 i = 0; i < count; ++i) {
        // Random point in hemisphere oriented along +Z
        glm::vec3 sample(
            rngFloatRange(-1.0f, 1.0f),
            rngFloatRange(-1.0f, 1.0f),
            rngFloatRange(0.0f, 1.0f)   // hemisphere: z >= 0
        );

        // Normalise and scale
        const f32 len = std::sqrt(sample.x * sample.x + sample.y * sample.y + sample.z * sample.z);
        if (len > 0.0001f) {
            sample /= len;
        }

        // Scale: distribute samples closer to the origin (quadratic falloff)
        f32 scale = static_cast<f32>(i) / static_cast<f32>(count);
        scale = 0.1f + scale * scale * 0.9f; // lerp(0.1, 1.0, scale^2)
        sample *= scale;

        s_kernel[i] = sample;
    }
}

static void generateNoiseTexture() {
    // 4x4 rotation vectors in the tangent plane (z = 0)
    static constexpr i32 NOISE_SIZE = 4;
    glm::vec3 noise[NOISE_SIZE * NOISE_SIZE];

    for (i32 i = 0; i < NOISE_SIZE * NOISE_SIZE; ++i) {
        noise[i] = glm::vec3(
            rngFloatRange(-1.0f, 1.0f),
            rngFloatRange(-1.0f, 1.0f),
            0.0f
        );
        // Normalise
        const f32 len = std::sqrt(noise[i].x * noise[i].x + noise[i].y * noise[i].y);
        if (len > 0.0001f) {
            noise[i] /= len;
        }
    }

    glGenTextures(1, &s_noiseTex);
    glBindTexture(GL_TEXTURE_2D, s_noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F,
                 NOISE_SIZE, NOISE_SIZE,
                 0, GL_RGB, GL_FLOAT, noise);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void createSSAOFbo(const i32 w, const i32 h) {
    s_halfWidth  = w / 2;
    s_halfHeight = h / 2;
    if (s_halfWidth < 1) s_halfWidth = 1;
    if (s_halfHeight < 1) s_halfHeight = 1;

    // SSAO output texture (single-channel, half-res)
    glGenTextures(1, &s_ssaoColorTex);
    glBindTexture(GL_TEXTURE_2D, s_ssaoColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 static_cast<GLsizei>(s_halfWidth),
                 static_cast<GLsizei>(s_halfHeight),
                 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &s_ssaoFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_ssaoFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_ssaoColorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("SSAO", "SSAO FBO incomplete (status 0x%X)",
                      glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }

    // Blur output texture (single-channel, half-res)
    glGenTextures(1, &s_blurColorTex);
    glBindTexture(GL_TEXTURE_2D, s_blurColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 static_cast<GLsizei>(s_halfWidth),
                 static_cast<GLsizei>(s_halfHeight),
                 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &s_blurFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_blurFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_blurColorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("SSAO", "SSAO blur FBO incomplete (status 0x%X)",
                      glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void destroyFbos() {
    if (s_ssaoFbo != 0) {
        glDeleteFramebuffers(1, &s_ssaoFbo);
        s_ssaoFbo = 0;
    }
    if (s_ssaoColorTex != 0) {
        glDeleteTextures(1, &s_ssaoColorTex);
        s_ssaoColorTex = 0;
    }
    if (s_blurFbo != 0) {
        glDeleteFramebuffers(1, &s_blurFbo);
        s_blurFbo = 0;
    }
    if (s_blurColorTex != 0) {
        glDeleteTextures(1, &s_blurColorTex);
        s_blurColorTex = 0;
    }
}

/// Draw a fullscreen triangle using gl_VertexID (no VBO).
static void drawFullscreenTriangle() {
    glBindVertexArray(s_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool initSSAO(const i32 width, const i32 height) {
    if (rhi::isHeadless()) {
        s_initialised = false;
        return true; // Silently succeed — no GPU work possible.
    }

    if (glad_glGenFramebuffers == nullptr) {
        FFE_LOG_WARN("SSAO", "GL not available — skipping init");
        return false;
    }

    s_width  = width;
    s_height = height;

    // Create empty VAO for fullscreen triangle (gl_VertexID-based)
    glGenVertexArrays(1, &s_fullscreenVao);

    // Generate hemisphere sample kernel (default 32 samples)
    generateKernel(32);

    // Generate 4x4 noise texture
    generateNoiseTexture();

    // Create half-res FBOs
    createSSAOFbo(width, height);

    s_initialised = true;

    FFE_LOG_INFO("SSAO", "SSAO initialised (%dx%d, half-res %dx%d)",
                 width, height, s_halfWidth, s_halfHeight);
    return true;
}

void resizeSSAO(const i32 width, const i32 height) {
    if (!s_initialised) return;
    if (width <= 0 || height <= 0) return;
    if (width == s_width && height == s_height) return;

    s_width  = width;
    s_height = height;

    destroyFbos();
    createSSAOFbo(width, height);

    FFE_LOG_INFO("SSAO", "SSAO resized to %dx%d (half-res %dx%d)",
                 width, height, s_halfWidth, s_halfHeight);
}

void renderSSAOPass(const glm::mat4& /*viewMatrix*/,
                    const glm::mat4& projMatrix,
                    const SSAOConfig& config) {
    if (!s_initialised) return;
    if (!config.enabled) return;
    if (!rhi::isValid(s_ssaoShader) || !rhi::isValid(s_blurShader)) return;
    if (s_sceneDepthTex == 0) return;

    // Regenerate kernel if sample count changed
    const i32 clamped = clampSSAOSamples(config.sampleCount);
    if (clamped != s_kernelSize) {
        generateKernel(clamped);
    }

    // Disable depth test and blending for fullscreen passes
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // --- Pass 1: SSAO ---
    glBindFramebuffer(GL_FRAMEBUFFER, s_ssaoFbo);
    glViewport(0, 0, static_cast<GLsizei>(s_halfWidth),
                     static_cast<GLsizei>(s_halfHeight));
    glClear(GL_COLOR_BUFFER_BIT);

    rhi::bindShader(s_ssaoShader);

    // Bind depth texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_sceneDepthTex);
    rhi::setUniformInt(s_ssaoShader, "u_depthTex", 0);

    // Bind noise texture to unit 1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_noiseTex);
    rhi::setUniformInt(s_ssaoShader, "u_noiseTex", 1);

    // Upload projection and inverse projection matrices
    rhi::setUniformMat4(s_ssaoShader, "u_projection", projMatrix);

    const glm::mat4 invProj = glm::inverse(projMatrix);
    rhi::setUniformMat4(s_ssaoShader, "u_invProjection", invProj);

    // Upload sample kernel
    {
        char uniformName[64];
        for (i32 i = 0; i < s_kernelSize; ++i) {
            const auto uIdx = static_cast<unsigned int>(i);
            std::snprintf(uniformName, sizeof(uniformName), "u_samples[%u]", uIdx);
            rhi::setUniformVec3(s_ssaoShader, uniformName, s_kernel[i]);
        }
    }
    rhi::setUniformInt(s_ssaoShader, "u_kernelSize", s_kernelSize);
    rhi::setUniformFloat(s_ssaoShader, "u_radius", clampSSAORadius(config.radius));
    rhi::setUniformFloat(s_ssaoShader, "u_bias", clampSSAOBias(config.bias));
    rhi::setUniformFloat(s_ssaoShader, "u_intensity", clampSSAOIntensity(config.intensity));

    // Noise scale: screen resolution / noise texture size (4x4)
    const glm::vec2 noiseScale(
        static_cast<f32>(s_halfWidth) / 4.0f,
        static_cast<f32>(s_halfHeight) / 4.0f
    );
    rhi::setUniformVec2(s_ssaoShader, "u_noiseScale", noiseScale);

    drawFullscreenTriangle();

    // --- Pass 2: Box blur ---
    glBindFramebuffer(GL_FRAMEBUFFER, s_blurFbo);
    glViewport(0, 0, static_cast<GLsizei>(s_halfWidth),
                     static_cast<GLsizei>(s_halfHeight));

    rhi::bindShader(s_blurShader);

    // Bind raw SSAO texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_ssaoColorTex);
    rhi::setUniformInt(s_blurShader, "u_ssaoInput", 0);

    drawFullscreenTriangle();

    // Unbind
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

u32 getSSAOTexture() {
    if (!s_initialised) return 0;
    return static_cast<u32>(s_blurColorTex);
}

void shutdownSSAO() {
    if (!s_initialised) return;

    destroyFbos();

    if (s_noiseTex != 0) {
        glDeleteTextures(1, &s_noiseTex);
        s_noiseTex = 0;
    }

    if (s_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &s_fullscreenVao);
        s_fullscreenVao = 0;
    }

    // Clear shader handles — ShaderLibrary owns them.
    s_ssaoShader = {};
    s_blurShader = {};
    s_sceneDepthTex = 0;

    s_initialised = false;
    s_width  = 0;
    s_height = 0;

    FFE_LOG_INFO("SSAO", "SSAO shutdown");
}

bool isSSAOInitialised() {
    return s_initialised;
}

void setSSAOShaders(const rhi::ShaderHandle ssao, const rhi::ShaderHandle blur) {
    s_ssaoShader = ssao;
    s_blurShader = blur;
}

void setSceneDepthTexture(const u32 depthTextureGlName) {
    s_sceneDepthTex = depthTextureGlName;
}

} // namespace ffe::renderer
