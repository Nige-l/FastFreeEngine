#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_shader.h"

namespace ffe::rhi::vk {

VkManagedShader createShaderModules(VkDevice device,
                                    const u32* vertSpv, const u32 vertSizeBytes,
                                    const u32* fragSpv, const u32 fragSizeBytes) {
    VkManagedShader result{};

    if (vertSpv == nullptr || vertSizeBytes == 0) {
        FFE_LOG_ERROR("Vulkan", "createShaderModules: null or empty vertex SPIR-V");
        return result;
    }
    if (fragSpv == nullptr || fragSizeBytes == 0) {
        FFE_LOG_ERROR("Vulkan", "createShaderModules: null or empty fragment SPIR-V");
        return result;
    }

    // SPIR-V must be aligned to 4 bytes (u32)
    if (vertSizeBytes % 4 != 0) {
        FFE_LOG_ERROR("Vulkan", "createShaderModules: vertex SPIR-V size %u is not 4-byte aligned",
                      vertSizeBytes);
        return result;
    }
    if (fragSizeBytes % 4 != 0) {
        FFE_LOG_ERROR("Vulkan", "createShaderModules: fragment SPIR-V size %u is not 4-byte aligned",
                      fragSizeBytes);
        return result;
    }

    // Create vertex shader module
    VkShaderModuleCreateInfo vertInfo{};
    vertInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertInfo.codeSize = vertSizeBytes;
    vertInfo.pCode    = vertSpv;

    VkResult vkr = vkCreateShaderModule(device, &vertInfo, nullptr, &result.vertModule);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to create vertex shader module (VkResult %d)",
                      static_cast<int>(vkr));
        return result;
    }

    // Create fragment shader module
    VkShaderModuleCreateInfo fragInfo{};
    fragInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragInfo.codeSize = fragSizeBytes;
    fragInfo.pCode    = fragSpv;

    vkr = vkCreateShaderModule(device, &fragInfo, nullptr, &result.fragModule);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to create fragment shader module (VkResult %d)",
                      static_cast<int>(vkr));
        // Clean up the vertex module that was successfully created
        vkDestroyShaderModule(device, result.vertModule, nullptr);
        result.vertModule = VK_NULL_HANDLE;
        return result;
    }

    FFE_LOG_INFO("Vulkan", "Shader modules created (vert: %u bytes, frag: %u bytes)",
                 vertSizeBytes, fragSizeBytes);
    return result;
}

void destroyShaderModules(VkDevice device, VkManagedShader& shader) {
    if (shader.fragModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, shader.fragModule, nullptr);
        shader.fragModule = VK_NULL_HANDLE;
    }
    if (shader.vertModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, shader.vertModule, nullptr);
        shader.vertModule = VK_NULL_HANDLE;
    }
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
