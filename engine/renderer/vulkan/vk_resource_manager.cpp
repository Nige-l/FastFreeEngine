#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_resource_manager.h"

namespace ffe::rhi::vk {

// --- Linear scan for free slot (1-based, 0 = invalid) ---

u32 ResourceManager::allocBuffer() {
    for (u32 i = 1; i < MAX_RHI_BUFFERS; ++i) {
        if (!buffers[i].active) {
            buffers[i].active = true;
            return i;
        }
    }
    return 0;
}

u32 ResourceManager::allocTexture() {
    for (u32 i = 1; i < MAX_RHI_TEXTURES; ++i) {
        if (!textures[i].active) {
            textures[i].active = true;
            return i;
        }
    }
    return 0;
}

u32 ResourceManager::allocShader() {
    for (u32 i = 1; i < MAX_RHI_SHADERS; ++i) {
        if (!shaders[i].active) {
            shaders[i].active = true;
            return i;
        }
    }
    return 0;
}

void ResourceManager::freeBuffer(const u32 handle) {
    if (handle == 0 || handle >= MAX_RHI_BUFFERS) return;
    buffers[handle] = BufferSlot{};
}

void ResourceManager::freeTexture(const u32 handle) {
    if (handle == 0 || handle >= MAX_RHI_TEXTURES) return;
    textures[handle] = TextureSlot{};
}

void ResourceManager::freeShader(const u32 handle) {
    if (handle == 0 || handle >= MAX_RHI_SHADERS) return;
    shaders[handle] = ShaderSlot{};
}

void ResourceManager::destroyAll(VmaAllocator allocator, VkDevice device) {
    // Destroy buffers
    for (u32 i = 1; i < MAX_RHI_BUFFERS; ++i) {
        if (buffers[i].active) {
            if (buffers[i].hostVisible && buffers[i].buffer.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, buffers[i].buffer.buffer, buffers[i].buffer.allocation);
            } else {
                destroyBuffer(allocator, buffers[i].buffer);
            }
            buffers[i] = BufferSlot{};
        }
    }

    // Destroy textures
    for (u32 i = 1; i < MAX_RHI_TEXTURES; ++i) {
        if (textures[i].active) {
            destroyTexture(allocator, device, textures[i].texture);
            textures[i] = TextureSlot{};
        }
    }

    // Destroy shaders (pipeline, layout, descriptor resources, UBOs)
    for (u32 i = 1; i < MAX_RHI_SHADERS; ++i) {
        if (shaders[i].active) {
            ShaderSlot& s = shaders[i];

            // Destroy UBOs
            for (u32 f = 0; f < ShaderSlot::MAX_FRAMES; ++f) {
                if (s.sceneUboBuffers[f] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, s.sceneUboBuffers[f], s.sceneUboAllocations[f]);
                }
                if (s.lightUboBuffers[f] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(allocator, s.lightUboBuffers[f], s.lightUboAllocations[f]);
                }
            }

            if (s.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, s.pipeline, nullptr);
            }
            if (s.pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, s.pipelineLayout, nullptr);
            }
            if (s.descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, s.descriptorSetLayout, nullptr);
            }
            if (s.descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, s.descriptorPool, nullptr);
            }

            destroyShaderModules(device, s.shader);
            shaders[i] = ShaderSlot{};
        }
    }
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
