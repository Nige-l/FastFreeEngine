#pragma once

// editor_framebuffer.h -- Off-screen FBO for the editor viewport panel.
//
// The editor renders the engine scene into this framebuffer, then displays
// its colour attachment as an ImGui::Image() texture in the viewport panel.
// This keeps the scene render separate from the editor UI render and allows
// the viewport to be resized by dragging the ImGui panel.
//
// Tier support: LEGACY (OpenGL 3.3 core). FBOs with colour + depth
// attachments are core OpenGL 3.0+.
//
// Usage:
//   EditorFramebuffer fb;
//   fb.init(800, 600);
//   // Each frame:
//   fb.bind();
//   app.renderOnce(alpha);   // engine renders into FBO
//   fb.unbind();
//   ImGui::Image((ImTextureID)(uintptr_t)fb.textureId(), ...);
//   // On shutdown:
//   fb.shutdown();

namespace ffe::editor {

class EditorFramebuffer {
public:
    /// Create the FBO with colour texture + depth texture attachment.
    /// Safe to call without a GL context (no-op if glad is not loaded).
    void init(int width, int height);

    /// Destroy all GPU resources. Safe to call on an uninitialised instance.
    void shutdown();

    /// Recreate attachments at a new size. No-op if dimensions match current.
    void resize(int width, int height);

    /// Bind the FBO for rendering into it. Sets glViewport to FBO dimensions.
    void bind();

    /// Unbind the FBO (restore default framebuffer 0).
    /// NOTE: does NOT restore the previous viewport -- the caller is
    /// responsible for setting glViewport back to the window dimensions.
    void unbind();

    /// Colour attachment texture ID, suitable for ImGui::Image().
    unsigned int textureId() const { return m_colorTexture; }

    int width() const { return m_width; }
    int height() const { return m_height; }

    /// Returns true if init() succeeded and GPU resources are valid.
    bool isValid() const { return m_fbo != 0; }

private:
    void createAttachments(int width, int height);
    void deleteAttachments();

    unsigned int m_fbo = 0;
    unsigned int m_colorTexture = 0;
    unsigned int m_depthTexture = 0; // Depth texture attachment
    int m_width = 0;
    int m_height = 0;
};

} // namespace ffe::editor
