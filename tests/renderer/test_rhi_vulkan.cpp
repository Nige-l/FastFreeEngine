#include <catch2/catch_test_macros.hpp>
#include "renderer/rhi_types.h"
#include <type_traits>

// Vulkan-specific headers — only available when building the Vulkan backend
#ifdef FFE_BACKEND_VULKAN
#include "renderer/vulkan/vk_buffer.h"
#include "renderer/vulkan/vk_shader.h"
#include "renderer/vulkan/vk_pipeline.h"
#include "renderer/vulkan/vk_texture.h"
#include "renderer/vulkan/vk_descriptor.h"
#include "renderer/vulkan/vk_uniform.h"
#include "renderer/vulkan/shaders/triangle_vert.h"
#include "renderer/vulkan/shaders/triangle_frag.h"
#include "renderer/vulkan/shaders/textured_vert.h"
#include "renderer/vulkan/shaders/textured_frag.h"
#endif

using namespace ffe;
using namespace ffe::rhi;

// ============================================================================
// CPU-only tests — no Vulkan context needed, always compile
// ============================================================================

TEST_CASE("RhiBackend enum values", "[rhi][vulkan]") {
    REQUIRE(static_cast<u8>(RhiBackend::OPENGL) == 0);
    REQUIRE(static_cast<u8>(RhiBackend::VULKAN) == 1);
}

TEST_CASE("RhiConfig default values preserve backward compatibility", "[rhi][vulkan]") {
    const RhiConfig config{};

    // Existing defaults must be unchanged
    REQUIRE(config.viewportWidth  == 1280);
    REQUIRE(config.viewportHeight == 720);
    REQUIRE(config.headless       == false);
    REQUIRE(config.vsync          == true);
    REQUIRE(config.debugGL        == false);

    // New Vulkan fields default to safe values
    REQUIRE(config.backend        == RhiBackend::OPENGL);
    REQUIRE(config.debugVulkan    == false);
    REQUIRE(config.preferredSwapImages == 2);
    REQUIRE(config.window         == nullptr);
}

TEST_CASE("RhiConfig Vulkan-specific fields", "[rhi][vulkan]") {
    RhiConfig config{};
    config.backend             = RhiBackend::VULKAN;
    config.debugVulkan         = true;
    config.preferredSwapImages = 3;

    REQUIRE(config.backend             == RhiBackend::VULKAN);
    REQUIRE(config.debugVulkan         == true);
    REQUIRE(config.preferredSwapImages == 3);
}

TEST_CASE("MAX_SWAPCHAIN_IMAGES constant", "[rhi][vulkan]") {
    REQUIRE(MAX_SWAPCHAIN_IMAGES == 4);
    // Must be at least 2 for double-buffering
    REQUIRE(MAX_SWAPCHAIN_IMAGES >= 2);
}

TEST_CASE("Backend/tier compatibility validation", "[rhi][vulkan]") {
    // These are compile-time constraints enforced by CMake.
    // At runtime we can verify the enum combinations that would be valid.

    // RETRO and LEGACY only support OpenGL
    SECTION("RETRO tier requires OpenGL") {
        const auto tier = HardwareTier::RETRO;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE_FALSE(vulkanAllowed);
    }

    SECTION("LEGACY tier requires OpenGL") {
        const auto tier = HardwareTier::LEGACY;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE_FALSE(vulkanAllowed);
    }

    SECTION("STANDARD tier allows both backends") {
        const auto tier = HardwareTier::STANDARD;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE(vulkanAllowed);
    }

    SECTION("MODERN tier requires Vulkan") {
        const auto tier = HardwareTier::MODERN;
        const bool vulkanAllowed = (static_cast<u8>(tier) >= static_cast<u8>(HardwareTier::STANDARD));
        REQUIRE(vulkanAllowed);
        // MODERN must use Vulkan (enforced by CMake, validated here as a logic check)
        const bool openglAllowed = (static_cast<u8>(tier) < static_cast<u8>(HardwareTier::MODERN));
        REQUIRE_FALSE(openglAllowed);
    }
}

TEST_CASE("RhiConfig can be constructed with Vulkan settings", "[rhi][vulkan]") {
    // Verify that constructing a config with all Vulkan-related fields
    // does not break any existing fields
    RhiConfig config{};
    config.viewportWidth       = 800;
    config.viewportHeight      = 600;
    config.headless            = true;
    config.vsync               = false;
    config.debugGL             = false;
    config.debugVulkan         = true;
    config.backend             = RhiBackend::VULKAN;
    config.preferredSwapImages = 3;
    config.window              = nullptr;

    REQUIRE(config.viewportWidth  == 800);
    REQUIRE(config.viewportHeight == 600);
    REQUIRE(config.headless       == true);
    REQUIRE(config.vsync          == false);
    REQUIRE(config.debugGL        == false);
    REQUIRE(config.debugVulkan    == true);
    REQUIRE(config.backend        == RhiBackend::VULKAN);
    REQUIRE(config.preferredSwapImages == 3);
    REQUIRE(config.window         == nullptr);
}

// ============================================================================
// M2 — Vulkan Shader Compilation + Vertex Buffers (CPU-only)
// ============================================================================

#ifdef FFE_BACKEND_VULKAN

// --- Buffer Tests ---

TEST_CASE("VkManagedBuffer: default-initialized fields are null/zero", "[renderer][vulkan]") {
    const vk::VkManagedBuffer buf{};
    REQUIRE(buf.buffer     == VK_NULL_HANDLE);
    REQUIRE(buf.allocation == VK_NULL_HANDLE);
    REQUIRE(buf.size       == 0);
}

TEST_CASE("VkManagedBuffer: size field is VkDeviceSize (8 bytes)", "[renderer][vulkan]") {
    // VkDeviceSize is typedef'd to uint64_t — must be 8 bytes
    static_assert(sizeof(VkDeviceSize) == 8, "VkDeviceSize must be 8 bytes");
    const vk::VkManagedBuffer buf{};
    REQUIRE(sizeof(buf.size) == 8);
}

TEST_CASE("VkManagedBuffer: struct is trivially copyable", "[renderer][vulkan]") {
    static_assert(std::is_trivially_copyable_v<vk::VkManagedBuffer>,
                  "VkManagedBuffer must be trivially copyable for memcpy/DMA");
    REQUIRE(std::is_trivially_copyable_v<vk::VkManagedBuffer>);
}

// --- Shader Tests ---

TEST_CASE("VkManagedShader: default-initialized modules are null", "[renderer][vulkan]") {
    const vk::VkManagedShader shader{};
    REQUIRE(shader.vertModule == VK_NULL_HANDLE);
    REQUIRE(shader.fragModule == VK_NULL_HANDLE);
}

TEST_CASE("Embedded triangle vertex SPIR-V is non-empty", "[renderer][vulkan]") {
    REQUIRE(vk::spv::TRIANGLE_VERT_SPV_SIZE > 0);
    // SPIR-V must be a multiple of 4 bytes (each word is 32-bit)
    REQUIRE(vk::spv::TRIANGLE_VERT_SPV_SIZE % 4 == 0);
}

TEST_CASE("Embedded triangle fragment SPIR-V is non-empty", "[renderer][vulkan]") {
    REQUIRE(vk::spv::TRIANGLE_FRAG_SPV_SIZE > 0);
    // SPIR-V must be a multiple of 4 bytes (each word is 32-bit)
    REQUIRE(vk::spv::TRIANGLE_FRAG_SPV_SIZE % 4 == 0);
}

TEST_CASE("Embedded triangle vertex SPIR-V starts with magic number 0x07230203", "[renderer][vulkan]") {
    REQUIRE(vk::spv::TRIANGLE_VERT_SPV[0] == 0x07230203);
}

TEST_CASE("Embedded triangle fragment SPIR-V starts with magic number 0x07230203", "[renderer][vulkan]") {
    REQUIRE(vk::spv::TRIANGLE_FRAG_SPV[0] == 0x07230203);
}

// --- Pipeline Tests ---

TEST_CASE("PipelineConfig: MAX_ATTRS is 8", "[renderer][vulkan]") {
    REQUIRE(vk::PipelineConfig::MAX_ATTRS == 8);
}

TEST_CASE("PipelineConfig: default attrCount is 0", "[renderer][vulkan]") {
    const vk::PipelineConfig config{};
    REQUIRE(config.attrCount == 0);
}

TEST_CASE("PipelineConfig: default vertexStride is 0", "[renderer][vulkan]") {
    const vk::PipelineConfig config{};
    REQUIRE(config.vertexStride == 0);
}

TEST_CASE("PipelineConfig: default renderPass is VK_NULL_HANDLE", "[renderer][vulkan]") {
    const vk::PipelineConfig config{};
    REQUIRE(config.renderPass == VK_NULL_HANDLE);
}

// --- Triangle Vertex Data Tests ---
// The TriangleVertex struct is internal to rhi_vulkan.cpp, but we can verify
// the expected layout: position(vec2 = 8 bytes) + color(vec3 = 12 bytes) = 20 bytes.
// This matches the GLSL layout: layout(location=0) in vec2 inPos; layout(location=1) in vec3 inColor;

TEST_CASE("Triangle vertex stride matches expected layout", "[renderer][vulkan]") {
    // vec2 (position) = 2 * sizeof(float) = 8 bytes
    // vec3 (color)    = 3 * sizeof(float) = 12 bytes
    // Total stride    = 20 bytes
    constexpr u32 expectedStride = 2 * sizeof(f32) + 3 * sizeof(f32);
    REQUIRE(expectedStride == 20);
    // This value must match the vertexStride used in pipeline configuration
    // and the TriangleVertex struct defined in rhi_vulkan.cpp
}

// ============================================================================
// M3 — Vulkan Textures, Descriptors, Uniforms (CPU-only)
// ============================================================================

// --- Texture Tests ---

TEST_CASE("VkManagedTexture: default fields are null/zero", "[renderer][vulkan]") {
    const vk::VkManagedTexture tex{};
    REQUIRE(tex.image      == VK_NULL_HANDLE);
    REQUIRE(tex.imageView  == VK_NULL_HANDLE);
    REQUIRE(tex.sampler    == VK_NULL_HANDLE);
    REQUIRE(tex.allocation == VK_NULL_HANDLE);
    REQUIRE(tex.width  == 0);
    REQUIRE(tex.height == 0);
}

TEST_CASE("VkManagedTexture: struct stores width and height", "[renderer][vulkan]") {
    vk::VkManagedTexture tex{};
    tex.width  = 512;
    tex.height = 256;
    REQUIRE(tex.width  == 512);
    REQUIRE(tex.height == 256);
}

// --- Uniform Tests ---

TEST_CASE("MVPUniform: size is 192 bytes (3 * mat4)", "[renderer][vulkan]") {
    static_assert(sizeof(vk::MVPUniform) == 192, "MVPUniform must be 3 * mat4 = 192 bytes");
    REQUIRE(sizeof(vk::MVPUniform) == 192);
}

TEST_CASE("MVPUniform: identity matrices", "[renderer][vulkan]") {
    vk::MVPUniform mvp{};
    mvp.model = glm::mat4(1.0f);
    mvp.view  = glm::mat4(1.0f);
    mvp.proj  = glm::mat4(1.0f);

    REQUIRE(mvp.model[0][0] == 1.0f);
    REQUIRE(mvp.model[3][3] == 1.0f);
    REQUIRE(mvp.view[1][1]  == 1.0f);
    REQUIRE(mvp.proj[2][2]  == 1.0f);
}

TEST_CASE("VkManagedUniform: default arrays are null", "[renderer][vulkan]") {
    const vk::VkManagedUniform uniform{};
    for (u32 i = 0; i < vk::MAX_FRAMES_IN_FLIGHT; ++i) {
        REQUIRE(uniform.buffers[i]     == VK_NULL_HANDLE);
        REQUIRE(uniform.allocations[i] == VK_NULL_HANDLE);
        REQUIRE(uniform.mappedPtrs[i]  == nullptr);
    }
}

// --- Descriptor Tests ---

TEST_CASE("MAX_DESCRIPTOR_SETS is 16", "[renderer][vulkan]") {
    REQUIRE(vk::MAX_DESCRIPTOR_SETS == 16);
}

TEST_CASE("DescriptorSetLayout UBO binding is 0, sampler binding is 1", "[renderer][vulkan]") {
    REQUIRE(vk::DESCRIPTOR_UBO_BINDING     == 0);
    REQUIRE(vk::DESCRIPTOR_SAMPLER_BINDING == 1);
}

// --- Textured Shader SPIR-V Tests ---

TEST_CASE("Embedded textured vertex SPIR-V starts with magic number", "[renderer][vulkan]") {
    REQUIRE(vk::spv::TEXTURED_VERT_SPV[0] == 0x07230203);
    REQUIRE(vk::spv::TEXTURED_VERT_SPV_SIZE > 0);
    REQUIRE(vk::spv::TEXTURED_VERT_SPV_SIZE % 4 == 0);
}

TEST_CASE("Embedded textured fragment SPIR-V starts with magic number", "[renderer][vulkan]") {
    REQUIRE(vk::spv::TEXTURED_FRAG_SPV[0] == 0x07230203);
    REQUIRE(vk::spv::TEXTURED_FRAG_SPV_SIZE > 0);
    REQUIRE(vk::spv::TEXTURED_FRAG_SPV_SIZE % 4 == 0);
}

// --- Textured Quad Layout Tests ---

TEST_CASE("Textured quad vertex stride is 16 bytes", "[renderer][vulkan]") {
    // vec2 (position) = 2 * sizeof(float) = 8 bytes
    // vec2 (texcoord) = 2 * sizeof(float) = 8 bytes
    // Total stride    = 16 bytes
    constexpr u32 expectedStride = 2 * sizeof(f32) + 2 * sizeof(f32);
    REQUIRE(expectedStride == 16);
}

#endif // FFE_BACKEND_VULKAN
