// animation_system.cpp — Skeletal animation update system implementation.
//
// Per-tick system that advances animation time, samples keyframes via binary
// search + interpolation, walks the bone hierarchy to compute world-space
// transforms, and writes final bone matrices into Skeleton::boneMatrices[].
//
// No heap allocations. All temporary matrices use stack-allocated fixed arrays.
// Performance: O(bones * log(keyframes)) per animated entity per tick.

#include "renderer/animation_system.h"
#include "renderer/render_system.h"
#include "renderer/mesh_loader.h"
#include "renderer/skeleton.h"
#include "core/logging.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm> // std::lower_bound
#include <cmath>     // std::fmod

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// Keyframe sampling helpers (binary search + interpolation)
// ---------------------------------------------------------------------------

// Find the index of the keyframe just before or at the given time.
// Returns the index of the lower bracket; upper bracket is index + 1.
// If time <= first keyframe, returns 0. If time >= last, returns count - 2.
static u32 findKeyframeIndex(const f32* times, const u32 count, const f32 time) {
    if (count <= 1) {
        return 0;
    }
    // Binary search: find first element > time, then step back one.
    // lower_bound returns iterator to first element >= time.
    const f32* const begin = times;
    const f32* const end   = times + count;
    const f32* it = std::lower_bound(begin, end, time);

    if (it == end) {
        // time >= all keyframes — clamp to last pair
        return count - 2;
    }
    if (it == begin) {
        return 0;
    }
    // it points to first element >= time; we want the element before it
    const auto idx = static_cast<u32>(it - begin);
    return idx - 1;
}

// Compute interpolation factor between two keyframes.
static f32 computeLerpFactor(const f32 timeLow, const f32 timeHigh, const f32 currentTime) {
    const f32 span = timeHigh - timeLow;
    if (span <= 0.0f) {
        return 0.0f;
    }
    const f32 t = (currentTime - timeLow) / span;
    // Clamp to [0, 1] to handle float precision edge cases
    return glm::clamp(t, 0.0f, 1.0f);
}

// Sample translation at a given time (linear interpolation).
static glm::vec3 sampleTranslation(const AnimationChannel& ch, const f32 time) {
    if (ch.translationCount == 0) {
        return glm::vec3(0.0f);
    }
    if (ch.translationCount == 1) {
        return ch.translationValues[0];
    }
    const u32 idx = findKeyframeIndex(ch.translationTimes, ch.translationCount, time);
    const u32 nextIdx = idx + 1 < ch.translationCount ? idx + 1 : idx;
    const f32 t = computeLerpFactor(ch.translationTimes[idx], ch.translationTimes[nextIdx], time);
    return glm::mix(ch.translationValues[idx], ch.translationValues[nextIdx], t);
}

// Sample rotation at a given time (slerp on quaternions).
static glm::quat sampleRotation(const AnimationChannel& ch, const f32 time) {
    if (ch.rotationCount == 0) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    }
    if (ch.rotationCount == 1) {
        return ch.rotationValues[0];
    }
    const u32 idx = findKeyframeIndex(ch.rotationTimes, ch.rotationCount, time);
    const u32 nextIdx = idx + 1 < ch.rotationCount ? idx + 1 : idx;
    const f32 t = computeLerpFactor(ch.rotationTimes[idx], ch.rotationTimes[nextIdx], time);
    return glm::slerp(ch.rotationValues[idx], ch.rotationValues[nextIdx], t);
}

// Sample scale at a given time (linear interpolation).
static glm::vec3 sampleScale(const AnimationChannel& ch, const f32 time) {
    if (ch.scaleCount == 0) {
        return glm::vec3(1.0f);
    }
    if (ch.scaleCount == 1) {
        return ch.scaleValues[0];
    }
    const u32 idx = findKeyframeIndex(ch.scaleTimes, ch.scaleCount, time);
    const u32 nextIdx = idx + 1 < ch.scaleCount ? idx + 1 : idx;
    const f32 t = computeLerpFactor(ch.scaleTimes[idx], ch.scaleTimes[nextIdx], time);
    return glm::mix(ch.scaleValues[idx], ch.scaleValues[nextIdx], t);
}

// ---------------------------------------------------------------------------
// Animation update system
// ---------------------------------------------------------------------------

void animationUpdateSystem3D(World& world, const float dt) {
    auto view = world.registry().view<AnimationState, Skeleton, const Mesh>();

    for (const auto entity : view) {
        auto& animState = view.get<AnimationState>(entity);
        if (!animState.playing) {
            continue;
        }

        auto& skeleton = view.get<Skeleton>(entity);
        const auto& meshComp = view.get<const Mesh>(entity);

        // Get the skeleton data and animations from the mesh asset cache
        const SkeletonData* skelData = getMeshSkeletonData(meshComp.meshHandle);
        const MeshAnimations* meshAnims = getMeshAnimations(meshComp.meshHandle);
        if (skelData == nullptr || meshAnims == nullptr) {
            continue;
        }
        if (animState.clipIndex >= meshAnims->clipCount) {
            animState.playing = false;
            continue;
        }

        const AnimationClipData& clip = meshAnims->clips[animState.clipIndex];
        if (clip.duration <= 0.0f) {
            animState.playing = false;
            continue;
        }

        // Advance time
        animState.time += dt * animState.speed;

        // Handle looping / clamping
        if (animState.time >= clip.duration) {
            if (animState.looping) {
                animState.time = std::fmod(animState.time, clip.duration);
            } else {
                animState.time = clip.duration;
                animState.playing = false;
            }
        }

        // Ensure bone count matches
        skeleton.boneCount = skelData->boneCount;

        // --- Sample keyframes and compute local bone transforms ---
        // Stack-allocated array for local-space transforms
        glm::mat4 localTransforms[MAX_BONES];
        for (u32 b = 0; b < skelData->boneCount; ++b) {
            localTransforms[b] = glm::mat4(1.0f); // identity default
        }

        // Sample channels — each channel targets a specific bone
        for (u32 b = 0; b < skelData->boneCount; ++b) {
            const AnimationChannel& ch = clip.channels[b];

            // Check if this bone has any animation data
            if (ch.translationCount == 0 && ch.rotationCount == 0 && ch.scaleCount == 0) {
                continue; // Leave as identity
            }

            const glm::vec3 translation = sampleTranslation(ch, animState.time);
            const glm::quat rotation    = sampleRotation(ch, animState.time);
            const glm::vec3 scale       = sampleScale(ch, animState.time);

            // Compose local transform: T * R * S
            glm::mat4 local = glm::translate(glm::mat4(1.0f), translation);
            local = local * glm::mat4_cast(rotation);
            local = glm::scale(local, scale);
            localTransforms[b] = local;
        }

        // --- Walk bone hierarchy (parent-first order) to compute world-space ---
        // glTF guarantees parent bones appear before children.
        glm::mat4 worldTransforms[MAX_BONES];
        for (u32 b = 0; b < skelData->boneCount; ++b) {
            const i32 parentIdx = skelData->bones[b].parentIndex;
            if (parentIdx >= 0 && static_cast<u32>(parentIdx) < skelData->boneCount) {
                worldTransforms[b] = worldTransforms[parentIdx] * localTransforms[b];
            } else {
                worldTransforms[b] = localTransforms[b];
            }
        }

        // --- Compute final bone matrices: worldTransform * inverseBindMatrix ---
        for (u32 b = 0; b < skelData->boneCount; ++b) {
            skeleton.boneMatrices[b] = worldTransforms[b] * skelData->bones[b].inverseBindMatrix;
        }
    }
}

} // namespace ffe::renderer
