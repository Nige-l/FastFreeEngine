#pragma once

// Vulkan texture management with VMA.
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_init.h"

namespace ffe::rhi::vk {

/// GPU-managed texture backed by VMA allocation.
struct VkManagedTexture {
    VkImage       image      = VK_NULL_HANDLE;
    VkImageView   imageView  = VK_NULL_HANDLE;
    VkSampler     sampler    = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    u32 width  = 0;
    u32 height = 0;
};

/// Create a 2D texture from RGBA pixel data.
/// Pixels are uploaded via a staging buffer. The image is transitioned to
/// SHADER_READ_ONLY_OPTIMAL layout before this function returns.
/// channels must be 4 (RGBA). Returns a default-initialized struct on failure.
VkManagedTexture createTexture2D(VmaAllocator allocator, VkDevice device,
                                  VkCommandPool cmdPool, VkQueue graphicsQueue,
                                  const void* pixels, u32 width, u32 height, u32 channels);

/// Destroy a managed texture and free its VMA allocation.
/// Resets all fields to null/zero after destruction.
void destroyTexture(VmaAllocator allocator, VkDevice device, VkManagedTexture& tex);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
