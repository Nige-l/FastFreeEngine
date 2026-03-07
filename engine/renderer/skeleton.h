#pragma once

// skeleton.h — Bone hierarchy, animation clip data structures for skeletal animation.
//
// These structures are stored per mesh asset (read-only after load).
// Multiple entities sharing the same MeshHandle share the same SkeletonData
// and MeshAnimations. Per-entity mutable state lives in the ECS components
// (Skeleton, AnimationState) defined in render_system.h.
//
// Memory layout: fixed-size arrays, no heap allocation.
// All data is loaded at asset time (cold path), never per-frame.
//
// Tier support: LEGACY (OpenGL 3.3). 64 max bones fits within GL 3.3
// uniform limits (1024 vertex uniform components = 64 mat4).

#include "core/types.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ffe::renderer {

// --- Constants ---

inline constexpr u32 MAX_BONES = 64;
inline constexpr u32 MAX_BONE_INFLUENCES = 4;        // per vertex
inline constexpr u32 MAX_ANIMATIONS_PER_MESH = 16;
inline constexpr u32 MAX_KEYFRAMES_PER_CHANNEL = 256;

// --- Bone data (per mesh asset, read-only after load) ---

struct BoneInfo {
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f); // transforms vertex from model space to bone space
    i32       parentIndex       = -1;               // -1 for root bones
};

struct SkeletonData {
    BoneInfo bones[MAX_BONES] = {};
    u32      boneCount        = 0;
};

// --- Animation channel (per bone per clip) ---
// Stores keyframe times and values for translation, rotation, scale.
// Channels with count == 0 are unused (bone not animated in this clip).

struct AnimationChannel {
    u32       boneIndex        = 0;

    f32       translationTimes[MAX_KEYFRAMES_PER_CHANNEL]  = {};
    glm::vec3 translationValues[MAX_KEYFRAMES_PER_CHANNEL] = {};
    u32       translationCount = 0;

    f32       rotationTimes[MAX_KEYFRAMES_PER_CHANNEL]     = {};
    glm::quat rotationValues[MAX_KEYFRAMES_PER_CHANNEL]    = {};
    u32       rotationCount = 0;

    f32       scaleTimes[MAX_KEYFRAMES_PER_CHANNEL]        = {};
    glm::vec3 scaleValues[MAX_KEYFRAMES_PER_CHANNEL]       = {};
    u32       scaleCount = 0;
};

// --- Animation clip data (per clip per mesh asset) ---

struct AnimationClipData {
    AnimationChannel channels[MAX_BONES] = {}; // one channel per bone (sparse)
    f32 duration     = 0.0f;                   // total clip duration in seconds
    u32 channelCount = 0;                      // number of bones actually animated
};

// --- Collection of all animation clips for a mesh ---

struct MeshAnimations {
    AnimationClipData clips[MAX_ANIMATIONS_PER_MESH] = {};
    u32 clipCount = 0;
};

// --- Skinned mesh vertex (extends MeshVertex with bone data) ---
// Used only for meshes that have skeleton data.
// Position(3f) + Normal(3f) + Texcoord(2f) + Joints(4u16) + Weights(4f) = 56 bytes

struct SkinnedMeshVertex {
    f32 px, py, pz;  // Position  (12 bytes)
    f32 nx, ny, nz;  // Normal    (12 bytes)
    f32 u, v;        // Texcoord  (8 bytes)
    u16 jx, jy, jz, jw; // Joint indices (8 bytes, u16 for GL_UNSIGNED_SHORT)
    f32 wx, wy, wz, ww; // Weights  (16 bytes)
};
static_assert(sizeof(SkinnedMeshVertex) == 56, "SkinnedMeshVertex must be 56 bytes");

} // namespace ffe::renderer
