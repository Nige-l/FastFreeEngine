#pragma once

// Internal Vulkan backend helpers. Not part of the public API.
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "core/types.h"
#include "core/logging.h"
#include "renderer/rhi_types.h"

// volk must be included before any Vulkan headers.
// Define VOLK_IMPLEMENTATION in exactly one translation unit (vk_init.cpp).
#include <volk.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

// --- VK_CHECK macro ---
// Evaluates a VkResult expression. On failure, logs the error and returns
// the specified return value. No exceptions.
#define VK_CHECK(expr, retval)                                                 \
    do {                                                                        \
        const VkResult vkResult_ = (expr);                                      \
        if (vkResult_ != VK_SUCCESS) {                                          \
            FFE_LOG_ERROR("Vulkan", "%s failed with VkResult %d at %s:%d",      \
                          #expr, static_cast<int>(vkResult_), __FILE__, __LINE__); \
            return retval;                                                      \
        }                                                                       \
    } while (0)

// Variant that returns void
#define VK_CHECK_VOID(expr)                                                    \
    do {                                                                        \
        const VkResult vkResult_ = (expr);                                      \
        if (vkResult_ != VK_SUCCESS) {                                          \
            FFE_LOG_ERROR("Vulkan", "%s failed with VkResult %d at %s:%d",      \
                          #expr, static_cast<int>(vkResult_), __FILE__, __LINE__); \
            return;                                                             \
        }                                                                       \
    } while (0)

namespace ffe::rhi::vk {

// --- Result struct for Vulkan init operations ---
struct VkInitResult {
    bool success = false;
    const char* errorMessage = "";
};

// --- Vulkan context holding all Vulkan state ---
struct VulkanContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          presentQueue   = VK_NULL_HANDLE;
    u32              graphicsFamily = 0;
    u32              presentFamily  = 0;
    VkSurfaceKHR     surface        = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain      = VK_NULL_HANDLE;
    VkFormat         swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D       swapchainExtent = {0, 0};
    VkRenderPass     clearPass      = VK_NULL_HANDLE;

    // Swap chain images (not owned -- destroyed with swapchain)
    static constexpr u32 MAX_SWAP_IMAGES = MAX_SWAPCHAIN_IMAGES;
    VkImage       swapImages[MAX_SWAP_IMAGES]       = {};
    VkImageView   swapImageViews[MAX_SWAP_IMAGES]   = {};
    VkFramebuffer swapFramebuffers[MAX_SWAP_IMAGES] = {};
    u32           swapImageCount = 0;

    // Synchronisation (per frame-in-flight)
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;
    VkSemaphore     imageAvailable[MAX_FRAMES_IN_FLIGHT]  = {};
    VkSemaphore     renderFinished[MAX_FRAMES_IN_FLIGHT]  = {};
    VkFence         inFlightFences[MAX_FRAMES_IN_FLIGHT]  = {};
    VkCommandPool   commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT]  = {};
    u32 currentFrame = 0;

    // Acquired image index for the current frame (-1 if not acquired)
    u32 acquiredImageIndex = 0;
    bool imageAcquired = false;

    // Config snapshot
    bool vsync = true;
    bool debugEnabled = false;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    GLFWwindow* window = nullptr;
};

// --- Vulkan initialization functions ---

/// Create a Vulkan instance with optional validation layers.
/// Validation layers are only requested if available (enumerated first).
VkInitResult createVulkanInstance(VulkanContext& ctx, bool enableValidation);

/// Select the best physical device (prefer discrete GPU).
VkInitResult selectPhysicalDevice(VulkanContext& ctx);

/// Create logical device with graphics and present queues.
VkInitResult createLogicalDevice(VulkanContext& ctx);

/// Create swap chain, image views, render pass, and framebuffers.
VkInitResult createSwapChain(VulkanContext& ctx, u32 preferredImageCount);

/// Create semaphores and fences for frame synchronisation.
VkInitResult createSyncObjects(VulkanContext& ctx);

/// Create command pool and allocate command buffers.
VkInitResult createCommandBuffers(VulkanContext& ctx);

/// Recreate swap chain (e.g., after window resize).
/// Destroys old swap chain resources and creates new ones.
VkInitResult recreateSwapChain(VulkanContext& ctx, u32 preferredImageCount);

/// Destroy swap chain resources (image views, framebuffers, render pass).
void destroySwapChainResources(VulkanContext& ctx);

/// Full Vulkan teardown in reverse order.
void shutdownVulkan(VulkanContext& ctx);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
