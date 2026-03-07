#ifdef FFE_BACKEND_VULKAN

// volk must be included before VMA so that Vulkan function declarations are available.
#include <volk.h>

// VMA_IMPLEMENTATION must be defined in exactly one translation unit.
// VMA needs Vulkan function pointers; volk provides them globally.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include "renderer/vulkan/vk_buffer.h"

#include <cstring>

namespace ffe::rhi::vk {

// ============================================================================
// Single-time command helpers
// ============================================================================

static VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool cmdPool) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = cmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    const VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &cmdBuf);
    if (result != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to allocate single-time command buffer (VkResult %d)",
                      static_cast<int>(result));
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    const VkResult beginResult = vkBeginCommandBuffer(cmdBuf, &beginInfo);
    if (beginResult != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to begin single-time command buffer (VkResult %d)",
                      static_cast<int>(beginResult));
        vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
        return VK_NULL_HANDLE;
    }

    return cmdBuf;
}

static void endSingleTimeCommands(VkDevice device, VkCommandPool cmdPool,
                                  VkQueue queue, VkCommandBuffer cmdBuf) {
    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmdBuf;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
}

// ============================================================================
// Internal: create a device-local buffer with staging upload
// ============================================================================

static VkManagedBuffer createDeviceLocalBuffer(VmaAllocator allocator, VkDevice device,
                                               VkCommandPool cmdPool, VkQueue graphicsQueue,
                                               const void* data, const VkDeviceSize size,
                                               const VkBufferUsageFlags usage) {
    VkManagedBuffer result{};

    if (data == nullptr || size == 0) {
        FFE_LOG_ERROR("Vulkan", "createDeviceLocalBuffer: null data or zero size");
        return result;
    }

    // --- Create staging buffer (CPU-visible) ---
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size  = size;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;

    VkResult vkr = vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo,
                                   &stagingBuffer, &stagingAlloc, nullptr);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to create staging buffer (VkResult %d)", static_cast<int>(vkr));
        return result;
    }

    // Map staging buffer and copy data
    void* mapped = nullptr;
    vkr = vmaMapMemory(allocator, stagingAlloc, &mapped);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to map staging buffer (VkResult %d)", static_cast<int>(vkr));
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return result;
    }

    std::memcpy(mapped, data, static_cast<size_t>(size));
    vmaUnmapMemory(allocator, stagingAlloc);

    // --- Create device-local buffer (GPU-only) ---
    VkBufferCreateInfo deviceBufferInfo{};
    deviceBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    deviceBufferInfo.size  = size;
    deviceBufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    deviceBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo deviceAllocInfo{};
    deviceAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vkr = vmaCreateBuffer(allocator, &deviceBufferInfo, &deviceAllocInfo,
                          &result.buffer, &result.allocation, nullptr);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to create device-local buffer (VkResult %d)", static_cast<int>(vkr));
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return result;
    }
    result.size = size;

    // --- Copy staging -> device-local via one-shot command buffer ---
    VkCommandBuffer cmdBuf = beginSingleTimeCommands(device, cmdPool);
    if (cmdBuf == VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, result.buffer, result.allocation);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        result = {};
        return result;
    }

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size      = size;
    vkCmdCopyBuffer(cmdBuf, stagingBuffer, result.buffer, 1, &copyRegion);

    endSingleTimeCommands(device, cmdPool, graphicsQueue, cmdBuf);

    // --- Destroy staging buffer ---
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    return result;
}

// ============================================================================
// Public API
// ============================================================================

VkManagedBuffer createVertexBuffer(VmaAllocator allocator, VkDevice device,
                                   VkCommandPool cmdPool, VkQueue graphicsQueue,
                                   const void* data, const VkDeviceSize size) {
    return createDeviceLocalBuffer(allocator, device, cmdPool, graphicsQueue,
                                   data, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

VkManagedBuffer createIndexBuffer(VmaAllocator allocator, VkDevice device,
                                  VkCommandPool cmdPool, VkQueue graphicsQueue,
                                  const void* data, const VkDeviceSize size) {
    return createDeviceLocalBuffer(allocator, device, cmdPool, graphicsQueue,
                                   data, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void destroyBuffer(VmaAllocator allocator, VkManagedBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
        buffer.buffer     = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.size       = 0;
    }
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
