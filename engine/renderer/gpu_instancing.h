#pragma once

// gpu_instancing.h -- GPU instancing types and constants for FFE.
//
// Defines the per-instance data layout and batch size limits for
// instanced rendering of repeated meshes (glDrawElementsInstanced).
//
// Instance data is uploaded via a shared VBO with vertex attribute
// divisors. The model matrix occupies attribute slots 8-11 (each vec4
// column). This leaves slots 0-5 for mesh vertex data and 6-7 spare.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).
// glDrawElementsInstanced and glVertexAttribDivisor are core in GL 3.1+.
//
// No heap allocation. No virtual functions.

#include "core/types.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

// Maximum instances in a single instanced draw call.
// Buffer size: 1024 * 64 = 64 KB. Fits in VRAM trivially on LEGACY hardware.
inline constexpr u32 MAX_INSTANCES_PER_BATCH = 1024;

// Per-instance data uploaded to the GPU via instance VBO.
// Currently only the model matrix (mat4, 64 bytes).
// Per-instance material variation is deferred to a future milestone.
struct InstanceData {
    glm::mat4 modelMatrix;  // 64 bytes
};
static_assert(sizeof(InstanceData) == 64, "InstanceData must be 64 bytes (one mat4)");

// Total instance buffer size in bytes.
inline constexpr u32 INSTANCE_BUFFER_SIZE = MAX_INSTANCES_PER_BATCH * sizeof(InstanceData);
static_assert(INSTANCE_BUFFER_SIZE == 65536, "Instance buffer must be 64 KB");

// Vertex attribute slots used by instance data.
// A mat4 requires 4 consecutive vec4 attribute slots.
// Slots 0-5 are used by mesh vertex data (position, normal, texcoord, tangent, joints, weights).
// Slots 6-7 are reserved/spare. Slots 8-11 are instance data.
inline constexpr u32 INSTANCE_ATTR_SLOT_BASE = 8;
inline constexpr u32 INSTANCE_ATTR_SLOT_COUNT = 4;  // 4 vec4 columns of mat4

// Minimum entity count sharing a MeshHandle before instancing kicks in.
// With 1 entity, the regular single-draw path is used (no instance buffer overhead).
inline constexpr u32 INSTANCING_THRESHOLD = 2;

} // namespace ffe::renderer
