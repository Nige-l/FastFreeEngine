#pragma once

// Vulkan handle-to-object resource pools.
// Maps RHI opaque handles (BufferHandle, TextureHandle, ShaderHandle) to
// Vulkan objects. Static-capacity arrays — no heap allocation.
// Only compiled when FFE_BACKEND_VULKAN=1.

#ifdef FFE_BACKEND_VULKAN

#include "renderer/vulkan/vk_buffer.h"
#include "renderer/vulkan/vk_texture.h"
#include "renderer/vulkan/vk_shader.h"

namespace ffe::rhi::vk {

// Pool capacities — static arrays, no heap.
static constexpr u32 MAX_RHI_BUFFERS  = 256;
static constexpr u32 MAX_RHI_TEXTURES = 256;
static constexpr u32 MAX_RHI_SHADERS  = 32;

// --- Buffer slot ---
struct BufferSlot {
    VkManagedBuffer buffer{};
    // For dynamic/stream buffers: host-visible, persistently mapped
    VmaAllocation   hostAlloc    = VK_NULL_HANDLE;
    void*           mappedPtr    = nullptr;
    bool            hostVisible  = false;
    u32             sizeBytes    = 0;
    bool            active       = false;
};

// --- Texture slot ---
struct TextureSlot {
    VkManagedTexture texture{};
    u32  vramBytes = 0;
    bool active    = false;
};

// --- Shader slot ---
struct ShaderSlot {
    VkManagedShader shader{};
    VkPipeline              pipeline            = VK_NULL_HANDLE;
    VkPipelineLayout        pipelineLayout      = VK_NULL_HANDLE;
    VkDescriptorSetLayout   descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool        descriptorPool      = VK_NULL_HANDLE;
    // Per-frame descriptor sets
    static constexpr u32 MAX_FRAMES = 2;
    VkDescriptorSet         descriptorSets[MAX_FRAMES] = {};
    // Per-frame UBO buffers (scene + light)
    VkBuffer                sceneUboBuffers[MAX_FRAMES]       = {};
    VmaAllocation           sceneUboAllocations[MAX_FRAMES]   = {};
    void*                   sceneUboMapped[MAX_FRAMES]        = {};
    VkBuffer                lightUboBuffers[MAX_FRAMES]       = {};
    VmaAllocation           lightUboAllocations[MAX_FRAMES]   = {};
    void*                   lightUboMapped[MAX_FRAMES]        = {};
    bool active = false;
};

// --- Resource manager ---
struct ResourceManager {
    BufferSlot  buffers[MAX_RHI_BUFFERS];
    TextureSlot textures[MAX_RHI_TEXTURES];
    ShaderSlot  shaders[MAX_RHI_SHADERS];

    // Allocate a free slot. Returns slot index (1-based). 0 = pool exhausted.
    u32 allocBuffer();
    u32 allocTexture();
    u32 allocShader();

    // Free a slot. Safe to call with 0 (no-op).
    void freeBuffer(u32 handle);
    void freeTexture(u32 handle);
    void freeShader(u32 handle);

    // Destroy all active resources. Call during shutdown.
    void destroyAll(VmaAllocator allocator, VkDevice device);
};

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
