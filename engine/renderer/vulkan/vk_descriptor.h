#pragma once

// Vulkan descriptor pool, set layout, and set management.
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_init.h"

namespace ffe::rhi::vk {

/// Maximum descriptor sets that can be allocated from a single pool.
static constexpr u32 MAX_DESCRIPTOR_SETS = 16;

/// UBO binding index used by the textured quad pipeline.
static constexpr u32 DESCRIPTOR_UBO_BINDING = 0;

/// Combined image sampler binding index used by the textured quad pipeline.
static constexpr u32 DESCRIPTOR_SAMPLER_BINDING = 1;

/// Create a descriptor pool that can allocate up to maxSets sets,
/// with the specified number of uniform buffer and combined image sampler descriptors.
VkDescriptorPool createDescriptorPool(VkDevice device, u32 maxSets,
                                       u32 uniformBufferCount, u32 combinedImageSamplerCount);

/// Create a descriptor set layout with:
///   - binding uboBinding: uniform buffer, vertex stage
///   - binding samplerBinding: combined image sampler, fragment stage
VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device,
                                                 u32 uboBinding, u32 samplerBinding);

/// Allocate descriptor sets from a pool using the given layout.
/// outSets must point to an array of at least `count` VkDescriptorSet handles.
void allocateDescriptorSets(VkDevice device, VkDescriptorPool pool,
                            VkDescriptorSetLayout layout,
                            VkDescriptorSet* outSets, u32 count);

/// Update a descriptor set's UBO binding.
void updateDescriptorSetUBO(VkDevice device, VkDescriptorSet set,
                            u32 binding, VkBuffer buffer, VkDeviceSize size);

/// Update a descriptor set's combined image sampler binding.
void updateDescriptorSetTexture(VkDevice device, VkDescriptorSet set,
                                u32 binding, VkImageView imageView, VkSampler sampler);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
