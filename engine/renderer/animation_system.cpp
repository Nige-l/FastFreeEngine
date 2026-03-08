// animation_system.cpp — Skeletal animation update system implementation.
//
// Per-tick system that advances animation time, samples keyframes via binary
// search + interpolation, walks the bone hierarchy to compute world-space
// transforms, and writes final bone matrices into Skeleton::boneMatrices[].
//
// Features:
//   - Single-clip playback (advance time, sample channels, walk hierarchy)
//   - Crossfade blending (lerp/slerp between two clips during transitions)
//   - Interpolation modes (STEP, LINEAR; CUBIC_SPLINE stubs to LINEAR)
//   - Root motion extraction (XZ delta for gameplay, in-place animation)
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

// ---------------------------------------------------------------------------
// Interpolation-mode-aware sampling helpers
// ---------------------------------------------------------------------------

// Sample translation at a given time, respecting interpolation mode.
static glm::vec3 sampleTranslation(const AnimationChannel& ch, const f32 time) {
    if (ch.translationCount == 0) {
        return glm::vec3(0.0f);
    }
    if (ch.translationCount == 1) {
        return ch.translationValues[0];
    }
    const u32 idx = findKeyframeIndex(ch.translationTimes, ch.translationCount, time);
    const u32 nextIdx = idx + 1 < ch.translationCount ? idx + 1 : idx;

    if (ch.mode == InterpolationMode::STEP) {
        return ch.translationValues[idx];
    }

    // LINEAR (and CUBIC_SPLINE fallback)
    const f32 t = computeLerpFactor(ch.translationTimes[idx], ch.translationTimes[nextIdx], time);
    return glm::mix(ch.translationValues[idx], ch.translationValues[nextIdx], t);
}

// Sample rotation at a given time (slerp on quaternions), respecting interpolation mode.
static glm::quat sampleRotation(const AnimationChannel& ch, const f32 time) {
    if (ch.rotationCount == 0) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity
    }
    if (ch.rotationCount == 1) {
        return ch.rotationValues[0];
    }
    const u32 idx = findKeyframeIndex(ch.rotationTimes, ch.rotationCount, time);
    const u32 nextIdx = idx + 1 < ch.rotationCount ? idx + 1 : idx;

    if (ch.mode == InterpolationMode::STEP) {
        return ch.rotationValues[idx];
    }

    // LINEAR (and CUBIC_SPLINE fallback — slerp)
    const f32 t = computeLerpFactor(ch.rotationTimes[idx], ch.rotationTimes[nextIdx], time);
    return glm::slerp(ch.rotationValues[idx], ch.rotationValues[nextIdx], t);
}

// Sample scale at a given time, respecting interpolation mode.
static glm::vec3 sampleScale(const AnimationChannel& ch, const f32 time) {
    if (ch.scaleCount == 0) {
        return glm::vec3(1.0f);
    }
    if (ch.scaleCount == 1) {
        return ch.scaleValues[0];
    }
    const u32 idx = findKeyframeIndex(ch.scaleTimes, ch.scaleCount, time);
    const u32 nextIdx = idx + 1 < ch.scaleCount ? idx + 1 : idx;

    if (ch.mode == InterpolationMode::STEP) {
        return ch.scaleValues[idx];
    }

    // LINEAR (and CUBIC_SPLINE fallback)
    const f32 t = computeLerpFactor(ch.scaleTimes[idx], ch.scaleTimes[nextIdx], time);
    return glm::mix(ch.scaleValues[idx], ch.scaleValues[nextIdx], t);
}

// ---------------------------------------------------------------------------
// Helper: sample all channels of a clip and produce local bone transforms
// ---------------------------------------------------------------------------

static void sampleClipLocalTransforms(
    const AnimationClipData& clip,
    const SkeletonData& skelData,
    const f32 time,
    glm::mat4* outLocal) // must have room for MAX_BONES entries
{
    for (u32 b = 0; b < skelData.boneCount; ++b) {
        outLocal[b] = glm::mat4(1.0f); // identity default
    }

    for (u32 b = 0; b < skelData.boneCount; ++b) {
        const AnimationChannel& ch = clip.channels[b];

        // Check if this bone has any animation data
        if (ch.translationCount == 0 && ch.rotationCount == 0 && ch.scaleCount == 0) {
            continue; // Leave as identity
        }

        const glm::vec3 translation = sampleTranslation(ch, time);
        const glm::quat rotation    = sampleRotation(ch, time);
        const glm::vec3 scale       = sampleScale(ch, time);

        // Compose local transform: T * R * S
        glm::mat4 local = glm::translate(glm::mat4(1.0f), translation);
        local = local * glm::mat4_cast(rotation);
        local = glm::scale(local, scale);
        outLocal[b] = local;
    }
}

// ---------------------------------------------------------------------------
// Helper: decompose a mat4 into T/R/S (for blending)
// Assumes no shear — only TRS compose (which is what we produce).
// ---------------------------------------------------------------------------

struct TRS {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

static TRS decomposeTRS(const glm::mat4& m) {
    TRS result;
    result.translation = glm::vec3(m[3]);

    // Extract scale from column lengths
    result.scale.x = glm::length(glm::vec3(m[0]));
    result.scale.y = glm::length(glm::vec3(m[1]));
    result.scale.z = glm::length(glm::vec3(m[2]));

    // Build rotation matrix from normalised columns
    glm::mat3 rotMat;
    if (result.scale.x > 0.0f) rotMat[0] = glm::vec3(m[0]) / result.scale.x;
    else rotMat[0] = glm::vec3(1.0f, 0.0f, 0.0f);
    if (result.scale.y > 0.0f) rotMat[1] = glm::vec3(m[1]) / result.scale.y;
    else rotMat[1] = glm::vec3(0.0f, 1.0f, 0.0f);
    if (result.scale.z > 0.0f) rotMat[2] = glm::vec3(m[2]) / result.scale.z;
    else rotMat[2] = glm::vec3(0.0f, 0.0f, 1.0f);

    result.rotation = glm::quat_cast(rotMat);
    return result;
}

static glm::mat4 composeTRS(const TRS& trs) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), trs.translation);
    m = m * glm::mat4_cast(trs.rotation);
    m = glm::scale(m, trs.scale);
    return m;
}

// ---------------------------------------------------------------------------
// Helper: blend two sets of local transforms using TRS decomposition
// ---------------------------------------------------------------------------

static void blendLocalTransforms(
    const glm::mat4* fromLocal,
    const glm::mat4* toLocal,
    const u32 boneCount,
    const f32 alpha,
    glm::mat4* outLocal) // must have room for MAX_BONES entries
{
    for (u32 b = 0; b < boneCount; ++b) {
        const TRS fromTRS = decomposeTRS(fromLocal[b]);
        const TRS toTRS   = decomposeTRS(toLocal[b]);

        TRS blended;
        blended.translation = glm::mix(fromTRS.translation, toTRS.translation, alpha);
        blended.rotation    = glm::slerp(fromTRS.rotation, toTRS.rotation, alpha);
        blended.scale       = glm::mix(fromTRS.scale, toTRS.scale, alpha);

        outLocal[b] = composeTRS(blended);
    }
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

        // Guard against negative time (e.g. speed < 0 combined with external set)
        if (animState.time < 0.0f) animState.time = 0.0f;

        // Advance time for the target clip
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

        // Advance crossfade blend time if blending
        if (animState.blending) {
            animState.blendElapsed += dt;
            // Also advance the "from" clip time
            animState.blendFromTime += dt * animState.speed;

            // Wrap from-clip time if it has a valid clip
            if (animState.blendFromClip >= 0 &&
                static_cast<u32>(animState.blendFromClip) < meshAnims->clipCount) {
                const f32 fromDur = meshAnims->clips[animState.blendFromClip].duration;
                if (fromDur > 0.0f && animState.blendFromTime >= fromDur) {
                    animState.blendFromTime = std::fmod(animState.blendFromTime, fromDur);
                }
            }

            // Check if blend is complete
            if (animState.blendElapsed >= animState.blendDuration) {
                animState.blending      = false;
                animState.blendFromClip = -1;
                animState.blendElapsed  = 0.0f;
                animState.blendDuration = 0.0f;
                animState.blendFromTime = 0.0f;
            }
        }

        // Ensure bone count matches
        skeleton.boneCount = skelData->boneCount;

        // --- Sample keyframes and compute local bone transforms ---
        // Stack-allocated arrays for local-space transforms (no heap)
        glm::mat4 localTransforms[MAX_BONES];
        sampleClipLocalTransforms(clip, *skelData, animState.time, localTransforms);

        // --- Crossfade blending: blend with "from" clip if active ---
        if (animState.blending &&
            animState.blendFromClip >= 0 &&
            static_cast<u32>(animState.blendFromClip) < meshAnims->clipCount &&
            animState.blendDuration > 0.0f) {

            const AnimationClipData& fromClip = meshAnims->clips[animState.blendFromClip];
            glm::mat4 fromLocal[MAX_BONES];
            sampleClipLocalTransforms(fromClip, *skelData, animState.blendFromTime, fromLocal);

            const f32 blendAlpha = glm::clamp(
                animState.blendElapsed / animState.blendDuration, 0.0f, 1.0f);

            // Blend: result = lerp(fromBone, toBone, blendAlpha)
            glm::mat4 blendedLocal[MAX_BONES];
            blendLocalTransforms(fromLocal, localTransforms, skelData->boneCount,
                                 blendAlpha, blendedLocal);

            // Copy blended result back into localTransforms
            for (u32 b = 0; b < skelData->boneCount; ++b) {
                localTransforms[b] = blendedLocal[b];
            }
        }

        // --- Root motion extraction (before hierarchy walk) ---
        auto* rootMotion = world.registry().try_get<RootMotionDelta>(entity);
        if (rootMotion != nullptr && rootMotion->enabled && skelData->boneCount > 0) {
            // Extract root bone (index 0) translation
            const glm::vec3 currentRootTranslation = glm::vec3(localTransforms[0][3]);

            // Compute delta
            rootMotion->translationDelta =
                currentRootTranslation - rootMotion->previousRootTranslation;

            // Store for next frame
            rootMotion->previousRootTranslation = currentRootTranslation;

            // Zero root bone XZ translation (keep Y for gravity)
            localTransforms[0][3].x = 0.0f;
            localTransforms[0][3].z = 0.0f;
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
