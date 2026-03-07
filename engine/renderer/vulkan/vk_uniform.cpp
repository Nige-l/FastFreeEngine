#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_uniform.h"

#include <cstring>

namespace ffe::rhi::vk {

VkManagedUniform createUniformBuffers(VmaAllocator allocator) {
    VkManagedUniform result{};

    const VkDeviceSize bufferSize = sizeof(MVPUniform);

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size        = bufferSize;
        bufInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocResult{};
        const VkResult vkr = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                             &result.buffers[i], &result.allocations[i],
                                             &allocResult);
        if (vkr != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "createUniformBuffers: buffer %u creation failed (VkResult %d)",
                          i, static_cast<int>(vkr));
            // Clean up any previously created buffers
            for (u32 j = 0; j < i; ++j) {
                vmaDestroyBuffer(allocator, result.buffers[j], result.allocations[j]);
                result.buffers[j]     = VK_NULL_HANDLE;
                result.allocations[j] = VK_NULL_HANDLE;
                result.mappedPtrs[j]  = nullptr;
            }
            return result;
        }

        // VMA_ALLOCATION_CREATE_MAPPED_BIT means the allocation is already mapped
        result.mappedPtrs[i] = allocResult.pMappedData;

        if (result.mappedPtrs[i] == nullptr) {
            // Fallback: map explicitly
            const VkResult mapResult = vmaMapMemory(allocator, result.allocations[i],
                                                    &result.mappedPtrs[i]);
            if (mapResult != VK_SUCCESS) {
                FFE_LOG_ERROR("Vulkan", "createUniformBuffers: buffer %u map failed (VkResult %d)",
                              i, static_cast<int>(mapResult));
            }
        }
    }

    FFE_LOG_INFO("Vulkan", "Uniform buffers created (%u frames, %zu bytes each)",
                 MAX_FRAMES_IN_FLIGHT, static_cast<size_t>(bufferSize));
    return result;
}

void destroyUniformBuffers(VmaAllocator allocator, VkManagedUniform& uniform) {
    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (uniform.buffers[i] != VK_NULL_HANDLE) {
            // VMA handles unmap automatically when destroying the buffer
            vmaDestroyBuffer(allocator, uniform.buffers[i], uniform.allocations[i]);
            uniform.buffers[i]     = VK_NULL_HANDLE;
            uniform.allocations[i] = VK_NULL_HANDLE;
            uniform.mappedPtrs[i]  = nullptr;
        }
    }
}

void updateUniform(const VkManagedUniform& uniform, const u32 frameIndex, const MVPUniform& data) {
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT) {
        FFE_LOG_ERROR("Vulkan", "updateUniform: frameIndex %u out of bounds", frameIndex);
        return;
    }

    void* const dst = uniform.mappedPtrs[frameIndex];
    if (dst != nullptr) {
        std::memcpy(dst, &data, sizeof(MVPUniform));
    }
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
