#ifdef FFE_BACKEND_VULKAN

#include "renderer/rhi.h"
#include "renderer/vulkan/vk_init.h"
#include "core/logging.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <tracy/Tracy.hpp>

namespace ffe::rhi {

// --- Internal state ---
static bool s_headless = false;
static bool s_initialized = false;
static i32  s_viewportWidth  = 0;
static i32  s_viewportHeight = 0;

// Vulkan context
static vk::VulkanContext s_vk{};

// Preferred swap image count (from config)
static u32 s_preferredSwapImages = 2;

// Cached view-projection matrix
static glm::mat4 s_viewProjection{1.0f};

// --- Headless handle counters ---
static u32 s_headlessBufferNext = 1;
static u32 s_headlessTextureNext = 1;
static u32 s_headlessShaderNext = 1;

// ==================== RHI Implementation ====================

RhiResult init(const RhiConfig& config) {
    s_headless = config.headless;
    s_viewportWidth  = config.viewportWidth;
    s_viewportHeight = config.viewportHeight;
    s_preferredSwapImages = config.preferredSwapImages;

    if (s_headless) {
        FFE_LOG_INFO("Renderer", "Headless mode (Vulkan backend) — no GPU operations");
        s_initialized = true;
        return RhiResult::OK;
    }

    if (config.window == nullptr) {
        FFE_LOG_ERROR("Renderer", "Vulkan backend requires a valid window pointer in RhiConfig");
        return RhiResult::ERROR_UNSUPPORTED;
    }

    s_vk.window = config.window;
    s_vk.vsync = config.vsync;

    // 1. Create Vulkan instance
    {
        const vk::VkInitResult result = vk::createVulkanInstance(s_vk, config.debugVulkan);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Vulkan instance creation failed: %s", result.errorMessage);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 2. Create surface (via GLFW)
    {
        const VkResult surfResult = glfwCreateWindowSurface(s_vk.instance, config.window, nullptr, &s_vk.surface);
        if (surfResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Renderer", "Failed to create Vulkan surface (VkResult %d)", static_cast<int>(surfResult));
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 3. Select physical device
    {
        const vk::VkInitResult result = vk::selectPhysicalDevice(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Physical device selection failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 4. Create logical device
    {
        const vk::VkInitResult result = vk::createLogicalDevice(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Logical device creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 5. Create swap chain
    {
        const vk::VkInitResult result = vk::createSwapChain(s_vk, s_preferredSwapImages);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Swap chain creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 6. Create sync objects
    {
        const vk::VkInitResult result = vk::createSyncObjects(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Sync object creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 7. Create command pool and buffers
    {
        const vk::VkInitResult result = vk::createCommandBuffers(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Command buffer creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    s_initialized = true;
    FFE_LOG_INFO("Renderer", "RHI initialized (Vulkan backend, %ux%u)",
                 s_vk.swapchainExtent.width, s_vk.swapchainExtent.height);
    return RhiResult::OK;
}

void shutdown() {
    if (!s_initialized) return;

    if (!s_headless) {
        vk::shutdownVulkan(s_vk);
        s_vk = vk::VulkanContext{};
    }

    // Reset all state
    s_headlessBufferNext = 1;
    s_headlessTextureNext = 1;
    s_headlessShaderNext = 1;
    s_viewportWidth  = 0;
    s_viewportHeight = 0;
    s_initialized = false;

    FFE_LOG_INFO("Renderer", "RHI shutdown complete");
}

// --- Buffers (stub) ---

BufferHandle createBuffer(const BufferDesc& desc) {
    if (s_headless) {
        return BufferHandle{s_headlessBufferNext++};
    }
    FFE_LOG_WARN("Renderer", "createBuffer: Vulkan buffer creation not yet implemented (M2)");
    (void)desc;
    return BufferHandle{0};
}

RhiResult updateBuffer(const BufferHandle handle, const void* data, const u32 sizeBytes, const u32 offset) {
    if (s_headless) return RhiResult::OK;
    FFE_LOG_WARN("Renderer", "updateBuffer: Vulkan not yet implemented (M2)");
    (void)handle; (void)data; (void)sizeBytes; (void)offset;
    return RhiResult::ERROR_UNSUPPORTED;
}

void destroyBuffer(const BufferHandle handle) {
    if (s_headless) return;
    FFE_LOG_WARN("Renderer", "destroyBuffer: Vulkan not yet implemented (M2)");
    (void)handle;
}

// --- Textures (stub) ---

TextureHandle createTexture(const TextureDesc& desc) {
    if (s_headless) {
        return TextureHandle{s_headlessTextureNext++};
    }
    FFE_LOG_WARN("Renderer", "createTexture: Vulkan texture creation not yet implemented (M2)");
    (void)desc;
    return TextureHandle{0};
}

void bindTexture(const TextureHandle handle, const u32 unitIndex) {
    if (s_headless) return;
    (void)handle; (void)unitIndex;
}

void destroyTexture(const TextureHandle handle) {
    if (s_headless) return;
    (void)handle;
}

u32 textureVramUsed() {
    return 0;
}

u32 getTextureWidth(const TextureHandle handle) {
    (void)handle;
    return 0;
}

u32 getTextureHeight(const TextureHandle handle) {
    (void)handle;
    return 0;
}

void updateTextureSubImage(const TextureHandle handle, const u32 x, const u32 y,
                           const u32 width, const u32 height, const void* pixelData) {
    if (s_headless) return;
    (void)handle; (void)x; (void)y; (void)width; (void)height; (void)pixelData;
}

bool readTexturePixels(const TextureHandle handle, void* outBuffer, const u32 bufferSize) {
    (void)handle; (void)outBuffer; (void)bufferSize;
    return false;
}

// --- Shaders (stub) ---

ShaderHandle createShader(const ShaderDesc& desc) {
    if (desc.vertexSource == nullptr || desc.fragmentSource == nullptr) {
        FFE_LOG_ERROR("Renderer", "createShader called with null source");
        return ShaderHandle{0};
    }
    if (s_headless) {
        return ShaderHandle{s_headlessShaderNext++};
    }
    FFE_LOG_WARN("Renderer", "createShader: Vulkan shader compilation not yet implemented (M2)");
    return ShaderHandle{0};
}

void bindShader(const ShaderHandle handle) {
    if (s_headless) return;
    (void)handle;
}

void destroyShader(const ShaderHandle handle) {
    if (s_headless) return;
    (void)handle;
}

// --- Uniforms (stub) ---

void setUniformInt(const ShaderHandle handle, const char* name, const i32 value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformFloat(const ShaderHandle handle, const char* name, const f32 value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformVec2(const ShaderHandle handle, const char* name, const glm::vec2& value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformVec3(const ShaderHandle handle, const char* name, const glm::vec3& value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformVec4(const ShaderHandle handle, const char* name, const glm::vec4& value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformMat3(const ShaderHandle handle, const char* name, const glm::mat3& value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformMat4(const ShaderHandle handle, const char* name, const glm::mat4& value) {
    if (s_headless) return;
    (void)handle; (void)name; (void)value;
}

void setUniformMat4Array(const ShaderHandle handle, const char* name, const glm::mat4* values, const u32 count) {
    if (s_headless) return;
    (void)handle; (void)name; (void)values; (void)count;
}

// --- Pipeline state (stub) ---

void applyPipelineState(const PipelineState& state) {
    if (s_headless) return;
    (void)state;
}

// --- Frame ---

void beginFrame(const glm::vec4& clearColor) {
    if (s_headless) return;

    ZoneScopedN("RHI::beginFrame");

    // Skip rendering when framebuffer is zero-size (window minimised)
    if (s_vk.swapchainExtent.width == 0 || s_vk.swapchainExtent.height == 0) {
        s_vk.imageAcquired = false;
        return;
    }

    const u32 frame = s_vk.currentFrame;

    // Wait for this frame's fence
    vkWaitForFences(s_vk.device, 1, &s_vk.inFlightFences[frame], VK_TRUE, UINT64_MAX);

    // Acquire next swap chain image
    const VkResult acquireResult = vkAcquireNextImageKHR(
        s_vk.device, s_vk.swapchain, UINT64_MAX,
        s_vk.imageAvailable[frame], VK_NULL_HANDLE,
        &s_vk.acquiredImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swap chain is out of date — recreate and retry
        const vk::VkInitResult recreateResult = vk::recreateSwapChain(s_vk, s_preferredSwapImages);
        if (!recreateResult.success) {
            FFE_LOG_ERROR("Renderer", "Swap chain recreation failed: %s", recreateResult.errorMessage);
            s_vk.imageAcquired = false;
            return;
        }
        // Skip this frame after recreation
        s_vk.imageAcquired = false;
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        FFE_LOG_ERROR("Renderer", "vkAcquireNextImageKHR failed with VkResult %d",
                      static_cast<int>(acquireResult));
        s_vk.imageAcquired = false;
        return;
    }

    // Validate swap chain image index bounds
    if (s_vk.acquiredImageIndex >= s_vk.swapImageCount) {
        FFE_LOG_ERROR("Renderer", "Acquired image index %u out of bounds (swapImageCount=%u)",
                      s_vk.acquiredImageIndex, s_vk.swapImageCount);
        s_vk.imageAcquired = false;
        return;
    }

    vkResetFences(s_vk.device, 1, &s_vk.inFlightFences[frame]);
    vkResetCommandBuffer(s_vk.commandBuffers[frame], 0);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    {
        const VkResult beginResult = vkBeginCommandBuffer(s_vk.commandBuffers[frame], &beginInfo);
        if (beginResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "vkBeginCommandBuffer failed with VkResult %d", static_cast<int>(beginResult));
            s_vk.imageAcquired = false;
            return;
        }
    }

    // Begin render pass with clear color
    VkClearValue clear{};
    clear.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = s_vk.clearPass;
    rpBegin.framebuffer       = s_vk.swapFramebuffers[s_vk.acquiredImageIndex];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = s_vk.swapchainExtent;
    rpBegin.clearValueCount   = 1;
    rpBegin.pClearValues      = &clear;

    vkCmdBeginRenderPass(s_vk.commandBuffers[frame], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // M1: nothing drawn -- just clear
    vkCmdEndRenderPass(s_vk.commandBuffers[frame]);

    {
        const VkResult endResult = vkEndCommandBuffer(s_vk.commandBuffers[frame]);
        if (endResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "vkEndCommandBuffer failed with VkResult %d", static_cast<int>(endResult));
            s_vk.imageAcquired = false;
            return;
        }
    }

    // Mark image as acquired only after command buffer is fully recorded
    s_vk.imageAcquired = true;
}

void endFrame(GLFWwindow* window) {
    if (s_headless) return;
    if (window == nullptr) return;
    if (!s_vk.imageAcquired) return;

    ZoneScopedN("RHI::endFrame");

    const u32 frame = s_vk.currentFrame;

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    const VkSemaphore waitSemaphores[] = {s_vk.imageAvailable[frame]};
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores    = waitSemaphores;
    submitInfo.pWaitDstStageMask  = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &s_vk.commandBuffers[frame];

    const VkSemaphore signalSemaphores[] = {s_vk.renderFinished[frame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    VK_CHECK_VOID(vkQueueSubmit(s_vk.graphicsQueue, 1, &submitInfo, s_vk.inFlightFences[frame]));

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;

    const VkSwapchainKHR swapchains[] = {s_vk.swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains    = swapchains;
    presentInfo.pImageIndices  = &s_vk.acquiredImageIndex;

    const VkResult presentResult = vkQueuePresentKHR(s_vk.presentQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        const vk::VkInitResult recreateResult = vk::recreateSwapChain(s_vk, s_preferredSwapImages);
        if (!recreateResult.success) {
            FFE_LOG_ERROR("Renderer", "Swap chain recreation failed during present: %s",
                          recreateResult.errorMessage);
        }
    } else if (presentResult != VK_SUCCESS) {
        FFE_LOG_ERROR("Renderer", "vkQueuePresentKHR failed with VkResult %d",
                      static_cast<int>(presentResult));
    }

    s_vk.currentFrame = (s_vk.currentFrame + 1) % vk::VulkanContext::MAX_FRAMES_IN_FLIGHT;
    s_vk.imageAcquired = false;
}

void setViewport(const i32 x, const i32 y, const i32 width, const i32 height) {
    if (s_headless) return;
    // Vulkan viewport is set during command buffer recording (M2+)
    (void)x; (void)y; (void)width; (void)height;
}

void setScissor(const i32 x, const i32 y, const i32 width, const i32 height) {
    if (s_headless) return;
    (void)x; (void)y; (void)width; (void)height;
}

void setViewProjection(const glm::mat4& vp) {
    s_viewProjection = vp;
}

// --- Draw calls (stub) ---

void drawArrays(const BufferHandle vertexBuffer, const u32 vertexCount, const u32 vertexOffset) {
    if (s_headless) return;
    (void)vertexBuffer; (void)vertexCount; (void)vertexOffset;
}

void drawIndexed(const BufferHandle vertexBuffer, const BufferHandle indexBuffer,
                 const u32 indexCount, const u32 vertexOffset, const u32 indexOffset) {
    if (s_headless) return;
    (void)vertexBuffer; (void)indexBuffer; (void)indexCount; (void)vertexOffset; (void)indexOffset;
}

// --- Query functions ---

bool isHeadless() {
    return s_headless;
}

i32 getViewportWidth() {
    return s_viewportWidth;
}

i32 getViewportHeight() {
    return s_viewportHeight;
}

void setViewportSize(const i32 width, const i32 height) {
    s_viewportWidth  = width;
    s_viewportHeight = height;
}

} // namespace ffe::rhi

#endif // FFE_BACKEND_VULKAN
