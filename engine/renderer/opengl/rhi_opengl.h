#pragma once

// Internal OpenGL backend helpers. Not part of the public API.
// Only included by rhi_opengl.cpp.

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glad/glad.h>

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
