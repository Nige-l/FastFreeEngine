#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_descriptor.h"

namespace ffe::rhi::vk {

VkDescriptorPool createDescriptorPool(VkDevice device, const u32 maxSets,
                                       const u32 uniformBufferCount,
                                       const u32 combinedImageSamplerCount) {
    VkDescriptorPoolSize poolSizes[2]{};
    u32 poolSizeCount = 0;

    if (uniformBufferCount > 0) {
        poolSizes[poolSizeCount].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[poolSizeCount].descriptorCount = uniformBufferCount;
        ++poolSizeCount;
    }

    if (combinedImageSamplerCount > 0) {
        poolSizes[poolSizeCount].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[poolSizeCount].descriptorCount = combinedImageSamplerCount;
        ++poolSizeCount;
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizeCount;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = maxSets;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    const VkResult vkr = vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createDescriptorPool failed (VkResult %d)", static_cast<int>(vkr));
        return VK_NULL_HANDLE;
    }

    FFE_LOG_INFO("Vulkan", "Descriptor pool created (maxSets=%u, UBOs=%u, samplers=%u)",
                 maxSets, uniformBufferCount, combinedImageSamplerCount);
    return pool;
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device,
                                                 const u32 uboBinding,
                                                 const u32 samplerBinding) {
    VkDescriptorSetLayoutBinding bindings[2]{};

    // Binding 0: UBO (vertex stage)
    bindings[0].binding            = uboBinding;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Combined image sampler (fragment stage)
    bindings[1].binding            = samplerBinding;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings    = bindings;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult vkr = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createDescriptorSetLayout failed (VkResult %d)", static_cast<int>(vkr));
        return VK_NULL_HANDLE;
    }

    FFE_LOG_INFO("Vulkan", "Descriptor set layout created (UBO binding=%u, sampler binding=%u)",
                 uboBinding, samplerBinding);
    return layout;
}

void allocateDescriptorSets(VkDevice device, VkDescriptorPool pool,
                            VkDescriptorSetLayout layout,
                            VkDescriptorSet* outSets, const u32 count) {
    if (count == 0 || outSets == nullptr) return;

    // Build array of identical layouts (one per set)
    VkDescriptorSetLayout layouts[MAX_DESCRIPTOR_SETS];
    const u32 allocCount = (count <= MAX_DESCRIPTOR_SETS) ? count : MAX_DESCRIPTOR_SETS;
    for (u32 i = 0; i < allocCount; ++i) {
        layouts[i] = layout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = pool;
    allocInfo.descriptorSetCount = allocCount;
    allocInfo.pSetLayouts        = layouts;

    const VkResult vkr = vkAllocateDescriptorSets(device, &allocInfo, outSets);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "allocateDescriptorSets failed (VkResult %d)", static_cast<int>(vkr));
    }
}

void updateDescriptorSetUBO(VkDevice device, VkDescriptorSet set,
                            const u32 binding, VkBuffer buffer, const VkDeviceSize size) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = size;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = binding;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo     = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void updateDescriptorSetTexture(VkDevice device, VkDescriptorSet set,
                                const u32 binding, VkImageView imageView, VkSampler sampler) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView   = imageView;
    imageInfo.sampler     = sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = binding;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
