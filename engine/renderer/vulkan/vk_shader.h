#pragma once

// Vulkan shader module management.
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_init.h"

namespace ffe::rhi::vk {

/// Holds vertex and fragment shader modules created from SPIR-V binary data.
struct VkManagedShader {
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
};

/// Create shader modules from pre-compiled SPIR-V binary data.
/// vertSpv/fragSpv must point to valid SPIR-V. Sizes are in bytes.
/// No file I/O is performed — SPIR-V data is expected to be embedded at compile time.
VkManagedShader createShaderModules(VkDevice device,
                                    const u32* vertSpv, u32 vertSizeBytes,
                                    const u32* fragSpv, u32 fragSizeBytes);

/// Destroy shader modules and reset handles.
void destroyShaderModules(VkDevice device, VkManagedShader& shader);

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
