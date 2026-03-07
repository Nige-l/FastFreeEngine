#pragma once

// screenshot.h — Framebuffer capture to PNG.
//
// Provides a single cold-path function to read the current OpenGL framebuffer
// and write it to a PNG file using stb_image_write.
//
// This is a tooling/debug facility, not an engine hot path. It must never be
// called per-frame. The pixel buffer is heap-allocated for the duration of the
// call and freed before returning.
//
// Tier support: LEGACY and above (requires an active OpenGL 3.3 context).
// Headless mode: captureFramebuffer returns false immediately (no GL context).

namespace ffe::renderer {

/// Capture the current OpenGL framebuffer and write it to a PNG file at `path`.
///
/// Must be called after all draw calls are complete and before the buffer swap
/// (i.e., after render() but before glfwSwapBuffers). The caller is responsible
/// for ensuring a valid OpenGL context is current on the calling thread.
///
/// path:   Destination file path. Absolute or relative to the process CWD.
///         The directory must already exist — this function does not create
///         parent directories.
/// width:  Framebuffer width in pixels (must be > 0).
/// height: Framebuffer height in pixels (must be > 0).
///
/// Returns true on success (PNG written), false on any failure (logs error).
bool captureFramebuffer(const char* path, int width, int height);

} // namespace ffe::renderer
