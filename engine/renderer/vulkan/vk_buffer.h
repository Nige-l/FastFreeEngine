#pragma once

// Vulkan buffer management with VMA (Vulkan Memory Allocator).
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_init.h"

// VMA forward declarations — the actual implementation header is included
// in exactly one .cpp file with VMA_IMPLEMENTATION defined.
#include <vk_mem_alloc.h>

namespace ffe::rhi::vk {

/// GPU-managed buffer backed by VMA allocation.
struct VkManagedBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize  size       = 0;
};

/// Create a device-local vertex buffer with staging upload.
/// data must point to at least `size` bytes. The staging buffer is freed
/// before this function returns.
VkManagedBuffer createVertexBuffer(VmaAllocator allocator, VkDevice device,
                                   VkCommandPool cmdPool, VkQueue graphicsQueue,
                                   const void* data, VkDeviceSize size);

/// Create a device-local index buffer with staging upload.
VkManagedBuffer createIndexBuffer(VmaAllocator allocator, VkDevice device,
                                  VkCommandPool cmdPool, VkQueue graphicsQueue,
                                  const void* data, VkDeviceSize size);

/// Destroy a managed buffer and free its VMA allocation.
/// Resets the buffer fields to null after destruction.
void destroyBuffer(VmaAllocator allocator, VkManagedBuffer& buffer);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
