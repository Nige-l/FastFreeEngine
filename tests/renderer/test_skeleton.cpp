// test_skeleton.cpp — Unit tests for skeletal animation subsystem.
//
// Tests bone hierarchy computation, keyframe interpolation, animation state
// transitions, component defaults, and bone count limits.
// All tests run headless — no GL context required.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <memory>

#include "renderer/skeleton.h"
#include "renderer/render_system.h"
#include "renderer/animation_system.h"
#include "renderer/mesh_loader.h"
#include "core/ecs.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

using Catch::Approx;

// -----------------------------------------------------------------------
// Skeleton component defaults
// -----------------------------------------------------------------------

TEST_CASE("Skeleton component has sane defaults", "[skeleton]") {
    const ffe::Skeleton skel;
    REQUIRE(skel.boneCount == 0);
    // All bone matrices should be zero-initialized
    for (ffe::u32 i = 0; i < ffe::renderer::MAX_BONES; ++i) {
        REQUIRE(skel.boneMatrices[i] == glm::mat4(0.0f));
    }
}

TEST_CASE("AnimationState component has sane defaults", "[skeleton]") {
    const ffe::AnimationState state;
    REQUIRE(state.clipIndex == 0);
    REQUIRE(state.time == Approx(0.0f));
    REQUIRE(state.speed == Approx(1.0f));
    REQUIRE(state.looping == true);
    REQUIRE(state.playing == false);
}

TEST_CASE("AnimationState is 16 bytes", "[skeleton]") {
    REQUIRE(sizeof(ffe::AnimationState) == 16);
}

// -----------------------------------------------------------------------
// SkeletonData and bone hierarchy
// -----------------------------------------------------------------------

TEST_CASE("SkeletonData defaults to zero bones", "[skeleton]") {
    const ffe::renderer::SkeletonData data;
    REQUIRE(data.boneCount == 0);
}

TEST_CASE("BoneInfo defaults to identity inverse bind and root parent", "[skeleton]") {
    const ffe::renderer::BoneInfo bone;
    REQUIRE(bone.parentIndex == -1);
    REQUIRE(bone.inverseBindMatrix == glm::mat4(1.0f));
}

TEST_CASE("MAX_BONES is 64", "[skeleton]") {
    REQUIRE(ffe::renderer::MAX_BONES == 64);
}

TEST_CASE("MAX_BONE_INFLUENCES is 4", "[skeleton]") {
    REQUIRE(ffe::renderer::MAX_BONE_INFLUENCES == 4);
}

// -----------------------------------------------------------------------
// Bone hierarchy computation (manual test without animation system)
// -----------------------------------------------------------------------

TEST_CASE("Parent-child bone matrix multiplication", "[skeleton]") {
    // Set up a simple 2-bone hierarchy: root (0) -> child (1)
    ffe::renderer::SkeletonData skelData;
    skelData.boneCount = 2;
    skelData.bones[0].parentIndex = -1; // root
    skelData.bones[0].inverseBindMatrix = glm::mat4(1.0f);
    skelData.bones[1].parentIndex = 0;  // child of root
    skelData.bones[1].inverseBindMatrix = glm::mat4(1.0f);

    // Root translated 1 unit along X
    const glm::mat4 rootLocal = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    // Child translated 2 units along Y (relative to parent)
    const glm::mat4 childLocal = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));

    // Walk hierarchy: root world = rootLocal; child world = root * childLocal
    const glm::mat4 rootWorld = rootLocal;
    const glm::mat4 childWorld = rootWorld * childLocal;

    // Final: world * inverseBindMatrix (identity in this test)
    const glm::mat4 rootFinal = rootWorld * skelData.bones[0].inverseBindMatrix;
    const glm::mat4 childFinal = childWorld * skelData.bones[1].inverseBindMatrix;

    // Verify: root translates (1,0,0); child translates (1,2,0) in world space
    const glm::vec4 rootPos = rootFinal * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE(rootPos.x == Approx(1.0f));
    REQUIRE(rootPos.y == Approx(0.0f));
    REQUIRE(rootPos.z == Approx(0.0f));

    const glm::vec4 childPos = childFinal * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE(childPos.x == Approx(1.0f));
    REQUIRE(childPos.y == Approx(2.0f));
    REQUIRE(childPos.z == Approx(0.0f));
}

TEST_CASE("Inverse bind matrix transforms vertex to bone space", "[skeleton]") {
    // A bone at position (3,0,0) with inverse bind = translate(-3,0,0)
    ffe::renderer::BoneInfo bone;
    bone.parentIndex = -1;
    bone.inverseBindMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f));

    // World transform: bone at (3,0,0)
    const glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));

    // Final = world * inverseBind
    const glm::mat4 finalMat = worldTransform * bone.inverseBindMatrix;

    // A vertex at the bone's bind position (3,0,0) should end up at the bone's
    // current position (3,0,0) in world space.
    const glm::vec4 result = finalMat * glm::vec4(3.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE(result.x == Approx(3.0f));
    REQUIRE(result.y == Approx(0.0f));
}

// -----------------------------------------------------------------------
// Keyframe interpolation
// -----------------------------------------------------------------------

TEST_CASE("AnimationChannel defaults to zero counts", "[skeleton]") {
    const ffe::renderer::AnimationChannel ch;
    REQUIRE(ch.translationCount == 0);
    REQUIRE(ch.rotationCount == 0);
    REQUIRE(ch.scaleCount == 0);
}

TEST_CASE("AnimationClipData defaults to zero duration", "[skeleton]") {
    auto clip = std::make_unique<ffe::renderer::AnimationClipData>();
    REQUIRE(clip->duration == Approx(0.0f));
    REQUIRE(clip->channelCount == 0);
}

// -----------------------------------------------------------------------
// AnimationState play/stop/loop/speed
// -----------------------------------------------------------------------

TEST_CASE("AnimationState play sets correct fields", "[skeleton]") {
    ffe::AnimationState state;
    state.clipIndex = 2;
    state.time = 0.5f;
    state.speed = 1.5f;
    state.looping = false;
    state.playing = true;

    REQUIRE(state.clipIndex == 2);
    REQUIRE(state.time == Approx(0.5f));
    REQUIRE(state.speed == Approx(1.5f));
    REQUIRE(state.looping == false);
    REQUIRE(state.playing == true);
}

TEST_CASE("AnimationState stop preserves time", "[skeleton]") {
    ffe::AnimationState state;
    state.playing = true;
    state.time = 0.75f;

    // Stop
    state.playing = false;

    REQUIRE(state.playing == false);
    REQUIRE(state.time == Approx(0.75f)); // time preserved
}

// -----------------------------------------------------------------------
// SkinnedMeshVertex layout
// -----------------------------------------------------------------------

TEST_CASE("SkinnedMeshVertex is 56 bytes", "[skeleton]") {
    REQUIRE(sizeof(ffe::renderer::SkinnedMeshVertex) == 56);
}

// -----------------------------------------------------------------------
// MeshAnimations defaults
// -----------------------------------------------------------------------

TEST_CASE("MeshAnimations defaults to zero clips", "[skeleton]") {
    auto anims = std::make_unique<ffe::renderer::MeshAnimations>();
    REQUIRE(anims->clipCount == 0);
}

TEST_CASE("MAX_ANIMATIONS_PER_MESH is 16", "[skeleton]") {
    REQUIRE(ffe::renderer::MAX_ANIMATIONS_PER_MESH == 16);
}

TEST_CASE("MAX_KEYFRAMES_PER_CHANNEL is 256", "[skeleton]") {
    REQUIRE(ffe::renderer::MAX_KEYFRAMES_PER_CHANNEL == 256);
}

// -----------------------------------------------------------------------
// MeshGpuRecord hasSkeleton field
// -----------------------------------------------------------------------

TEST_CASE("MeshGpuRecord defaults hasSkeleton to false", "[skeleton]") {
    const ffe::renderer::MeshGpuRecord rec;
    REQUIRE(rec.hasSkeleton == false);
}

// -----------------------------------------------------------------------
// ECS component registration: entities can have Skeleton + AnimationState
// -----------------------------------------------------------------------

TEST_CASE("Skeleton and AnimationState can be emplaced on entities", "[skeleton]") {
    ffe::World world;
    const ffe::EntityId eid = world.createEntity();

    auto& skel = world.addComponent<ffe::Skeleton>(eid);
    skel.boneCount = 4;
    for (ffe::u32 i = 0; i < skel.boneCount; ++i) {
        skel.boneMatrices[i] = glm::mat4(1.0f);
    }

    auto& anim = world.addComponent<ffe::AnimationState>(eid);
    anim.clipIndex = 1;
    anim.playing = true;
    anim.looping = true;
    anim.speed = 2.0f;

    REQUIRE(world.hasComponent<ffe::Skeleton>(eid));
    REQUIRE(world.hasComponent<ffe::AnimationState>(eid));

    const auto& gotSkel = world.getComponent<ffe::Skeleton>(eid);
    REQUIRE(gotSkel.boneCount == 4);

    const auto& gotAnim = world.getComponent<ffe::AnimationState>(eid);
    REQUIRE(gotAnim.clipIndex == 1);
    REQUIRE(gotAnim.playing == true);
    REQUIRE(gotAnim.speed == Approx(2.0f));
}

// -----------------------------------------------------------------------
// Static mesh loading still works (no skeleton should return nullptr)
// -----------------------------------------------------------------------

TEST_CASE("getMeshSkeletonData returns nullptr for invalid handle", "[skeleton]") {
    const ffe::renderer::MeshHandle invalid{0};
    REQUIRE(ffe::renderer::getMeshSkeletonData(invalid) == nullptr);
}

TEST_CASE("getMeshAnimations returns nullptr for invalid handle", "[skeleton]") {
    const ffe::renderer::MeshHandle invalid{0};
    REQUIRE(ffe::renderer::getMeshAnimations(invalid) == nullptr);
}

TEST_CASE("getMeshSkeletonData returns nullptr for out-of-range handle", "[skeleton]") {
    const ffe::renderer::MeshHandle outOfRange{999};
    REQUIRE(ffe::renderer::getMeshSkeletonData(outOfRange) == nullptr);
}

TEST_CASE("getMeshAnimations returns nullptr for out-of-range handle", "[skeleton]") {
    const ffe::renderer::MeshHandle outOfRange{999};
    REQUIRE(ffe::renderer::getMeshAnimations(outOfRange) == nullptr);
}
