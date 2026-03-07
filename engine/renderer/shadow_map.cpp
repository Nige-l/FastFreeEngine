// shadow_map.cpp — Directional light shadow map implementation for FFE.
//
// createShadowMap: allocates a GL_DEPTH_COMPONENT24 texture + depth-only FBO.
//   - NEAREST filter (no filtering on the depth texture — PCF is done manually in GLSL)
//   - GL_CLAMP_TO_BORDER with border = {1,1,1,1} so fragments outside the shadow
//     frustum test as fully lit (shadow factor = 1.0).
//
// computeLightSpaceMatrix: builds an orthographic VP from the light direction,
//   projecting the scene from a "sun" position offset along -lightDir * 20 units
//   looking toward the world origin.
//
// No heap allocations. No per-frame allocation.

#include "renderer/shadow_map.h"
#include "core/logging.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

// GL_CLAMP_TO_BORDER is core in OpenGL 3.3 but not declared in our minimal GLAD.
// Value per the OpenGL 3.3 spec (Table 3.18).
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif

// GL_DEPTH_COMPONENT24 is core in OpenGL 3.3.
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

// GL_DEPTH_COMPONENT is already defined in glad.h via the depth constants block.
// Guard in case some platforms omit it.
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif

namespace ffe {

ShadowMap createShadowMap(const int resolution) {
    ShadowMap sm{};

    // Guard: must have a valid resolution and a real GL context.
    // glGenFramebuffers is NULL in headless mode (glad function pointers not loaded).
    if (resolution <= 0) {
        FFE_LOG_ERROR("ShadowMap", "createShadowMap: resolution must be > 0 (got %d)", resolution);
        return sm;
    }
    if (glad_glGenFramebuffers == nullptr) {
        // Headless mode — no GL context. Return zero struct (callers guard enabled flag).
        return sm;
    }

    // --- Depth texture ---
    glGenTextures(1, &sm.depthTexture);
    glBindTexture(GL_TEXTURE_2D, sm.depthTexture);

    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_DEPTH_COMPONENT24,
                 static_cast<GLsizei>(resolution), static_cast<GLsizei>(resolution),
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT,
                 nullptr);

    // NEAREST filter — PCF is computed manually in the shader.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // CLAMP_TO_BORDER so areas beyond the shadow frustum read depth = 1.0
    // (border color {1,1,1,1} → depth = 1.0 → unoccluded).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    {
        static const GLfloat BORDER_COLOR[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, BORDER_COLOR);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    // --- Framebuffer ---
    glGenFramebuffers(1, &sm.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sm.fbo);

    // Attach depth texture only — no colour attachment.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, sm.depthTexture, 0);

    // Explicitly tell GL we do not intend to draw or read colour.
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    // Verify completeness.
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("ShadowMap",
                      "createShadowMap: FBO incomplete (status=0x%X) — shadows disabled",
                      static_cast<unsigned>(status));
        // Partial cleanup.
        glDeleteFramebuffers(1, &sm.fbo);
        glDeleteTextures(1, &sm.depthTexture);
        sm = ShadowMap{};
        return sm;
    }

    sm.resolution = resolution;
    FFE_LOG_INFO("ShadowMap", "Shadow map created (%dx%d)", resolution, resolution);
    return sm;
}

void destroyShadowMap(ShadowMap& sm) {
    if (sm.fbo == 0 && sm.depthTexture == 0) {
        return; // Nothing to destroy.
    }
    if (glad_glDeleteFramebuffers != nullptr) {
        if (sm.fbo != 0) {
            glDeleteFramebuffers(1, &sm.fbo);
        }
        if (sm.depthTexture != 0) {
            glDeleteTextures(1, &sm.depthTexture);
        }
    }
    sm = ShadowMap{};
}

glm::mat4 computeLightSpaceMatrix(const glm::vec3& lightDir, const ShadowConfig& cfg) {
    // Normalise light direction defensively; caller is responsible for non-zero input
    // but we guard to avoid NaN propagation.
    const float len = glm::length(lightDir);
    const glm::vec3 dir = (len > 1e-4f) ? (lightDir / len) : glm::vec3{0.5f, -1.0f, 0.3f};

    // Place the light "eye" 20 units behind the light direction from the origin.
    // This gives a stable world-space anchor for the shadow frustum.
    const glm::vec3 lightEye = -dir * 20.0f;

    const float hw = cfg.areaWidth  * 0.5f;
    const float hh = cfg.areaHeight * 0.5f;

    const glm::mat4 lightProj = glm::ortho(-hw, hw, -hh, hh,
                                            cfg.nearPlane, cfg.farPlane);
    const glm::mat4 lightView = glm::lookAt(lightEye,
                                             glm::vec3{0.0f, 0.0f, 0.0f},
                                             glm::vec3{0.0f, 1.0f, 0.0f});
    return lightProj * lightView;
}

} // namespace ffe
