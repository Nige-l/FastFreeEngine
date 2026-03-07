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
static GLuint s_hdrDepthRbo   = 0;

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

    // Depth-stencil renderbuffer
    glGenRenderbuffers(1, &s_hdrDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s_hdrDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          static_cast<GLsizei>(w), static_cast<GLsizei>(h));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Assemble FBO
    glGenFramebuffers(1, &s_hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_hdrColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_hdrDepthRbo);

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

    // Destroy and recreate FBOs at the new size
    destroyFbos();
    createHdrFbo(width, height);
    createBloomFbos(width, height);

    FFE_LOG_INFO("PostProcess", "Post-processing resized to %dx%d", width, height);
}

void beginSceneCapture() {
    if (!s_initialised) return;

    glBindFramebuffer(GL_FRAMEBUFFER, s_hdrFbo);
    glViewport(0, 0, static_cast<GLsizei>(s_width), static_cast<GLsizei>(s_height));

    // Clear the HDR FBO (color + depth) so we start fresh each frame.
    // Ensure depth write is enabled before clearing (may be disabled by 2D restore).
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void executePostProcessing(const PostProcessConfig& config) {
    if (!s_initialised) return;

    // We need all three post-process shaders to be valid.
    // If they are not (e.g. shader compilation failed), fall back to a simple blit.
    const bool shadersReady = rhi::isValid(s_thresholdShader) &&
                              rhi::isValid(s_blurShader) &&
                              rhi::isValid(s_finalShader);

    // Disable depth test and blending for fullscreen passes
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    GLuint bloomTexture = 0; // Will hold the blurred bloom result

    // --- Bloom pass ---
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

    // --- Final composite pass: bloom + tone map + gamma -> default framebuffer ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

        // Uniforms
        rhi::setUniformInt(s_finalShader, "u_bloomEnabled", config.bloomEnabled ? 1 : 0);
        rhi::setUniformFloat(s_finalShader, "u_bloomIntensity", config.bloomIntensity);
        rhi::setUniformInt(s_finalShader, "u_toneMapper", config.toneMapper);
        rhi::setUniformInt(s_finalShader, "u_gammaEnabled", config.gammaEnabled ? 1 : 0);
        rhi::setUniformFloat(s_finalShader, "u_gamma", config.gamma);

        drawFullscreenTriangle();

        // Unbind textures
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

} // namespace ffe::renderer
