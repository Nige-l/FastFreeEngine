#pragma once

#include "renderer/rhi_types.h"
#include <glm/glm.hpp>

// Forward declare GLFWwindow to avoid pulling in GLFW header in public API
struct GLFWwindow;

namespace ffe::rhi {

// --- Lifecycle ---
RhiResult init(const RhiConfig& config);
void shutdown();

// --- Query ---
/// Returns true if the RHI was initialised in headless mode (no GPU context).
bool isHeadless();
/// Returns the viewport width (pixels) set at init time. Returns 0 if not yet initialised.
i32 getViewportWidth();
/// Returns the viewport height (pixels) set at init time. Returns 0 if not yet initialised.
i32 getViewportHeight();

// --- Buffer operations ---
BufferHandle createBuffer(const BufferDesc& desc);
RhiResult updateBuffer(BufferHandle handle, const void* data, u32 sizeBytes, u32 offset = 0);
void destroyBuffer(BufferHandle handle);

// --- Texture operations ---
TextureHandle createTexture(const TextureDesc& desc);
void bindTexture(TextureHandle handle, u32 unitIndex);
void destroyTexture(TextureHandle handle);
u32 textureVramUsed();

// --- Shader operations ---
ShaderHandle createShader(const ShaderDesc& desc);
void bindShader(ShaderHandle handle);
void destroyShader(ShaderHandle handle);

// --- Uniform setters ---
void setUniformInt(ShaderHandle handle, const char* name, i32 value);
void setUniformFloat(ShaderHandle handle, const char* name, f32 value);
void setUniformVec2(ShaderHandle handle, const char* name, const glm::vec2& value);
void setUniformVec3(ShaderHandle handle, const char* name, const glm::vec3& value);
void setUniformVec4(ShaderHandle handle, const char* name, const glm::vec4& value);
void setUniformMat3(ShaderHandle handle, const char* name, const glm::mat3& value);
void setUniformMat4(ShaderHandle handle, const char* name, const glm::mat4& value);
void setUniformMat4Array(ShaderHandle handle, const char* name, const glm::mat4* values, u32 count);

// --- Pipeline state ---
void applyPipelineState(const PipelineState& state);

// --- Frame operations ---
void beginFrame(const glm::vec4& clearColor);
void endFrame(GLFWwindow* window);

// --- Viewport ---
void setViewport(i32 x, i32 y, i32 width, i32 height);
void setScissor(i32 x, i32 y, i32 width, i32 height);

// --- Camera matrix cache ---
void setViewProjection(const glm::mat4& vp);

// --- Draw calls ---
void drawArrays(BufferHandle vertexBuffer, u32 vertexCount, u32 vertexOffset);
void drawIndexed(BufferHandle vertexBuffer, BufferHandle indexBuffer,
                 u32 indexCount, u32 vertexOffset, u32 indexOffset);

} // namespace ffe::rhi
