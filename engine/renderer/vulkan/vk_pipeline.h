#pragma once

// Vulkan graphics pipeline creation.
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_init.h"
#include "renderer/vulkan/vk_shader.h"

namespace ffe::rhi::vk {

/// Configuration for graphics pipeline creation.
struct PipelineConfig {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D   extent     = {0, 0};

    /// Vertex input binding stride in bytes.
    u32 vertexStride = 0;

    /// Vertex attribute descriptions.
    static constexpr u32 MAX_ATTRS = 8;
    VkVertexInputAttributeDescription attrs[MAX_ATTRS] = {};
    u32 attrCount = 0;
};

/// Create a graphics pipeline with the given shader modules and configuration.
/// On success, outLayout receives the pipeline layout handle; the caller must
/// destroy both the returned pipeline and the layout.
/// Returns VK_NULL_HANDLE on failure.
VkPipeline createGraphicsPipeline(VkDevice device,
                                  const VkManagedShader& shader,
                                  const PipelineConfig& config,
                                  VkPipelineLayout& outLayout);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
