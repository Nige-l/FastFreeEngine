#ifdef FFE_BACKEND_VULKAN

#include "renderer/rhi.h"
#include "renderer/vulkan/vk_init.h"
#include "renderer/vulkan/vk_buffer.h"
#include "renderer/vulkan/vk_shader.h"
#include "renderer/vulkan/vk_pipeline.h"
#include "renderer/vulkan/vk_texture.h"
#include "renderer/vulkan/vk_descriptor.h"
#include "renderer/vulkan/vk_uniform.h"
#include "renderer/vulkan/vk_resource_manager.h"
// SPIR-V shader headers: prefer build-time compiled shaders (M5),
// fall back to hand-assembled headers if no SPIR-V compiler was available.
#ifdef FFE_SPIRV_COMPILED
#include "shaders/triangle_vert_spv.h"
#include "shaders/triangle_frag_spv.h"
#include "shaders/textured_vert_spv.h"
#include "shaders/textured_frag_spv.h"
#include "shaders/blinn_phong_vert_spv.h"
#include "shaders/blinn_phong_frag_spv.h"
#else
#include "renderer/vulkan/shaders/triangle_vert.h"
#include "renderer/vulkan/shaders/triangle_frag.h"
#include "renderer/vulkan/shaders/textured_vert.h"
#include "renderer/vulkan/shaders/textured_frag.h"
#include "renderer/vulkan/shaders/blinn_phong_vert.h"
#include "renderer/vulkan/shaders/blinn_phong_frag.h"
#endif
#include "core/logging.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tracy/Tracy.hpp>
#include <cstddef>
#include <cstring>

namespace ffe::rhi {

// ============================================================================
// UBO layouts for Blinn-Phong mesh rendering (std140 compatible)
// ============================================================================

/// Scene UBO: binding 0, vertex stage.
/// 4 * mat4 = 4 * 64 = 256 bytes.
struct SceneUBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 normalMatrix; // mat3 padded to mat4 for std140
};
static_assert(sizeof(SceneUBO) == 256, "SceneUBO must be 256 bytes (4 * mat4)");

/// Light UBO: binding 1, fragment stage.
/// 4 * vec4 = 4 * 16 = 64 bytes.
struct LightUBO {
    glm::vec4 lightDir;     // xyz = direction, w = unused
    glm::vec4 lightColor;   // xyz = color, w = unused
    glm::vec4 ambientColor; // xyz = color, w = unused
    glm::vec4 cameraPos;    // xyz = position, w = unused
};
static_assert(sizeof(LightUBO) == 64, "LightUBO must be 64 bytes (4 * vec4)");

// ============================================================================
// Uniform staging area
// ============================================================================
// Collects setUniform* calls. Packed into UBOs at draw time.

static constexpr u32 MAX_UNIFORM_ENTRIES = 32;

enum class UniformType : u8 {
    INT,
    FLOAT,
    VEC2,
    VEC3,
    VEC4,
    MAT3,
    MAT4
};

struct UniformEntry {
    u32 nameHash = 0;
    UniformType type = UniformType::INT;
    union {
        i32         intVal;
        f32         floatVal;
        glm::vec2   vec2Val;
        glm::vec3   vec3Val;
        glm::vec4   vec4Val;
        glm::mat3   mat3Val;
        glm::mat4   mat4Val;
    } data{};
};

struct UniformStaging {
    UniformEntry entries[MAX_UNIFORM_ENTRIES];
    u32 count = 0;

    void clear() { count = 0; }

    void set(const u32 hash, const UniformType t, const void* val, const u32 size) {
        // Overwrite existing entry with same hash, or append
        for (u32 i = 0; i < count; ++i) {
            if (entries[i].nameHash == hash) {
                entries[i].type = t;
                std::memcpy(&entries[i].data, val, size);
                return;
            }
        }
        if (count < MAX_UNIFORM_ENTRIES) {
            entries[count].nameHash = hash;
            entries[count].type = t;
            std::memcpy(&entries[count].data, val, size);
            ++count;
        }
    }

    const UniformEntry* find(const u32 hash) const {
        for (u32 i = 0; i < count; ++i) {
            if (entries[i].nameHash == hash) return &entries[i];
        }
        return nullptr;
    }
};

// Simple FNV-1a hash for uniform name lookup
static constexpr u32 fnvHash(const char* str) {
    u32 hash = 2166136261u;
    while (*str != '\0') {
        hash ^= static_cast<u32>(static_cast<u8>(*str));
        hash *= 16777619u;
        ++str;
    }
    return hash;
}

// Known uniform name hashes (precomputed for hot-path lookup)
static constexpr u32 HASH_U_MODEL         = fnvHash("u_model");
static constexpr u32 HASH_U_VIEW          = fnvHash("u_view");
static constexpr u32 HASH_U_PROJECTION    = fnvHash("u_projection");
static constexpr u32 HASH_U_NORMAL_MATRIX = fnvHash("u_normalMatrix");
static constexpr u32 HASH_U_LIGHT_DIR     = fnvHash("u_lightDir");
static constexpr u32 HASH_U_LIGHT_COLOR   = fnvHash("u_lightColor");
static constexpr u32 HASH_U_AMBIENT_COLOR = fnvHash("u_ambientColor");
static constexpr u32 HASH_U_CAMERA_POS    = fnvHash("u_cameraPos");

// ============================================================================
// Internal state
// ============================================================================

static bool s_headless = false;
static bool s_initialized = false;
static i32  s_viewportWidth  = 0;
static i32  s_viewportHeight = 0;

// Vulkan context
static vk::VulkanContext s_vk{};

// Resource manager (M4)
static vk::ResourceManager s_resources{};

// Preferred swap image count (from config)
static u32 s_preferredSwapImages = 2;

// Cached view-projection matrix
static glm::mat4 s_viewProjection{1.0f};

// Uniform staging area — populated by setUniform* calls, consumed by draw*
static UniformStaging s_uniforms{};

// Current bound state for draw calls
static ShaderHandle s_currentShader{};
static TextureHandle s_boundTextures[4] = {}; // Up to 4 texture units

// Viewport/scissor for dynamic state
static VkViewport s_viewport{};
static VkRect2D   s_scissor{};

// Total VRAM used by textures (tracked for textureVramUsed())
static u32 s_totalTextureVram = 0;

// --- Headless handle counters ---
static u32 s_headlessBufferNext = 1;
static u32 s_headlessTextureNext = 1;
static u32 s_headlessShaderNext = 1;

// --- M3 textured quad demo resources ---
static vk::VkManagedBuffer  s_quadVB{};
static vk::VkManagedBuffer  s_quadIB{};
static vk::VkManagedShader  s_quadShader{};
static vk::VkManagedTexture s_quadTexture{};
static vk::VkManagedUniform s_quadUniforms{};

// Textured quad vertex: position (vec2) + texcoord (vec2) = 16 bytes
struct TexturedVertex {
    f32 x, y;
    f32 u, v;
};
static_assert(sizeof(TexturedVertex) == 16);

// Quad vertices: centered, half-size 0.5, CCW winding
static constexpr TexturedVertex QUAD_VERTICES[] = {
    { -0.5f, -0.5f,   0.0f, 0.0f },  // bottom-left
    {  0.5f, -0.5f,   1.0f, 0.0f },  // bottom-right
    {  0.5f,  0.5f,   1.0f, 1.0f },  // top-right
    { -0.5f,  0.5f,   0.0f, 1.0f },  // top-left
};

// Two triangles forming the quad
static constexpr u16 QUAD_INDICES[] = {
    0, 1, 2,
    2, 3, 0,
};

// ============================================================================
// Internal helpers
// ============================================================================

/// Create a host-visible, persistently-mapped uniform buffer.
/// Returns the VkBuffer, allocation, and mapped pointer via out params.
/// Returns false on failure.
static bool createMappedUniformBuffer(VmaAllocator allocator, const VkDeviceSize size,
                                      VkBuffer& outBuffer, VmaAllocation& outAlloc, void*& outMapped) {
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = size;
    bufInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult{};
    const VkResult vkr = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                         &outBuffer, &outAlloc, &allocResult);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createMappedUniformBuffer: failed (VkResult %d)", static_cast<int>(vkr));
        return false;
    }

    outMapped = allocResult.pMappedData;
    if (outMapped == nullptr) {
        // Fallback: map explicitly
        const VkResult mapResult = vmaMapMemory(allocator, outAlloc, &outMapped);
        if (mapResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "createMappedUniformBuffer: map failed (VkResult %d)",
                          static_cast<int>(mapResult));
            vmaDestroyBuffer(allocator, outBuffer, outAlloc);
            outBuffer = VK_NULL_HANDLE;
            outAlloc  = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

/// Create a host-visible, persistently mapped buffer for dynamic vertex/index data.
static vk::VkManagedBuffer createHostVisibleBuffer(VmaAllocator allocator, const VkDeviceSize size,
                                                    const VkBufferUsageFlags usage,
                                                    void*& outMapped) {
    vk::VkManagedBuffer result{};
    outMapped = nullptr;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = size;
    bufInfo.usage       = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult{};
    const VkResult vkr = vmaCreateBuffer(allocator, &bufInfo, &allocInfo,
                                         &result.buffer, &result.allocation, &allocResult);
    if (vkr != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "createHostVisibleBuffer: failed (VkResult %d)", static_cast<int>(vkr));
        return result;
    }
    result.size = size;
    outMapped = allocResult.pMappedData;

    if (outMapped == nullptr) {
        const VkResult mapResult = vmaMapMemory(allocator, result.allocation, &outMapped);
        if (mapResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "createHostVisibleBuffer: map failed (VkResult %d)",
                          static_cast<int>(mapResult));
        }
    }
    return result;
}

/// Pack staged uniforms into SceneUBO and LightUBO structs.
static void packUniforms(const UniformStaging& staging, SceneUBO& scene, LightUBO& light) {
    // Defaults
    scene.model        = glm::mat4(1.0f);
    scene.view         = glm::mat4(1.0f);
    scene.projection   = glm::mat4(1.0f);
    scene.normalMatrix = glm::mat4(1.0f);

    light.lightDir     = glm::vec4(0.5f, -1.0f, 0.3f, 0.0f);
    light.lightColor   = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    light.ambientColor = glm::vec4(0.15f, 0.15f, 0.15f, 0.0f);
    light.cameraPos    = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    for (u32 i = 0; i < staging.count; ++i) {
        const UniformEntry& e = staging.entries[i];
        if (e.nameHash == HASH_U_MODEL && e.type == UniformType::MAT4) {
            scene.model = e.data.mat4Val;
        } else if (e.nameHash == HASH_U_VIEW && e.type == UniformType::MAT4) {
            scene.view = e.data.mat4Val;
        } else if (e.nameHash == HASH_U_PROJECTION && e.type == UniformType::MAT4) {
            scene.projection = e.data.mat4Val;
        } else if (e.nameHash == HASH_U_NORMAL_MATRIX && e.type == UniformType::MAT4) {
            scene.normalMatrix = e.data.mat4Val;
        } else if (e.nameHash == HASH_U_NORMAL_MATRIX && e.type == UniformType::MAT3) {
            // Pad mat3 into mat4 for std140
            const glm::mat3& m3 = e.data.mat3Val;
            scene.normalMatrix = glm::mat4(1.0f);
            scene.normalMatrix[0] = glm::vec4(m3[0], 0.0f);
            scene.normalMatrix[1] = glm::vec4(m3[1], 0.0f);
            scene.normalMatrix[2] = glm::vec4(m3[2], 0.0f);
        } else if (e.nameHash == HASH_U_LIGHT_DIR && e.type == UniformType::VEC3) {
            light.lightDir = glm::vec4(e.data.vec3Val, 0.0f);
        } else if (e.nameHash == HASH_U_LIGHT_COLOR && e.type == UniformType::VEC3) {
            light.lightColor = glm::vec4(e.data.vec3Val, 0.0f);
        } else if (e.nameHash == HASH_U_AMBIENT_COLOR && e.type == UniformType::VEC3) {
            light.ambientColor = glm::vec4(e.data.vec3Val, 0.0f);
        } else if (e.nameHash == HASH_U_CAMERA_POS && e.type == UniformType::VEC3) {
            light.cameraPos = glm::vec4(e.data.vec3Val, 0.0f);
        }
    }
}

// ==================== RHI Implementation ====================

RhiResult init(const RhiConfig& config) {
    s_headless = config.headless;
    s_viewportWidth  = config.viewportWidth;
    s_viewportHeight = config.viewportHeight;
    s_preferredSwapImages = config.preferredSwapImages;

    if (s_headless) {
        FFE_LOG_INFO("Renderer", "Headless mode (Vulkan backend) — no GPU operations");
        s_initialized = true;
        return RhiResult::OK;
    }

    if (config.window == nullptr) {
        FFE_LOG_ERROR("Renderer", "Vulkan backend requires a valid window pointer in RhiConfig");
        return RhiResult::ERROR_UNSUPPORTED;
    }

    s_vk.window = config.window;
    s_vk.vsync = config.vsync;

    // 1. Create Vulkan instance
    {
        const vk::VkInitResult result = vk::createVulkanInstance(s_vk, config.debugVulkan);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Vulkan instance creation failed: %s", result.errorMessage);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 2. Create surface (via GLFW)
    {
        const VkResult surfResult = glfwCreateWindowSurface(s_vk.instance, config.window, nullptr, &s_vk.surface);
        if (surfResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Renderer", "Failed to create Vulkan surface (VkResult %d)", static_cast<int>(surfResult));
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 3. Select physical device
    {
        const vk::VkInitResult result = vk::selectPhysicalDevice(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Physical device selection failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 4. Create logical device
    {
        const vk::VkInitResult result = vk::createLogicalDevice(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Logical device creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 5. Create VMA allocator
    {
        const vk::VkInitResult result = vk::createAllocator(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "VMA allocator creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 6. Create swap chain
    {
        const vk::VkInitResult result = vk::createSwapChain(s_vk, s_preferredSwapImages);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Swap chain creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 7. Create sync objects
    {
        const vk::VkInitResult result = vk::createSyncObjects(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Sync object creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 8. Create command pool and buffers
    {
        const vk::VkInitResult result = vk::createCommandBuffers(s_vk);
        if (!result.success) {
            FFE_LOG_ERROR("Renderer", "Command buffer creation failed: %s", result.errorMessage);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // 9. Create M3 textured quad demo resources
    {
        // --- Create 2x2 checkerboard texture (no file I/O) ---
        static constexpr u8 checkerPixels[] = {
            255, 255, 255, 255,   255,   0, 255, 255,
            255,   0, 255, 255,   255, 255, 255, 255,
        };
        s_quadTexture = vk::createTexture2D(
            s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
            checkerPixels, 2, 2, 4);
        if (s_quadTexture.image == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create checkerboard texture");
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Create uniform buffers (one per frame in flight) ---
        s_quadUniforms = vk::createUniformBuffers(s_vk.allocator);
        if (s_quadUniforms.buffers[0] == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create uniform buffers");
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Create descriptor pool ---
        s_vk.descriptorPool = vk::createDescriptorPool(
            s_vk.device, vk::MAX_DESCRIPTOR_SETS,
            vk::VulkanContext::MAX_FRAMES_IN_FLIGHT,
            vk::VulkanContext::MAX_FRAMES_IN_FLIGHT);
        if (s_vk.descriptorPool == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create descriptor pool");
            vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Create descriptor set layout ---
        s_vk.descriptorSetLayout = vk::createDescriptorSetLayout(
            s_vk.device, vk::DESCRIPTOR_UBO_BINDING, vk::DESCRIPTOR_SAMPLER_BINDING);
        if (s_vk.descriptorSetLayout == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create descriptor set layout");
            vkDestroyDescriptorPool(s_vk.device, s_vk.descriptorPool, nullptr);
            vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Allocate and update descriptor sets ---
        vk::allocateDescriptorSets(s_vk.device, s_vk.descriptorPool,
                                   s_vk.descriptorSetLayout,
                                   s_vk.descriptorSets,
                                   vk::VulkanContext::MAX_FRAMES_IN_FLIGHT);

        for (u32 i = 0; i < vk::VulkanContext::MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::updateDescriptorSetUBO(s_vk.device, s_vk.descriptorSets[i],
                                       vk::DESCRIPTOR_UBO_BINDING,
                                       s_quadUniforms.buffers[i],
                                       sizeof(vk::MVPUniform));
            vk::updateDescriptorSetTexture(s_vk.device, s_vk.descriptorSets[i],
                                           vk::DESCRIPTOR_SAMPLER_BINDING,
                                           s_quadTexture.imageView,
                                           s_quadTexture.sampler);
        }

        // --- Create shader modules from embedded SPIR-V ---
        s_quadShader = vk::createShaderModules(
            s_vk.device,
            vk::spv::TEXTURED_VERT_SPV, vk::spv::TEXTURED_VERT_SPV_SIZE,
            vk::spv::TEXTURED_FRAG_SPV, vk::spv::TEXTURED_FRAG_SPV_SIZE);
        if (s_quadShader.vertModule == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create textured quad shader modules");
            vkDestroyDescriptorSetLayout(s_vk.device, s_vk.descriptorSetLayout, nullptr);
            vkDestroyDescriptorPool(s_vk.device, s_vk.descriptorPool, nullptr);
            vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Create graphics pipeline with descriptor set layout ---
        vk::PipelineConfig pipeConfig{};
        pipeConfig.renderPass          = s_vk.clearPass;
        pipeConfig.extent              = s_vk.swapchainExtent;
        pipeConfig.vertexStride        = sizeof(TexturedVertex);
        pipeConfig.descriptorSetLayout = s_vk.descriptorSetLayout;
        pipeConfig.attrCount           = 2;

        // location 0: vec2 inPos
        pipeConfig.attrs[0].binding  = 0;
        pipeConfig.attrs[0].location = 0;
        pipeConfig.attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
        pipeConfig.attrs[0].offset   = 0;

        // location 1: vec2 inUV
        pipeConfig.attrs[1].binding  = 0;
        pipeConfig.attrs[1].location = 1;
        pipeConfig.attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
        pipeConfig.attrs[1].offset   = static_cast<u32>(offsetof(TexturedVertex, u));

        s_vk.trianglePipeline = vk::createGraphicsPipeline(
            s_vk.device, s_quadShader, pipeConfig, s_vk.trianglePipelineLayout);
        if (s_vk.trianglePipeline == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create textured quad graphics pipeline");
            vk::destroyShaderModules(s_vk.device, s_quadShader);
            vkDestroyDescriptorSetLayout(s_vk.device, s_vk.descriptorSetLayout, nullptr);
            vkDestroyDescriptorPool(s_vk.device, s_vk.descriptorPool, nullptr);
            vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Create vertex buffer ---
        s_quadVB = vk::createVertexBuffer(
            s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
            QUAD_VERTICES, sizeof(QUAD_VERTICES));
        if (s_quadVB.buffer == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create quad vertex buffer");
            vkDestroyPipeline(s_vk.device, s_vk.trianglePipeline, nullptr);
            vkDestroyPipelineLayout(s_vk.device, s_vk.trianglePipelineLayout, nullptr);
            vk::destroyShaderModules(s_vk.device, s_quadShader);
            vkDestroyDescriptorSetLayout(s_vk.device, s_vk.descriptorSetLayout, nullptr);
            vkDestroyDescriptorPool(s_vk.device, s_vk.descriptorPool, nullptr);
            vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }

        // --- Create index buffer ---
        s_quadIB = vk::createIndexBuffer(
            s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
            QUAD_INDICES, sizeof(QUAD_INDICES));
        if (s_quadIB.buffer == VK_NULL_HANDLE) {
            FFE_LOG_ERROR("Renderer", "Failed to create quad index buffer");
            vk::destroyBuffer(s_vk.allocator, s_quadVB);
            vkDestroyPipeline(s_vk.device, s_vk.trianglePipeline, nullptr);
            vkDestroyPipelineLayout(s_vk.device, s_vk.trianglePipelineLayout, nullptr);
            vk::destroyShaderModules(s_vk.device, s_quadShader);
            vkDestroyDescriptorSetLayout(s_vk.device, s_vk.descriptorSetLayout, nullptr);
            vkDestroyDescriptorPool(s_vk.device, s_vk.descriptorPool, nullptr);
            vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
            vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);
            vk::shutdownVulkan(s_vk);
            return RhiResult::ERROR_UNSUPPORTED;
        }
    }

    // Initialize default viewport
    s_viewport.x        = 0.0f;
    s_viewport.y        = 0.0f;
    s_viewport.width    = static_cast<f32>(s_vk.swapchainExtent.width);
    s_viewport.height   = static_cast<f32>(s_vk.swapchainExtent.height);
    s_viewport.minDepth = 0.0f;
    s_viewport.maxDepth = 1.0f;

    s_scissor.offset = {0, 0};
    s_scissor.extent = s_vk.swapchainExtent;

    s_initialized = true;
    FFE_LOG_INFO("Renderer", "RHI initialized (Vulkan backend, %ux%u, M4 mesh rendering)",
                 s_vk.swapchainExtent.width, s_vk.swapchainExtent.height);
    return RhiResult::OK;
}

void shutdown() {
    if (!s_initialized) return;

    if (!s_headless) {
        // Wait for GPU to finish before destroying resources
        if (s_vk.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(s_vk.device);
        }

        // Destroy M4 resource-managed objects
        s_resources.destroyAll(s_vk.allocator, s_vk.device);

        // Destroy M3 textured quad demo resources
        vk::destroyBuffer(s_vk.allocator, s_quadIB);
        vk::destroyBuffer(s_vk.allocator, s_quadVB);

        if (s_vk.trianglePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(s_vk.device, s_vk.trianglePipeline, nullptr);
            s_vk.trianglePipeline = VK_NULL_HANDLE;
        }
        if (s_vk.trianglePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(s_vk.device, s_vk.trianglePipelineLayout, nullptr);
            s_vk.trianglePipelineLayout = VK_NULL_HANDLE;
        }
        vk::destroyShaderModules(s_vk.device, s_quadShader);

        if (s_vk.descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(s_vk.device, s_vk.descriptorSetLayout, nullptr);
            s_vk.descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (s_vk.descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(s_vk.device, s_vk.descriptorPool, nullptr);
            s_vk.descriptorPool = VK_NULL_HANDLE;
        }

        vk::destroyUniformBuffers(s_vk.allocator, s_quadUniforms);
        vk::destroyTexture(s_vk.allocator, s_vk.device, s_quadTexture);

        vk::shutdownVulkan(s_vk);
        s_vk = vk::VulkanContext{};
    }

    // Reset all state
    s_resources = vk::ResourceManager{};
    s_uniforms.clear();
    s_currentShader = ShaderHandle{};
    for (auto& t : s_boundTextures) t = TextureHandle{};
    s_totalTextureVram = 0;
    s_headlessBufferNext = 1;
    s_headlessTextureNext = 1;
    s_headlessShaderNext = 1;
    s_viewportWidth  = 0;
    s_viewportHeight = 0;
    s_initialized = false;

    FFE_LOG_INFO("Renderer", "RHI shutdown complete");
}

// ============================================================================
// Buffers (M4)
// ============================================================================

BufferHandle createBuffer(const BufferDesc& desc) {
    if (s_headless) {
        return BufferHandle{s_headlessBufferNext++};
    }

    const u32 slot = s_resources.allocBuffer();
    if (slot == 0) {
        FFE_LOG_ERROR("Renderer", "Vulkan buffer pool exhausted (max %u)", vk::MAX_RHI_BUFFERS);
        return BufferHandle{0};
    }

    vk::BufferSlot& bs = s_resources.buffers[slot];
    bs.sizeBytes = desc.sizeBytes;

    const bool isDynamic = (desc.usage == BufferUsage::DYNAMIC || desc.usage == BufferUsage::STREAM);

    if (isDynamic) {
        // Host-visible buffer for frequent updates
        VkBufferUsageFlags vkUsage = 0;
        switch (desc.type) {
            case BufferType::VERTEX:  vkUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
            case BufferType::INDEX:   vkUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;  break;
            case BufferType::UNIFORM: vkUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
        }

        bs.buffer = createHostVisibleBuffer(s_vk.allocator, desc.sizeBytes, vkUsage, bs.mappedPtr);
        bs.hostVisible = true;

        if (bs.buffer.buffer == VK_NULL_HANDLE) {
            s_resources.freeBuffer(slot);
            return BufferHandle{0};
        }

        // Copy initial data if provided
        if (desc.data != nullptr && bs.mappedPtr != nullptr) {
            std::memcpy(bs.mappedPtr, desc.data, desc.sizeBytes);
        }
    } else {
        // Device-local buffer via staging upload (STATIC)
        if (desc.data != nullptr && desc.sizeBytes > 0) {
            switch (desc.type) {
                case BufferType::VERTEX:
                    bs.buffer = vk::createVertexBuffer(
                        s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
                        desc.data, desc.sizeBytes);
                    break;
                case BufferType::INDEX:
                    bs.buffer = vk::createIndexBuffer(
                        s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
                        desc.data, desc.sizeBytes);
                    break;
                case BufferType::UNIFORM:
                    // For uniform buffers, always use host-visible
                    bs.buffer = createHostVisibleBuffer(s_vk.allocator, desc.sizeBytes,
                                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bs.mappedPtr);
                    bs.hostVisible = true;
                    if (desc.data != nullptr && bs.mappedPtr != nullptr) {
                        std::memcpy(bs.mappedPtr, desc.data, desc.sizeBytes);
                    }
                    break;
            }
        }

        if (bs.buffer.buffer == VK_NULL_HANDLE) {
            s_resources.freeBuffer(slot);
            return BufferHandle{0};
        }
    }

    return BufferHandle{slot};
}

RhiResult updateBuffer(const BufferHandle handle, const void* data, const u32 sizeBytes, const u32 offset) {
    if (s_headless) return RhiResult::OK;
    if (data == nullptr) return RhiResult::ERROR_INVALID_HANDLE;
    if (handle.id == 0 || handle.id >= vk::MAX_RHI_BUFFERS) return RhiResult::ERROR_INVALID_HANDLE;

    vk::BufferSlot& bs = s_resources.buffers[handle.id];
    if (!bs.active) return RhiResult::ERROR_INVALID_HANDLE;

    if (bs.hostVisible && bs.mappedPtr != nullptr) {
        // Direct memcpy to persistently mapped memory
        auto* const dst = static_cast<u8*>(bs.mappedPtr) + offset;
        std::memcpy(dst, data, sizeBytes);
        return RhiResult::OK;
    }

    // For device-local buffers, would need staging upload.
    // For M4 this is not expected in the hot path — log a warning.
    FFE_LOG_WARN("Renderer", "updateBuffer: staging upload for device-local buffer not yet implemented");
    return RhiResult::ERROR_UNSUPPORTED;
}

void destroyBuffer(const BufferHandle handle) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= vk::MAX_RHI_BUFFERS) return;

    vk::BufferSlot& bs = s_resources.buffers[handle.id];
    if (!bs.active) return;

    if (bs.buffer.buffer != VK_NULL_HANDLE) {
        vk::destroyBuffer(s_vk.allocator, bs.buffer);
    }

    s_resources.freeBuffer(handle.id);
}

// ============================================================================
// Textures (M4)
// ============================================================================

TextureHandle createTexture(const TextureDesc& desc) {
    if (s_headless) {
        return TextureHandle{s_headlessTextureNext++};
    }

    const u32 slot = s_resources.allocTexture();
    if (slot == 0) {
        FFE_LOG_ERROR("Renderer", "Vulkan texture pool exhausted (max %u)", vk::MAX_RHI_TEXTURES);
        return TextureHandle{0};
    }

    vk::TextureSlot& ts = s_resources.textures[slot];

    // Determine channel count from format
    u32 channels = 4;
    switch (desc.format) {
        case TextureFormat::RGBA8:   channels = 4; break;
        case TextureFormat::RGB8:    channels = 4; break; // We upload as RGBA anyway
        case TextureFormat::R8:      channels = 4; break; // Expand to RGBA for Vulkan
        case TextureFormat::RGBA16F: channels = 4; break;
    }

    if (desc.pixelData != nullptr && desc.width > 0 && desc.height > 0) {
        ts.texture = vk::createTexture2D(
            s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
            desc.pixelData, desc.width, desc.height, channels);

        if (ts.texture.image == VK_NULL_HANDLE) {
            s_resources.freeTexture(slot);
            return TextureHandle{0};
        }

        ts.vramBytes = desc.width * desc.height * channels;
        s_totalTextureVram += ts.vramBytes;
    } else {
        // Texture with no pixel data — create a 1x1 white placeholder
        static constexpr u8 whitePixel[] = {255, 255, 255, 255};
        ts.texture = vk::createTexture2D(
            s_vk.allocator, s_vk.device, s_vk.commandPool, s_vk.graphicsQueue,
            whitePixel, 1, 1, 4);

        if (ts.texture.image == VK_NULL_HANDLE) {
            s_resources.freeTexture(slot);
            return TextureHandle{0};
        }
        ts.vramBytes = 4;
        s_totalTextureVram += 4;
    }

    return TextureHandle{slot};
}

void bindTexture(const TextureHandle handle, const u32 unitIndex) {
    if (s_headless) return;
    if (unitIndex < 4) {
        s_boundTextures[unitIndex] = handle;
    }
}

void destroyTexture(const TextureHandle handle) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= vk::MAX_RHI_TEXTURES) return;

    vk::TextureSlot& ts = s_resources.textures[handle.id];
    if (!ts.active) return;

    if (ts.vramBytes <= s_totalTextureVram) {
        s_totalTextureVram -= ts.vramBytes;
    }

    vk::destroyTexture(s_vk.allocator, s_vk.device, ts.texture);
    s_resources.freeTexture(handle.id);
}

u32 textureVramUsed() {
    return s_totalTextureVram;
}

u32 getTextureWidth(const TextureHandle handle) {
    if (s_headless) return 0;
    if (handle.id == 0 || handle.id >= vk::MAX_RHI_TEXTURES) return 0;
    const vk::TextureSlot& ts = s_resources.textures[handle.id];
    if (!ts.active) return 0;
    return ts.texture.width;
}

u32 getTextureHeight(const TextureHandle handle) {
    if (s_headless) return 0;
    if (handle.id == 0 || handle.id >= vk::MAX_RHI_TEXTURES) return 0;
    const vk::TextureSlot& ts = s_resources.textures[handle.id];
    if (!ts.active) return 0;
    return ts.texture.height;
}

void updateTextureSubImage(const TextureHandle handle, const u32 x, const u32 y,
                           const u32 width, const u32 height, const void* pixelData) {
    if (s_headless) return;
    // TODO(M5): Implement texture sub-image update for Vulkan
    (void)handle; (void)x; (void)y; (void)width; (void)height; (void)pixelData;
}

bool readTexturePixels(const TextureHandle handle, void* outBuffer, const u32 bufferSize) {
    // TODO(M5): Implement texture readback for Vulkan
    (void)handle; (void)outBuffer; (void)bufferSize;
    return false;
}

// ============================================================================
// Shaders (M4)
// ============================================================================

ShaderHandle createShader(const ShaderDesc& desc) {
    if (desc.vertexSource == nullptr || desc.fragmentSource == nullptr) {
        FFE_LOG_ERROR("Renderer", "createShader called with null source");
        return ShaderHandle{0};
    }
    if (s_headless) {
        return ShaderHandle{s_headlessShaderNext++};
    }

    const u32 slot = s_resources.allocShader();
    if (slot == 0) {
        FFE_LOG_ERROR("Renderer", "Vulkan shader pool exhausted (max %u)", vk::MAX_RHI_SHADERS);
        return ShaderHandle{0};
    }

    vk::ShaderSlot& ss = s_resources.shaders[slot];

    // For Vulkan, we cannot compile GLSL at runtime.
    // For M4, all shaders use the textured quad SPIR-V and 2D vertex layout.
    // The Blinn-Phong SPIR-V headers exist as documentation for the UBO layout
    // and will be used when build-time glslc compilation is added (M5).
    //
    // Mesh rendering through the Vulkan RHI requires valid SPIR-V matching the
    // MeshVertex layout (vec3 pos + vec3 normal + vec2 uv). The hand-assembled
    // SPIR-V in blinn_phong_vert.h/blinn_phong_frag.h documents the target
    // shader but needs build-time compilation for correctness.
    const u32* const vertSpv  = vk::spv::TEXTURED_VERT_SPV;
    const u32  vertSize      = vk::spv::TEXTURED_VERT_SPV_SIZE;
    const u32* const fragSpv = vk::spv::TEXTURED_FRAG_SPV;
    const u32  fragSize      = vk::spv::TEXTURED_FRAG_SPV_SIZE;

    if (desc.debugName != nullptr) {
        FFE_LOG_INFO("Renderer", "createShader: '%s' using textured pipeline (M4 — mesh SPIR-V pending M5)",
                     desc.debugName);
    }

    ss.shader = vk::createShaderModules(s_vk.device, vertSpv, vertSize, fragSpv, fragSize);
    if (ss.shader.vertModule == VK_NULL_HANDLE) {
        FFE_LOG_ERROR("Renderer", "createShader: SPIR-V module creation failed");
        s_resources.freeShader(slot);
        return ShaderHandle{0};
    }

    // For M4, all shaders use the simple UBO + sampler layout (binding 0 = UBO, binding 1 = sampler).
    // Mesh shader layout (SceneUBO + LightUBO + sampler) will be added in M5
    // when build-time SPIR-V compilation produces valid Blinn-Phong shaders.
    ss.descriptorSetLayout = vk::createDescriptorSetLayout(
        s_vk.device, vk::DESCRIPTOR_UBO_BINDING, vk::DESCRIPTOR_SAMPLER_BINDING);
    if (ss.descriptorSetLayout == VK_NULL_HANDLE) {
        vk::destroyShaderModules(s_vk.device, ss.shader);
        s_resources.freeShader(slot);
        return ShaderHandle{0};
    }

    constexpr u32 framesInFlight = vk::ShaderSlot::MAX_FRAMES;
    ss.descriptorPool = vk::createDescriptorPool(s_vk.device, framesInFlight,
                                                 framesInFlight, framesInFlight);
    if (ss.descriptorPool == VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(s_vk.device, ss.descriptorSetLayout, nullptr);
        vk::destroyShaderModules(s_vk.device, ss.shader);
        s_resources.freeShader(slot);
        return ShaderHandle{0};
    }

    vk::allocateDescriptorSets(s_vk.device, ss.descriptorPool, ss.descriptorSetLayout,
                               ss.descriptorSets, framesInFlight);

    // Create per-frame UBOs (SceneUBO for uniform staging, even in textured mode)
    for (u32 f = 0; f < framesInFlight; ++f) {
        if (!createMappedUniformBuffer(s_vk.allocator, sizeof(SceneUBO),
                                       ss.sceneUboBuffers[f], ss.sceneUboAllocations[f],
                                       ss.sceneUboMapped[f])) {
            s_resources.freeShader(slot);
            return ShaderHandle{0};
        }
        vk::updateDescriptorSetUBO(s_vk.device, ss.descriptorSets[f],
                                   vk::DESCRIPTOR_UBO_BINDING,
                                   ss.sceneUboBuffers[f], sizeof(SceneUBO));
    }

    // Create pipeline with TexturedVertex layout
    vk::PipelineConfig pipeConfig{};
    pipeConfig.renderPass          = s_vk.clearPass;
    pipeConfig.extent              = s_vk.swapchainExtent;
    pipeConfig.vertexStride        = sizeof(TexturedVertex);
    pipeConfig.descriptorSetLayout = ss.descriptorSetLayout;
    pipeConfig.attrCount           = 2;

    // TexturedVertex layout: position(vec2) + texcoord(vec2) = 16 bytes
    pipeConfig.attrs[0].binding  = 0;
    pipeConfig.attrs[0].location = 0;
    pipeConfig.attrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    pipeConfig.attrs[0].offset   = 0;

    pipeConfig.attrs[1].binding  = 0;
    pipeConfig.attrs[1].location = 1;
    pipeConfig.attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
    pipeConfig.attrs[1].offset   = 8;

    ss.pipeline = vk::createGraphicsPipeline(s_vk.device, ss.shader, pipeConfig, ss.pipelineLayout);
    if (ss.pipeline == VK_NULL_HANDLE) {
        FFE_LOG_ERROR("Renderer", "createShader: pipeline creation failed");
        s_resources.freeShader(slot);
        return ShaderHandle{0};
    }

    FFE_LOG_INFO("Renderer", "Vulkan shader created (slot %u, %s)", slot,
                 desc.debugName ? desc.debugName : "unnamed");
    return ShaderHandle{slot};
}

void bindShader(const ShaderHandle handle) {
    if (s_headless) return;
    s_currentShader = handle;
}

void destroyShader(const ShaderHandle handle) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= vk::MAX_RHI_SHADERS) return;

    vk::ShaderSlot& ss = s_resources.shaders[handle.id];
    if (!ss.active) return;

    // Wait for GPU idle before destroying shader resources
    if (s_vk.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(s_vk.device);
    }

    // Destroy UBOs
    for (u32 f = 0; f < vk::ShaderSlot::MAX_FRAMES; ++f) {
        if (ss.sceneUboBuffers[f] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(s_vk.allocator, ss.sceneUboBuffers[f], ss.sceneUboAllocations[f]);
        }
        if (ss.lightUboBuffers[f] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(s_vk.allocator, ss.lightUboBuffers[f], ss.lightUboAllocations[f]);
        }
    }

    if (ss.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(s_vk.device, ss.pipeline, nullptr);
    }
    if (ss.pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(s_vk.device, ss.pipelineLayout, nullptr);
    }
    if (ss.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(s_vk.device, ss.descriptorSetLayout, nullptr);
    }
    if (ss.descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(s_vk.device, ss.descriptorPool, nullptr);
    }
    vk::destroyShaderModules(s_vk.device, ss.shader);

    s_resources.freeShader(handle.id);

    // Clear current shader if it was the one destroyed
    if (s_currentShader.id == handle.id) {
        s_currentShader = ShaderHandle{};
    }
}

// ============================================================================
// Uniforms (M4)
// ============================================================================
// Uniforms are staged in CPU memory. At draw time, they are packed into UBOs
// and uploaded via persistently-mapped memory.

void setUniformInt(const ShaderHandle handle, const char* name, const i32 value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::INT, &value, sizeof(value));
}

void setUniformFloat(const ShaderHandle handle, const char* name, const f32 value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::FLOAT, &value, sizeof(value));
}

void setUniformVec2(const ShaderHandle handle, const char* name, const glm::vec2& value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::VEC2, &value, sizeof(value));
}

void setUniformVec3(const ShaderHandle handle, const char* name, const glm::vec3& value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::VEC3, &value, sizeof(value));
}

void setUniformVec4(const ShaderHandle handle, const char* name, const glm::vec4& value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::VEC4, &value, sizeof(value));
}

void setUniformMat3(const ShaderHandle handle, const char* name, const glm::mat3& value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::MAT3, &value, sizeof(value));
}

void setUniformMat4(const ShaderHandle handle, const char* name, const glm::mat4& value) {
    if (s_headless) return;
    (void)handle;
    const u32 hash = fnvHash(name);
    s_uniforms.set(hash, UniformType::MAT4, &value, sizeof(value));
}

void setUniformMat4Array(const ShaderHandle handle, const char* name, const glm::mat4* values, const u32 count) {
    if (s_headless) return;
    // TODO(M5): Implement mat4 array uniforms for Vulkan (needed for skeletal animation)
    (void)handle; (void)name; (void)values; (void)count;
}

// ============================================================================
// Pipeline state (M4)
// ============================================================================

void applyPipelineState(const PipelineState& state) {
    if (s_headless) return;
    // In Vulkan, pipeline state changes require different pipeline objects.
    // For M5, we track the depth test state. Full pipeline variants (blend modes,
    // per-shader depth mode permutations) will be added in a future milestone.
    // The default pipeline has depth test enabled via PipelineConfig defaults.
    (void)state;
}

// ============================================================================
// Frame operations
// ============================================================================

void beginFrame(const glm::vec4& clearColor) {
    if (s_headless) return;

    ZoneScopedN("RHI::beginFrame");

    // Skip rendering when framebuffer is zero-size (window minimised)
    if (s_vk.swapchainExtent.width == 0 || s_vk.swapchainExtent.height == 0) {
        s_vk.imageAcquired = false;
        return;
    }

    const u32 frame = s_vk.currentFrame;

    // Wait for this frame's fence
    vkWaitForFences(s_vk.device, 1, &s_vk.inFlightFences[frame], VK_TRUE, UINT64_MAX);

    // Acquire next swap chain image
    const VkResult acquireResult = vkAcquireNextImageKHR(
        s_vk.device, s_vk.swapchain, UINT64_MAX,
        s_vk.imageAvailable[frame], VK_NULL_HANDLE,
        &s_vk.acquiredImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        const vk::VkInitResult recreateResult = vk::recreateSwapChain(s_vk, s_preferredSwapImages);
        if (!recreateResult.success) {
            FFE_LOG_ERROR("Renderer", "Swap chain recreation failed: %s", recreateResult.errorMessage);
            s_vk.imageAcquired = false;
            return;
        }
        s_vk.imageAcquired = false;
        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        FFE_LOG_ERROR("Renderer", "vkAcquireNextImageKHR failed with VkResult %d",
                      static_cast<int>(acquireResult));
        s_vk.imageAcquired = false;
        return;
    }

    if (s_vk.acquiredImageIndex >= s_vk.swapImageCount) {
        FFE_LOG_ERROR("Renderer", "Acquired image index %u out of bounds (swapImageCount=%u)",
                      s_vk.acquiredImageIndex, s_vk.swapImageCount);
        s_vk.imageAcquired = false;
        return;
    }

    vkResetFences(s_vk.device, 1, &s_vk.inFlightFences[frame]);
    vkResetCommandBuffer(s_vk.commandBuffers[frame], 0);

    // --- Update M3 demo MVP uniform for this frame ---
    {
        vk::MVPUniform mvp{};
        mvp.model = glm::mat4(1.0f);
        mvp.view  = glm::mat4(1.0f);

        const f32 aspect = static_cast<f32>(s_vk.swapchainExtent.width) /
                           static_cast<f32>(s_vk.swapchainExtent.height);
        mvp.proj = glm::ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);
        mvp.proj[1][1] *= -1.0f;

        vk::updateUniform(s_quadUniforms, frame, mvp);
    }

    // Clear uniform staging for this frame
    s_uniforms.clear();

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    {
        const VkResult beginResult = vkBeginCommandBuffer(s_vk.commandBuffers[frame], &beginInfo);
        if (beginResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "vkBeginCommandBuffer failed with VkResult %d", static_cast<int>(beginResult));
            s_vk.imageAcquired = false;
            return;
        }
    }

    // Begin render pass with clear color + depth (M5)
    VkClearValue clearValues[2]{};
    clearValues[0].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = s_vk.clearPass;
    rpBegin.framebuffer       = s_vk.swapFramebuffers[s_vk.acquiredImageIndex];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = s_vk.swapchainExtent;
    rpBegin.clearValueCount   = 2;
    rpBegin.pClearValues      = clearValues;

    vkCmdBeginRenderPass(s_vk.commandBuffers[frame], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // M3: draw the textured quad demo
    if (s_vk.trianglePipeline != VK_NULL_HANDLE &&
        s_quadVB.buffer != VK_NULL_HANDLE &&
        s_quadIB.buffer != VK_NULL_HANDLE) {
        const VkCommandBuffer cmd = s_vk.commandBuffers[frame];

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_vk.trianglePipeline);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                s_vk.trianglePipelineLayout, 0, 1,
                                &s_vk.descriptorSets[frame], 0, nullptr);

        VkViewport vp{};
        vp.x        = 0.0f;
        vp.y        = 0.0f;
        vp.width    = static_cast<f32>(s_vk.swapchainExtent.width);
        vp.height   = static_cast<f32>(s_vk.swapchainExtent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D sc{};
        sc.offset = {0, 0};
        sc.extent = s_vk.swapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &sc);

        const VkBuffer vertexBuffers[] = {s_quadVB.buffer};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, s_quadIB.buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    }

    // Update default viewport/scissor for M4 draw calls
    s_viewport.width  = static_cast<f32>(s_vk.swapchainExtent.width);
    s_viewport.height = static_cast<f32>(s_vk.swapchainExtent.height);
    s_scissor.extent  = s_vk.swapchainExtent;

    // Do NOT end render pass here — M4 draw calls will record into it.
    // The render pass is ended in endFrame() after all draw calls.
    // However, we need to keep the M3 demo working. The M3 demo's render pass
    // is already begun above. Additional M4 draw calls will be appended
    // within the same render pass.

    s_vk.imageAcquired = true;
}

void endFrame(GLFWwindow* window) {
    if (s_headless) return;
    if (window == nullptr) return;
    if (!s_vk.imageAcquired) return;

    ZoneScopedN("RHI::endFrame");

    const u32 frame = s_vk.currentFrame;

    // End the render pass that was begun in beginFrame
    vkCmdEndRenderPass(s_vk.commandBuffers[frame]);

    {
        const VkResult endResult = vkEndCommandBuffer(s_vk.commandBuffers[frame]);
        if (endResult != VK_SUCCESS) {
            FFE_LOG_ERROR("Vulkan", "vkEndCommandBuffer failed with VkResult %d", static_cast<int>(endResult));
            s_vk.imageAcquired = false;
            return;
        }
    }

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    const VkSemaphore waitSemaphores[] = {s_vk.imageAvailable[frame]};
    const VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores    = waitSemaphores;
    submitInfo.pWaitDstStageMask  = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &s_vk.commandBuffers[frame];

    const VkSemaphore signalSemaphores[] = {s_vk.renderFinished[frame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    VK_CHECK_VOID(vkQueueSubmit(s_vk.graphicsQueue, 1, &submitInfo, s_vk.inFlightFences[frame]));

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;

    const VkSwapchainKHR swapchains[] = {s_vk.swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains    = swapchains;
    presentInfo.pImageIndices  = &s_vk.acquiredImageIndex;

    const VkResult presentResult = vkQueuePresentKHR(s_vk.presentQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        const vk::VkInitResult recreateResult = vk::recreateSwapChain(s_vk, s_preferredSwapImages);
        if (!recreateResult.success) {
            FFE_LOG_ERROR("Renderer", "Swap chain recreation failed during present: %s",
                          recreateResult.errorMessage);
        }
    } else if (presentResult != VK_SUCCESS) {
        FFE_LOG_ERROR("Renderer", "vkQueuePresentKHR failed with VkResult %d",
                      static_cast<int>(presentResult));
    }

    s_vk.currentFrame = (s_vk.currentFrame + 1) % vk::VulkanContext::MAX_FRAMES_IN_FLIGHT;
    s_vk.imageAcquired = false;
}

// ============================================================================
// Viewport / Scissor
// ============================================================================

void setViewport(const i32 x, const i32 y, const i32 width, const i32 height) {
    if (s_headless) return;
    s_viewport.x      = static_cast<f32>(x);
    s_viewport.y      = static_cast<f32>(y);
    s_viewport.width  = static_cast<f32>(width);
    s_viewport.height = static_cast<f32>(height);
}

void setScissor(const i32 x, const i32 y, const i32 width, const i32 height) {
    if (s_headless) return;
    s_scissor.offset = {x, y};
    s_scissor.extent = {static_cast<u32>(width), static_cast<u32>(height)};
}

void setViewProjection(const glm::mat4& vp) {
    s_viewProjection = vp;
}

// ============================================================================
// Draw calls (M4)
// ============================================================================

void drawArrays(const BufferHandle vertexBuffer, const u32 vertexCount, const u32 vertexOffset) {
    if (s_headless) return;
    if (!s_vk.imageAcquired) return;
    if (vertexBuffer.id == 0 || vertexBuffer.id >= vk::MAX_RHI_BUFFERS) return;
    if (!s_resources.buffers[vertexBuffer.id].active) return;
    if (s_currentShader.id == 0 || s_currentShader.id >= vk::MAX_RHI_SHADERS) return;

    const vk::ShaderSlot& ss = s_resources.shaders[s_currentShader.id];
    if (!ss.active || ss.pipeline == VK_NULL_HANDLE) return;

    const u32 frame = s_vk.currentFrame;
    const VkCommandBuffer cmd = s_vk.commandBuffers[frame];

    // Upload uniforms to UBO
    if (ss.sceneUboMapped[frame] != nullptr) {
        SceneUBO scene{};
        LightUBO light{};
        packUniforms(s_uniforms, scene, light);
        std::memcpy(ss.sceneUboMapped[frame], &scene, sizeof(SceneUBO));
        if (ss.lightUboMapped[frame] != nullptr) {
            std::memcpy(ss.lightUboMapped[frame], &light, sizeof(LightUBO));
        }
    }

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ss.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ss.pipelineLayout, 0, 1,
                            &ss.descriptorSets[frame], 0, nullptr);

    // Set dynamic viewport and scissor
    vkCmdSetViewport(cmd, 0, 1, &s_viewport);
    vkCmdSetScissor(cmd, 0, 1, &s_scissor);

    // Bind vertex buffer
    const vk::BufferSlot& vbs = s_resources.buffers[vertexBuffer.id];
    const VkBuffer vbuffers[] = {vbs.buffer.buffer};
    const VkDeviceSize voffsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbuffers, voffsets);

    // Draw
    vkCmdDraw(cmd, vertexCount, 1, vertexOffset, 0);
}

void drawIndexed(const BufferHandle vertexBuffer, const BufferHandle indexBuffer,
                 const u32 indexCount, const u32 vertexOffset, const u32 indexOffset) {
    if (s_headless) return;
    if (!s_vk.imageAcquired) return;
    if (vertexBuffer.id == 0 || vertexBuffer.id >= vk::MAX_RHI_BUFFERS) return;
    if (indexBuffer.id == 0 || indexBuffer.id >= vk::MAX_RHI_BUFFERS) return;
    if (!s_resources.buffers[vertexBuffer.id].active) return;
    if (!s_resources.buffers[indexBuffer.id].active) return;
    if (s_currentShader.id == 0 || s_currentShader.id >= vk::MAX_RHI_SHADERS) return;

    const vk::ShaderSlot& ss = s_resources.shaders[s_currentShader.id];
    if (!ss.active || ss.pipeline == VK_NULL_HANDLE) return;

    const u32 frame = s_vk.currentFrame;
    const VkCommandBuffer cmd = s_vk.commandBuffers[frame];

    // Upload uniforms to UBOs
    if (ss.sceneUboMapped[frame] != nullptr) {
        SceneUBO scene{};
        LightUBO light{};
        packUniforms(s_uniforms, scene, light);
        std::memcpy(ss.sceneUboMapped[frame], &scene, sizeof(SceneUBO));
        if (ss.lightUboMapped[frame] != nullptr) {
            std::memcpy(ss.lightUboMapped[frame], &light, sizeof(LightUBO));
        }
    }

    // Update texture descriptor if a texture is bound to unit 0
    if (isValid(s_boundTextures[0])) {
        const u32 tid = s_boundTextures[0].id;
        if (tid < vk::MAX_RHI_TEXTURES && s_resources.textures[tid].active) {
            const vk::VkManagedTexture& tex = s_resources.textures[tid].texture;
            if (tex.imageView != VK_NULL_HANDLE && tex.sampler != VK_NULL_HANDLE) {
                // Determine sampler binding index (2 for mesh shaders, 1 for others)
                const u32 samplerBinding = (ss.lightUboMapped[frame] != nullptr) ? 2 : 1;
                vk::updateDescriptorSetTexture(s_vk.device, ss.descriptorSets[frame],
                                               samplerBinding, tex.imageView, tex.sampler);
            }
        }
    }

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ss.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ss.pipelineLayout, 0, 1,
                            &ss.descriptorSets[frame], 0, nullptr);

    // Set dynamic viewport and scissor
    vkCmdSetViewport(cmd, 0, 1, &s_viewport);
    vkCmdSetScissor(cmd, 0, 1, &s_scissor);

    // Bind vertex buffer
    const vk::BufferSlot& vbs = s_resources.buffers[vertexBuffer.id];
    const VkBuffer vbuffers[] = {vbs.buffer.buffer};
    const VkDeviceSize voffsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vbuffers, voffsets);

    // Bind index buffer (u32 indices — meshes use 32-bit)
    const vk::BufferSlot& ibs = s_resources.buffers[indexBuffer.id];
    vkCmdBindIndexBuffer(cmd, ibs.buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw
    vkCmdDrawIndexed(cmd, indexCount, 1, indexOffset, static_cast<i32>(vertexOffset), 0);
}

// ============================================================================
// Query functions
// ============================================================================

bool isHeadless() {
    return s_headless;
}

bool isSoftwareRenderer() {
    // TODO: Detect Vulkan software renderers (lavapipe, etc.) when Vulkan backend matures.
    return false;
}

i32 getViewportWidth() {
    return s_viewportWidth;
}

i32 getViewportHeight() {
    return s_viewportHeight;
}

void setViewportSize(const i32 width, const i32 height) {
    s_viewportWidth  = width;
    s_viewportHeight = height;
}

} // namespace ffe::rhi

#endif // FFE_BACKEND_VULKAN
