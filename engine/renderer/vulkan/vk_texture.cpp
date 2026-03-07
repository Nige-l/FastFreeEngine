#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_texture.h"

#include <cstring>

namespace ffe::rhi::vk {

// ============================================================================
// Single-time command helpers (duplicated from vk_buffer.cpp to avoid
// cross-TU static linkage; these are small and init-time only)
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
// Image layout transition helper
// ============================================================================

static void transitionImageLayout(VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                  VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer cmdBuf = beginSingleTimeCommands(device, cmdPool);
    if (cmdBuf == VK_NULL_HANDLE) return;

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        FFE_LOG_WARN("Vulkan", "transitionImageLayout: unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmdBuf, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(device, cmdPool, queue, cmdBuf);
}

// ============================================================================
// Public API
// ============================================================================

VkManagedTexture createTexture2D(VmaAllocator allocator, VkDevice device,
                                  VkCommandPool cmdPool, VkQueue graphicsQueue,
                                  const void* pixels, const u32 width, const u32 height,
                                  const u32 channels) {
    VkManagedTexture result{};

    if (pixels == nullptr || width == 0 || height == 0) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: null pixels or zero dimensions");
        return result;
    }
    if (channels != 4) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: only 4-channel RGBA is supported (got %u)", channels);
        return result;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    // --- Create staging buffer ---
    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size        = imageSize;
    stagingBufInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;

    VkResult vkr = vmaCreateBuffer(allocator, &stagingBufInfo, &stagingAllocInfo,
                                   &stagingBuffer, &stagingAlloc, nullptr);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: staging buffer creation failed (VkResult %d)",
                      static_cast<int>(vkr));
        return result;
    }

    // Copy pixel data to staging buffer
    void* mapped = nullptr;
    vkr = vmaMapMemory(allocator, stagingAlloc, &mapped);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: staging buffer map failed (VkResult %d)",
                      static_cast<int>(vkr));
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return result;
    }
    std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(allocator, stagingAlloc);

    // --- Create VkImage ---
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vkr = vmaCreateImage(allocator, &imageInfo, &imageAllocInfo,
                         &result.image, &result.allocation, nullptr);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: image creation failed (VkResult %d)",
                      static_cast<int>(vkr));
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        return result;
    }

    result.width  = width;
    result.height = height;

    // --- Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL ---
    transitionImageLayout(device, cmdPool, graphicsQueue, result.image,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // --- Copy staging buffer to image ---
    {
        VkCommandBuffer cmdBuf = beginSingleTimeCommands(device, cmdPool);
        if (cmdBuf == VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, result.image, result.allocation);
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
            result = {};
            return result;
        }

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(cmdBuf, stagingBuffer, result.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        endSingleTimeCommands(device, cmdPool, graphicsQueue, cmdBuf);
    }

    // --- Transition: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL ---
    transitionImageLayout(device, cmdPool, graphicsQueue, result.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // --- Destroy staging buffer ---
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

    // --- Create image view ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = result.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    vkr = vkCreateImageView(device, &viewInfo, nullptr, &result.imageView);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: image view creation failed (VkResult %d)",
                      static_cast<int>(vkr));
        vmaDestroyImage(allocator, result.image, result.allocation);
        result = {};
        return result;
    }

    // --- Create sampler ---
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter               = VK_FILTER_LINEAR;
    samplerInfo.minFilter               = VK_FILTER_LINEAR;
    samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable        = VK_FALSE;
    samplerInfo.maxAnisotropy           = 1.0f;
    samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable           = VK_FALSE;
    samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias              = 0.0f;
    samplerInfo.minLod                  = 0.0f;
    samplerInfo.maxLod                  = 0.0f;

    vkr = vkCreateSampler(device, &samplerInfo, nullptr, &result.sampler);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createTexture2D: sampler creation failed (VkResult %d)",
                      static_cast<int>(vkr));
        vkDestroyImageView(device, result.imageView, nullptr);
        vmaDestroyImage(allocator, result.image, result.allocation);
        result = {};
        return result;
    }

    FFE_LOG_INFO("Vulkan", "Texture created: %ux%u RGBA", width, height);
    return result;
}

void destroyTexture(VmaAllocator allocator, VkDevice device, VkManagedTexture& tex) {
    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, tex.sampler, nullptr);
        tex.sampler = VK_NULL_HANDLE;
    }
    if (tex.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, tex.imageView, nullptr);
        tex.imageView = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, tex.image, tex.allocation);
        tex.image      = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
    }
    tex.width  = 0;
    tex.height = 0;
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
