// test_gpu_instancing.cpp -- Catch2 unit tests for GPU instancing infrastructure.
//
// All tests are CPU-only (no GL context required) -- they validate constants,
// data layout, instance buffer math, and the getInstanceCount helper.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/gpu_instancing.h"
#include "renderer/mesh_renderer.h"
#include "renderer/render_system.h"
#include "renderer/shader_library.h"
#include "core/types.h"
#include "core/ecs.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstring>

using namespace ffe;
using namespace ffe::renderer;

// ===========================================================================
// InstanceData struct layout
// ===========================================================================

TEST_CASE("InstanceData: size is exactly 64 bytes", "[renderer][instancing]") {
    CHECK(sizeof(InstanceData) == 64);
}

TEST_CASE("InstanceData: contains a single mat4", "[renderer][instancing]") {
    // InstanceData must be layout-compatible with glm::mat4 for direct GPU upload.
    CHECK(sizeof(InstanceData) == sizeof(glm::mat4));
}

TEST_CASE("InstanceData: modelMatrix field is accessible and assignable", "[renderer][instancing]") {
    InstanceData data{};
    data.modelMatrix = glm::mat4(1.0f);
    CHECK(data.modelMatrix[0][0] == 1.0f);
    CHECK(data.modelMatrix[1][1] == 1.0f);
    CHECK(data.modelMatrix[2][2] == 1.0f);
    CHECK(data.modelMatrix[3][3] == 1.0f);
    CHECK(data.modelMatrix[0][1] == 0.0f);
}

TEST_CASE("InstanceData: model matrix can be set and read back", "[renderer][instancing]") {
    InstanceData data{};
    const glm::mat4 translated = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 10.0f, -3.0f));
    data.modelMatrix = translated;
    CHECK(data.modelMatrix == translated);
}

// ===========================================================================
// Constants
// ===========================================================================

TEST_CASE("MAX_INSTANCES_PER_BATCH is 1024", "[renderer][instancing]") {
    CHECK(MAX_INSTANCES_PER_BATCH == 1024u);
}

TEST_CASE("INSTANCE_BUFFER_SIZE is 64 KB (65536 bytes)", "[renderer][instancing]") {
    CHECK(INSTANCE_BUFFER_SIZE == 65536u);
}

TEST_CASE("Instance buffer size equals MAX_INSTANCES * sizeof(InstanceData)", "[renderer][instancing]") {
    CHECK(INSTANCE_BUFFER_SIZE == MAX_INSTANCES_PER_BATCH * sizeof(InstanceData));
}

TEST_CASE("INSTANCE_ATTR_SLOT_BASE is 8", "[renderer][instancing]") {
    CHECK(INSTANCE_ATTR_SLOT_BASE == 8u);
}

TEST_CASE("INSTANCE_ATTR_SLOT_COUNT is 4 (mat4 = 4 vec4 columns)", "[renderer][instancing]") {
    CHECK(INSTANCE_ATTR_SLOT_COUNT == 4u);
}

TEST_CASE("INSTANCING_THRESHOLD is 2", "[renderer][instancing]") {
    CHECK(INSTANCING_THRESHOLD == 2u);
}

// ===========================================================================
// Instanced shader enum values
// ===========================================================================

TEST_CASE("BuiltinShader: MESH_BLINN_PHONG_INSTANCED has value 13", "[renderer][instancing]") {
    CHECK(static_cast<u32>(BuiltinShader::MESH_BLINN_PHONG_INSTANCED) == 13);
}

TEST_CASE("BuiltinShader: MESH_PBR_INSTANCED has value 14", "[renderer][instancing]") {
    CHECK(static_cast<u32>(BuiltinShader::MESH_PBR_INSTANCED) == 14);
}

TEST_CASE("BuiltinShader: SHADOW_DEPTH_INSTANCED has value 15", "[renderer][instancing]") {
    CHECK(static_cast<u32>(BuiltinShader::SHADOW_DEPTH_INSTANCED) == 15);
}

TEST_CASE("BuiltinShader: COUNT is 19 (includes instanced + post-process shaders)", "[renderer][instancing]") {
    CHECK(static_cast<u32>(BuiltinShader::COUNT) == 19);
}

// ===========================================================================
// Instance buffer math validation
// ===========================================================================

TEST_CASE("Instance buffer: filling MAX_INSTANCES_PER_BATCH entries uses exactly INSTANCE_BUFFER_SIZE bytes",
          "[renderer][instancing]") {
    constexpr u32 filledBytes = MAX_INSTANCES_PER_BATCH * static_cast<u32>(sizeof(InstanceData));
    CHECK(filledBytes == INSTANCE_BUFFER_SIZE);
}

TEST_CASE("Instance buffer: partial fill of N entries uses N*64 bytes", "[renderer][instancing]") {
    constexpr u32 N = 42;
    constexpr u32 expected = N * 64;
    CHECK(N * static_cast<u32>(sizeof(InstanceData)) == expected);
}

TEST_CASE("Instance buffer: mat4 column stride is 16 bytes (sizeof(vec4))", "[renderer][instancing]") {
    // Each attribute slot consumes one vec4 (16 bytes).
    // The stride between columns within an InstanceData is sizeof(InstanceData) = 64 bytes
    // (because we set the stride to sizeof(InstanceData) in the glVertexAttribPointer call).
    // But the offset between column 0 and column 1 within a single instance is sizeof(vec4).
    CHECK(sizeof(glm::vec4) == 16);

    // Column offsets within InstanceData:
    // col 0: offset 0
    // col 1: offset 16
    // col 2: offset 32
    // col 3: offset 48
    for (u32 col = 0; col < 4; ++col) {
        CHECK(col * sizeof(glm::vec4) == col * 16u);
    }
}

// ===========================================================================
// getInstanceCount (ECS query helper)
// ===========================================================================

TEST_CASE("getInstanceCount: returns 0 for empty world", "[renderer][instancing]") {
    World world;
    CHECK(getInstanceCount(world, 1) == 0);
}

TEST_CASE("getInstanceCount: returns 0 for non-existent mesh handle", "[renderer][instancing]") {
    World world;
    const EntityId eid = world.createEntity();
    world.registry().emplace<Mesh>(static_cast<entt::entity>(eid), Mesh{MeshHandle{5}, 0});
    CHECK(getInstanceCount(world, 99) == 0);
}

TEST_CASE("getInstanceCount: counts entities with matching mesh handle", "[renderer][instancing]") {
    World world;

    // Create 3 entities with mesh handle 7
    for (int i = 0; i < 3; ++i) {
        const EntityId eid = world.createEntity();
        world.registry().emplace<Mesh>(static_cast<entt::entity>(eid), Mesh{MeshHandle{7}, 0});
    }
    // Create 2 entities with mesh handle 3
    for (int i = 0; i < 2; ++i) {
        const EntityId eid = world.createEntity();
        world.registry().emplace<Mesh>(static_cast<entt::entity>(eid), Mesh{MeshHandle{3}, 0});
    }

    CHECK(getInstanceCount(world, 7) == 3);
    CHECK(getInstanceCount(world, 3) == 2);
}

TEST_CASE("getInstanceCount: returns 1 for single entity with mesh handle", "[renderer][instancing]") {
    World world;
    const EntityId eid = world.createEntity();
    world.registry().emplace<Mesh>(static_cast<entt::entity>(eid), Mesh{MeshHandle{1}, 0});
    CHECK(getInstanceCount(world, 1) == 1);
}
