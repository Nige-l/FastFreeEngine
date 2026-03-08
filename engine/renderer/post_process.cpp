// post_process.cpp -- Post-processing pipeline implementation for FFE.
//
// HDR scene capture -> bloom (half-res) -> tone mapping -> gamma correction.
// All GPU resources stored as file-static variables. No heap per-frame.
//
// Tier: LEGACY (OpenGL 3.3 core). GL_RGBA16F is core in GL 3.0+.

#include "renderer/post_process.h"
#include "renderer/shader_library.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <glad/glad.h>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// File-static GPU resources
// ---------------------------------------------------------------------------

static bool s_initialised = false;
static i32  s_width  = 0;
static i32  s_height = 0;

// HDR scene FBO (full resolution)
static GLuint s_hdrFbo        = 0;
static GLuint s_hdrColorTex   = 0;
static GLuint s_hdrDepthRbo   = 0;   // Renderbuffer fallback (when SSAO not needed)
static GLuint s_hdrDepthTex   = 0;   // Depth texture (for SSAO sampling)

// Bloom half-res ping-pong FBOs
static GLuint s_bloomFbo[2]   = {0, 0};
static GLuint s_bloomTex[2]   = {0, 0};
static i32    s_bloomWidth    = 0;
static i32    s_bloomHeight   = 0;

// Fullscreen empty VAO (draws a fullscreen triangle via gl_VertexID)
static GLuint s_fullscreenVao = 0;

// Shader handles (compiled via ShaderLibrary)
static rhi::ShaderHandle s_thresholdShader{};
static rhi::ShaderHandle s_blurShader{};
static rhi::ShaderHandle s_finalShader{};
static rhi::ShaderHandle s_fxaaShader{};

// MSAA FBO (multisample — scene renders here when MSAA is active)
static GLuint s_msaaFbo       = 0;
static GLuint s_msaaColorRbo  = 0;  // Multisample color renderbuffer
static GLuint s_msaaDepthRbo  = 0;  // Multisample depth renderbuffer
static i32    s_msaaSamples   = 0;  // Currently configured MSAA sample count (0 = none)

// Intermediate FBO for FXAA pass (between tone mapping and gamma)
// Reuses the same resolution as the main HDR FBO but in LDR (GL_RGBA8).
static GLuint s_fxaaFbo       = 0;
static GLuint s_fxaaColorTex  = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void createHdrFbo(const i32 w, const i32 h) {
    // Color attachment: GL_RGBA16F
    glGenTextures(1, &s_hdrColorTex);
    glBindTexture(GL_TEXTURE_2D, s_hdrColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth attachment: texture (not renderbuffer) so SSAO can sample it.
    // GL_DEPTH_COMPONENT24 is core in GL 3.3. We use a depth-only texture
    // and a separate renderbuffer for stencil if needed (stencil is not
    // currently used by the engine, so depth-only is sufficient).
    glGenTextures(1, &s_hdrDepthTex);
    glBindTexture(GL_TEXTURE_2D, s_hdrDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Assemble FBO
    glGenFramebuffers(1, &s_hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_hdrColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_hdrDepthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("PostProcess", "HDR FBO incomplete (status 0x%X)",
                      glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void createBloomFbos(const i32 w, const i32 h) {
    s_bloomWidth  = w / 2;
    s_bloomHeight = h / 2;
    if (s_bloomWidth < 1) s_bloomWidth = 1;
    if (s_bloomHeight < 1) s_bloomHeight = 1;

    for (i32 i = 0; i < 2; ++i) {
        glGenTextures(1, &s_bloomTex[i]);
        glBindTexture(GL_TEXTURE_2D, s_bloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,
                     static_cast<GLsizei>(s_bloomWidth),
                     static_cast<GLsizei>(s_bloomHeight),
                     0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &s_bloomFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_bloomTex[i], 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            FFE_LOG_ERROR("PostProcess", "Bloom FBO[%d] incomplete", i);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void createMsaaFbo(const i32 w, const i32 h, const i32 samples) {
    // Clamp to GPU-supported maximum
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const i32 actualSamples = (samples > maxSamples) ? maxSamples : samples;
    if (actualSamples < 2) {
        s_msaaSamples = 0;
        return; // GPU does not support MSAA
    }

    // Multisample color renderbuffer (GL_RGBA16F to match HDR FBO)
    glGenRenderbuffers(1, &s_msaaColorRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s_msaaColorRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                     static_cast<GLsizei>(actualSamples),
                                     GL_RGBA16F,
                                     static_cast<GLsizei>(w),
                                     static_cast<GLsizei>(h));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Multisample depth renderbuffer
    glGenRenderbuffers(1, &s_msaaDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s_msaaDepthRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                     static_cast<GLsizei>(actualSamples),
                                     GL_DEPTH24_STENCIL8,
                                     static_cast<GLsizei>(w),
                                     static_cast<GLsizei>(h));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Assemble MSAA FBO
    glGenFramebuffers(1, &s_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_msaaFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_msaaColorRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_msaaDepthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("PostProcess", "MSAA FBO incomplete (status 0x%X, samples=%d)",
                      glCheckFramebufferStatus(GL_FRAMEBUFFER), actualSamples);
        // Clean up and disable MSAA
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &s_msaaFbo);
        glDeleteRenderbuffers(1, &s_msaaColorRbo);
        glDeleteRenderbuffers(1, &s_msaaDepthRbo);
        s_msaaFbo      = 0;
        s_msaaColorRbo = 0;
        s_msaaDepthRbo = 0;
        s_msaaSamples  = 0;
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    s_msaaSamples = actualSamples;
    FFE_LOG_INFO("PostProcess", "MSAA FBO created (%dx%d, %dx samples)",
                 w, h, actualSamples);
}

static void destroyMsaaFbo() {
    if (s_msaaFbo != 0) {
        glDeleteFramebuffers(1, &s_msaaFbo);
        s_msaaFbo = 0;
    }
    if (s_msaaColorRbo != 0) {
        glDeleteRenderbuffers(1, &s_msaaColorRbo);
        s_msaaColorRbo = 0;
    }
    if (s_msaaDepthRbo != 0) {
        glDeleteRenderbuffers(1, &s_msaaDepthRbo);
        s_msaaDepthRbo = 0;
    }
    s_msaaSamples = 0;
}

static void createFxaaFbo(const i32 w, const i32 h) {
    // LDR texture for FXAA input (post tone-map, pre gamma)
    glGenTextures(1, &s_fxaaColorTex);
    glBindTexture(GL_TEXTURE_2D, s_fxaaColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &s_fxaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fxaaFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_fxaaColorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("PostProcess", "FXAA FBO incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void destroyFxaaFbo() {
    if (s_fxaaFbo != 0) {
        glDeleteFramebuffers(1, &s_fxaaFbo);
        s_fxaaFbo = 0;
    }
    if (s_fxaaColorTex != 0) {
        glDeleteTextures(1, &s_fxaaColorTex);
        s_fxaaColorTex = 0;
    }
}

static void destroyFbos() {
    if (s_hdrFbo != 0) {
        glDeleteFramebuffers(1, &s_hdrFbo);
        s_hdrFbo = 0;
    }
    if (s_hdrColorTex != 0) {
        glDeleteTextures(1, &s_hdrColorTex);
        s_hdrColorTex = 0;
    }
    if (s_hdrDepthRbo != 0) {
        glDeleteRenderbuffers(1, &s_hdrDepthRbo);
        s_hdrDepthRbo = 0;
    }
    if (s_hdrDepthTex != 0) {
        glDeleteTextures(1, &s_hdrDepthTex);
        s_hdrDepthTex = 0;
    }
    for (i32 i = 0; i < 2; ++i) {
        if (s_bloomFbo[i] != 0) {
            glDeleteFramebuffers(1, &s_bloomFbo[i]);
            s_bloomFbo[i] = 0;
        }
        if (s_bloomTex[i] != 0) {
            glDeleteTextures(1, &s_bloomTex[i]);
            s_bloomTex[i] = 0;
        }
    }
    destroyMsaaFbo();
    destroyFxaaFbo();
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

bool initPostProcessing(const i32 width, const i32 height) {
    if (rhi::isHeadless()) {
        s_initialised = false;
        return true; // Silently succeed — no GPU work possible.
    }

    if (glad_glGenFramebuffers == nullptr) {
        FFE_LOG_WARN("PostProcess", "GL not available — skipping init");
        return false;
    }

    s_width  = width;
    s_height = height;

    // Create empty VAO for fullscreen triangle (gl_VertexID-based)
    glGenVertexArrays(1, &s_fullscreenVao);

    // Create FBOs
    createHdrFbo(width, height);
    createBloomFbos(width, height);
    createFxaaFbo(width, height);

    s_initialised = true;

    FFE_LOG_INFO("PostProcess", "Post-processing initialised (%dx%d, bloom %dx%d)",
                 width, height, s_bloomWidth, s_bloomHeight);
    return true;
}

void resizePostProcessing(const i32 width, const i32 height) {
    if (!s_initialised) return;
    if (width <= 0 || height <= 0) return;
    if (width == s_width && height == s_height) return;

    s_width  = width;
    s_height = height;

    // Destroy and recreate FBOs at the new size.
    // Save MSAA sample count before destroyFbos clears it.
    const i32 savedMsaaSamples = s_msaaSamples;
    destroyFbos();
    createHdrFbo(width, height);
    createBloomFbos(width, height);
    createFxaaFbo(width, height);
    if (savedMsaaSamples > 0) {
        createMsaaFbo(width, height, savedMsaaSamples);
    }

    FFE_LOG_INFO("PostProcess", "Post-processing resized to %dx%d", width, height);
}

void beginSceneCapture(const f32 clearR, const f32 clearG, const f32 clearB) {
    if (!s_initialised) return;

    // If MSAA is active, bind the MSAA FBO; otherwise bind the regular HDR FBO.
    const GLuint targetFbo = (s_msaaSamples > 0 && s_msaaFbo != 0)
                           ? s_msaaFbo
                           : s_hdrFbo;
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, static_cast<GLsizei>(s_width), static_cast<GLsizei>(s_height));

    // Clear the HDR FBO (color + depth) so we start fresh each frame.
    // Ensure depth write is enabled before clearing (may be disabled by 2D restore).
    // Use the caller-supplied background color so the sky/background is visible when
    // no skybox is loaded. Previously hard-coded to black, which caused black sky on
    // real hardware with post-processing enabled.
    glDepthMask(GL_TRUE);
    glClearColor(clearR, clearG, clearB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void executePostProcessing(const PostProcessConfig& config) {
    if (!s_initialised) return;

    // We need all three post-process shaders to be valid.
    // If they are not (e.g. shader compilation failed), fall back to a simple blit.
    const bool shadersReady = rhi::isValid(s_thresholdShader) &&
                              rhi::isValid(s_blurShader) &&
                              rhi::isValid(s_finalShader);

    const bool fxaaActive = (config.aaMode == 2) && rhi::isValid(s_fxaaShader) &&
                            (s_fxaaFbo != 0) && (s_fxaaColorTex != 0);

    // --- Step 0: MSAA resolve (if active) ---
    // Blit from multisample FBO -> regular HDR FBO before any post-processing
    // reads the scene texture. Post-process shaders cannot read MSAA textures
    // directly on GL 3.3.
    if (s_msaaSamples > 0 && s_msaaFbo != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_msaaFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_hdrFbo);
        // Resolve color (bilinear filter for smooth result)
        glBlitFramebuffer(0, 0, s_width, s_height,
                          0, 0, s_width, s_height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        // Resolve depth (nearest — required for SSAO depth sampling)
        glBlitFramebuffer(0, 0, s_width, s_height,
                          0, 0, s_width, s_height,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    // Disable depth test and blending for fullscreen passes
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    GLuint bloomTexture = 0; // Will hold the blurred bloom result

    // --- Step 1: Bloom pass ---
    if (config.bloomEnabled && shadersReady) {
        // Pass 1: Threshold extract (full-res HDR -> half-res bloom FBO 0)
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[0]);
        glViewport(0, 0, static_cast<GLsizei>(s_bloomWidth),
                         static_cast<GLsizei>(s_bloomHeight));

        rhi::bindShader(s_thresholdShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_hdrColorTex);
        rhi::setUniformInt(s_thresholdShader, "u_scene", 0);
        rhi::setUniformFloat(s_thresholdShader, "u_threshold", config.bloomThreshold);

        drawFullscreenTriangle();

        // Pass 2: Gaussian blur ping-pong (horizontal then vertical)
        // Two passes: H-blur into bloomFbo[1], V-blur back into bloomFbo[0].
        rhi::bindShader(s_blurShader);

        // Horizontal pass: read bloomTex[0], write bloomFbo[1]
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[1]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_bloomTex[0]);
        rhi::setUniformInt(s_blurShader, "u_image", 0);
        rhi::setUniformInt(s_blurShader, "u_horizontal", 1);

        drawFullscreenTriangle();

        // Vertical pass: read bloomTex[1], write bloomFbo[0]
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloomFbo[0]);
        glBindTexture(GL_TEXTURE_2D, s_bloomTex[1]);
        rhi::setUniformInt(s_blurShader, "u_horizontal", 0);

        drawFullscreenTriangle();

        bloomTexture = s_bloomTex[0]; // Final blurred bloom result
    }

    // --- Step 2: Final composite pass (bloom + tone map) ---
    // When FXAA is active: render to FXAA FBO WITHOUT gamma (gamma applied
    // in the FXAA pass). Otherwise: render to default framebuffer with gamma.
    const GLuint finalTarget = fxaaActive ? s_fxaaFbo : 0;
    glBindFramebuffer(GL_FRAMEBUFFER, finalTarget);
    glViewport(0, 0, static_cast<GLsizei>(s_width), static_cast<GLsizei>(s_height));

    if (shadersReady) {
        rhi::bindShader(s_finalShader);

        // Bind HDR scene texture to unit 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_hdrColorTex);
        rhi::setUniformInt(s_finalShader, "u_scene", 0);

        // Bind bloom texture to unit 1 (or 0 if no bloom)
        glActiveTexture(GL_TEXTURE1);
        if (bloomTexture != 0) {
            glBindTexture(GL_TEXTURE_2D, bloomTexture);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        rhi::setUniformInt(s_finalShader, "u_bloom", 1);

        // Bind SSAO texture to unit 2 (if available)
        glActiveTexture(GL_TEXTURE2);
        if (config.ssaoTexture != 0) {
            glBindTexture(GL_TEXTURE_2D, config.ssaoTexture);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        rhi::setUniformInt(s_finalShader, "u_ssaoTex", 2);
        rhi::setUniformInt(s_finalShader, "u_ssaoEnabled", config.ssaoTexture != 0 ? 1 : 0);

        // Uniforms
        rhi::setUniformInt(s_finalShader, "u_bloomEnabled", config.bloomEnabled ? 1 : 0);
        rhi::setUniformFloat(s_finalShader, "u_bloomIntensity", config.bloomIntensity);
        rhi::setUniformInt(s_finalShader, "u_toneMapper", config.toneMapper);

        // When FXAA is active, skip gamma in the final pass — FXAA shader
        // applies gamma after edge detection (FXAA runs in linear space).
        if (fxaaActive) {
            rhi::setUniformInt(s_finalShader, "u_gammaEnabled", 0);
        } else {
            rhi::setUniformInt(s_finalShader, "u_gammaEnabled", config.gammaEnabled ? 1 : 0);
        }
        rhi::setUniformFloat(s_finalShader, "u_gamma", config.gamma);

        drawFullscreenTriangle();

        // Unbind textures
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else {
        // Fallback: blit HDR FBO to default framebuffer (no post-processing).
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_hdrFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, s_width, s_height,
                          0, 0, s_width, s_height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        return; // No FXAA without working shaders
    }

    // --- Step 3: FXAA pass (if active) ---
    // Reads from FXAA FBO texture (post tone-map, pre gamma), applies FXAA
    // edge detection and sub-pixel smoothing, then writes to default framebuffer
    // with gamma correction applied.
    if (fxaaActive) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, static_cast<GLsizei>(s_width), static_cast<GLsizei>(s_height));

        rhi::bindShader(s_fxaaShader);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_fxaaColorTex);
        rhi::setUniformInt(s_fxaaShader, "u_scene", 0);
        rhi::setUniformVec2(s_fxaaShader, "u_inverseScreenSize",
                            glm::vec2(1.0f / static_cast<f32>(s_width),
                                      1.0f / static_cast<f32>(s_height)));
        rhi::setUniformInt(s_fxaaShader, "u_gammaEnabled", config.gammaEnabled ? 1 : 0);
        rhi::setUniformFloat(s_fxaaShader, "u_gamma", config.gamma);

        drawFullscreenTriangle();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void shutdownPostProcessing() {
    if (!s_initialised) return;

    destroyFbos();

    if (s_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &s_fullscreenVao);
        s_fullscreenVao = 0;
    }

    // Clear shader handles — do NOT destroy them here. The ShaderLibrary owns
    // these shaders and will destroy them in shutdownShaderLibrary().
    s_thresholdShader = {};
    s_blurShader      = {};
    s_finalShader     = {};
    s_fxaaShader      = {};

    s_initialised = false;
    s_width  = 0;
    s_height = 0;

    FFE_LOG_INFO("PostProcess", "Post-processing shutdown");
}

u32 getSceneFBOHandle() {
    return static_cast<u32>(s_hdrFbo);
}

bool isPostProcessingInitialised() {
    return s_initialised;
}

u32 getSceneDepthTexture() {
    return static_cast<u32>(s_hdrDepthTex);
}

// ---------------------------------------------------------------------------
// Shader registration — called from initShaderLibrary after the post-process
// shaders are compiled via the ShaderLibrary. This function stores the handles
// so the post-process pipeline can use them each frame.
// ---------------------------------------------------------------------------

void setPostProcessShaders(const rhi::ShaderHandle threshold,
                           const rhi::ShaderHandle blur,
                           const rhi::ShaderHandle final_) {
    s_thresholdShader = threshold;
    s_blurShader      = blur;
    s_finalShader     = final_;
}

void setFxaaShader(const rhi::ShaderHandle fxaa) {
    s_fxaaShader = fxaa;
}

void updateAntiAliasingConfig(const PostProcessConfig& config) {
    if (!s_initialised) return;
    if (rhi::isHeadless()) return;

    const i32 requestedSamples = (config.aaMode == 1)
                               ? clampMsaaSamples(config.msaaSamples)
                               : 0;

    // Recreate MSAA FBO only if the sample count changed
    if (requestedSamples != s_msaaSamples) {
        destroyMsaaFbo();
        if (requestedSamples > 0 && s_width > 0 && s_height > 0) {
            createMsaaFbo(s_width, s_height, requestedSamples);
        }
    }
}

} // namespace ffe::renderer
