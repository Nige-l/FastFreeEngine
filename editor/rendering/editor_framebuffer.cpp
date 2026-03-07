// editor_framebuffer.cpp -- Off-screen FBO for the editor viewport.
//
// Creates a colour (GL_RGBA8) + depth (GL_DEPTH_COMPONENT24) FBO that the
// engine renders the scene into. The colour texture is then sampled by
// ImGui::Image() in the viewport panel.
//
// All GL calls are OpenGL 3.3 core compatible (LEGACY tier).
// No heap allocations. No per-frame allocation.

#include "rendering/editor_framebuffer.h"
#include "core/logging.h"

#include <glad/glad.h>

namespace ffe::editor {

void EditorFramebuffer::init(const int width, const int height) {
    // Guard: must have a valid GL context.
    if (glad_glGenFramebuffers == nullptr) {
        return; // Headless mode -- no GL context available.
    }

    if (width <= 0 || height <= 0) {
        FFE_LOG_ERROR("EditorFBO", "init: invalid dimensions (%dx%d)", width, height);
        return;
    }

    // Clean up any previous resources (safe if already zero).
    shutdown();

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    createAttachments(width, height);

    // Verify completeness.
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("EditorFBO",
                      "init: FBO incomplete (status=0x%X) -- viewport disabled",
                      static_cast<unsigned>(status));
        shutdown();
        return;
    }

    FFE_LOG_INFO("EditorFBO", "Viewport FBO created (%dx%d)", width, height);
}

void EditorFramebuffer::shutdown() {
    deleteAttachments();

    if (m_fbo != 0 && glad_glDeleteFramebuffers != nullptr) {
        glDeleteFramebuffers(1, &m_fbo);
    }
    m_fbo = 0;
    m_width = 0;
    m_height = 0;
}

void EditorFramebuffer::resize(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return; // Ignore invalid sizes (e.g., minimised window).
    }

    if (width == m_width && height == m_height) {
        return; // Nothing to do.
    }

    if (m_fbo == 0) {
        // Not initialised yet -- full init.
        init(width, height);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Delete old attachments and recreate at the new size.
    deleteAttachments();
    createAttachments(width, height);

    // Verify completeness after resize.
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        FFE_LOG_ERROR("EditorFBO",
                      "resize: FBO incomplete after resize to %dx%d (status=0x%X)",
                      width, height, static_cast<unsigned>(status));
        shutdown();
        return;
    }
}

void EditorFramebuffer::bind() {
    if (m_fbo == 0) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void EditorFramebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void EditorFramebuffer::createAttachments(const int width, const int height) {
    // --- Colour texture (GL_RGBA8, LINEAR filter for smooth viewport scaling) ---
    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);

    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_RGBA8,
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);

    // LINEAR filter -- the viewport panel may display the texture at a different
    // size than the FBO resolution (e.g., while the user is resizing the panel).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // CLAMP_TO_EDGE prevents border artefacts when ImGui samples the texture.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_colorTexture, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    // --- Depth texture (GL_DEPTH_COMPONENT24) ---
    // Using a depth texture instead of a renderbuffer to avoid needing
    // renderbuffer GL functions that may be absent from minimal GLAD builds.
    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);

    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_DEPTH_COMPONENT24,
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT,
                 nullptr);

    // Filtering / wrap -- not sampled, but GL requires valid state.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, m_depthTexture, 0);

    glBindTexture(GL_TEXTURE_2D, 0);

    m_width = width;
    m_height = height;
}

void EditorFramebuffer::deleteAttachments() {
    if (glad_glDeleteTextures == nullptr) {
        // No GL context -- nothing to clean up.
        m_colorTexture = 0;
        m_depthTexture = 0;
        return;
    }

    if (m_colorTexture != 0) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_depthTexture != 0) {
        glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
}

} // namespace ffe::editor
