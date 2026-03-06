#pragma once

// Internal OpenGL backend helpers. Not part of the public API.
// Included by rhi_opengl.cpp and by mesh_loader.cpp (for getGlBufferId).

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glad/glad.h>

namespace ffe::rhi {

// Returns the raw OpenGL buffer ID for a given BufferHandle.
// Used by mesh_loader.cpp to obtain the GL ID needed for VAO configuration.
// Returns 0 for invalid handles or in headless mode.
GLuint getGlBufferId(BufferHandle handle);

} // namespace ffe::rhi

namespace ffe::rhi::detail {

// Convert engine enums to GL constants
GLenum toGlBufferTarget(BufferType type);
GLenum toGlBufferUsage(BufferUsage usage);
GLenum toGlTextureFormat(TextureFormat fmt);
GLenum toGlTextureInternalFormat(TextureFormat fmt);
GLenum toGlTextureDataType(TextureFormat fmt);
GLint  toGlTextureFilter(TextureFilter filter);
GLint  toGlTextureWrap(TextureWrap wrap);

// Bytes per pixel for a texture format
u32 bytesPerPixel(TextureFormat fmt);

} // namespace ffe::rhi::detail
