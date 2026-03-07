#pragma once

// Vulkan uniform buffer management (persistently mapped, per-frame-in-flight).
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_init.h"
#include <glm/glm.hpp>

namespace ffe::rhi::vk {

/// Model-View-Projection uniform block.
/// Matches the GLSL layout: binding 0, set 0.
struct MVPUniform {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

/// Maximum frames in flight (must match VulkanContext::MAX_FRAMES_IN_FLIGHT).
static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

/// Per-frame-in-flight uniform buffers, persistently mapped.
struct VkManagedUniform {
    VkBuffer      buffers[MAX_FRAMES_IN_FLIGHT]     = {};
    VmaAllocation allocations[MAX_FRAMES_IN_FLIGHT]  = {};
    void*         mappedPtrs[MAX_FRAMES_IN_FLIGHT]   = {};
};

/// Create host-visible, host-coherent uniform buffers (one per frame in flight).
/// All buffers are persistently mapped at creation time.
VkManagedUniform createUniformBuffers(VmaAllocator allocator);

/// Destroy uniform buffers and free their VMA allocations.
/// Resets all fields to null/zero after destruction.
void destroyUniformBuffers(VmaAllocator allocator, VkManagedUniform& uniform);

/// Update the uniform data for the given frame index.
/// Uses memcpy to the persistently-mapped pointer (host-coherent, no sync needed).
void updateUniform(const VkManagedUniform& uniform, u32 frameIndex, const MVPUniform& data);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
