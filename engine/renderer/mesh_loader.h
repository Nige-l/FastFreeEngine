#pragma once

// mesh_loader.h — glTF 2.0 mesh loading and GPU upload for FFE.
//
// NOT a per-frame API. All functions perform file I/O and/or GPU uploads.
// Call at scene load time or Application::startup(). Never call per-frame.
// Calling loadMesh() from a hot path is a performance violation.
//
// Thread safety: single-threaded. All functions must be called from the render thread.
//
// Tier support: LEGACY (primary). Requires OpenGL 3.3 core profile.
// Uses one VAO per mesh. All indices widened to u32 internally.
//
// Only .glb (binary glTF) files are accepted. .gltf with external .bin is not
// supported in this session — see SEC-M8 in ADR-007-3d-foundation.md.

#include "core/types.h"
#include "renderer/rhi_types.h"
#include "renderer/skeleton.h"

namespace ffe::renderer {

// --- Opaque handle to a GPU-resident mesh ---
// Value 0 is always invalid (null handle). Same pattern as rhi::BufferHandle.
struct MeshHandle { u32 id = 0; };
inline bool isValid(const MeshHandle h) { return h.id != 0; }
static_assert(sizeof(MeshHandle) == 4);

// Maximum mesh assets that can be loaded simultaneously.
inline constexpr u32 MAX_MESH_ASSETS = 100;

// Maximum .glb file size accepted by loadMesh(). Pre-parse guard (SEC-M2).
inline constexpr u64 MESH_FILE_SIZE_LIMIT = 64ull * 1024ull * 1024ull; // 64 MB

// Maximum vertex count per mesh (single primitive).
inline constexpr u32 MAX_MESH_VERTICES = 1'000'000;

// Maximum index count per mesh.
inline constexpr u32 MAX_MESH_INDICES = 3'000'000;

// Internal GPU record type — defined here so mesh_renderer.cpp can dereference
// the pointer returned by getMeshGpuRecord() without an incomplete-type error.
// Not part of the public game API; game code must not use this struct directly.
struct MeshGpuRecord {
    unsigned int vaoId      = 0;  // Vertex Array Object (raw GL ID)
    unsigned int vboId      = 0;  // Vertex Buffer Object (raw GL ID)
    unsigned int iboId      = 0;  // Index Buffer Object  (raw GL ID)
    rhi::BufferHandle vboHandle{};  // RHI handle for VRAM tracking + destroyBuffer
    rhi::BufferHandle iboHandle{};  // RHI handle for VRAM tracking + destroyBuffer
    u32 indexCount  = 0;
    u32 vertexCount = 0;
    bool alive       = false;
    bool hasSkeleton = false;  // true if this mesh has skin data (skeletal animation)
};

// Load a glTF 2.0 mesh from a file path relative to the asset root.
//
// Path validation is identical to texture_loader (path traversal prevention,
// realpath check). Additionally, the path must end with ".glb" (SEC-M8).
//
// Loads the FIRST mesh primitive from the FIRST mesh in the glTF scene.
// Unindexed meshes are not supported — the primitive must have indices.
//
// On success: returns a valid MeshHandle (handle.id > 0).
// On failure: returns MeshHandle{0} and logs via FFE_LOG_ERROR.
//
// Cold path only — do NOT call per frame. Call at scene load time.
MeshHandle loadMesh(const char* path);

// Destroy a loaded mesh and free all GPU resources (VAO, VBO, IBO).
// Safe to call with an invalid handle (id == 0) — no-op.
void unloadMesh(MeshHandle handle);

// Destroy all loaded meshes. Called by Application::shutdown().
void unloadAllMeshes();

// Get the number of vertices in a loaded mesh (0 if invalid).
u32 getMeshVertexCount(MeshHandle handle);

// Get the number of indices in a loaded mesh (0 if invalid).
u32 getMeshIndexCount(MeshHandle handle);

// Returns a pointer to the internal GPU record for use by meshRenderSystem.
// Returns nullptr if the handle is invalid or the slot is not alive.
// Only for use by mesh_renderer.cpp — not part of the public game API.
const MeshGpuRecord* getMeshGpuRecord(MeshHandle handle);

// Returns the skeleton data for a mesh (read-only after load).
// Returns nullptr if the mesh has no skeleton or the handle is invalid.
const SkeletonData* getMeshSkeletonData(MeshHandle handle);

// Returns the animation data for a mesh (read-only after load).
// Returns nullptr if the mesh has no animations or the handle is invalid.
const MeshAnimations* getMeshAnimations(MeshHandle handle);

} // namespace ffe::renderer
