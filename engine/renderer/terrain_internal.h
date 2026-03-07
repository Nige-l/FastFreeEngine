#pragma once

// terrain_internal.h -- Internal accessors for terrain data.
//
// NOT part of the public API. Used by terrain_renderer.cpp to access
// terrain asset data (chunk VAOs, diffuse texture, config) without
// exposing internal data structures in the public header.

#include "renderer/terrain.h"
#include "renderer/rhi_types.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <memory>

namespace ffe::renderer {

// Maximum LOD levels per chunk. LOD 0 = full resolution, LOD 1 = half, LOD 2 = quarter.
inline constexpr u32 MAX_LOD_LEVELS = 3;

// Per-LOD GPU resources for a single terrain chunk.
struct TerrainChunkLod {
    GLuint vaoId     = 0;
    GLuint vboId     = 0;
    GLuint iboId     = 0;
    u32    indexCount = 0;
};

// Internal per-chunk GPU data with multiple LOD levels.
struct TerrainChunkGpu {
    TerrainChunkLod lods[MAX_LOD_LEVELS];
    u32 lodCount = 0;
    // AABB for frustum culling
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    // Center for LOD distance calculation
    glm::vec3 center{0.0f};
};

// Internal terrain asset record.
struct TerrainAsset {
    bool active = false;
    TerrainConfig config;
    std::unique_ptr<float[]> heightData;
    u32 heightDataWidth  = 0;
    u32 heightDataHeight = 0;
    TerrainChunkGpu chunks[MAX_CHUNKS_TOTAL];
    u32 chunkCount = 0;
    rhi::TextureHandle diffuseTexture{0};
    TerrainMaterial material;  // M2: splat-map texturing material
    TerrainLodConfig lodConfig;  // M3: LOD distance configuration
};

// Get internal terrain asset data. Returns nullptr for invalid/inactive handles.
// Defined in terrain.cpp.
const TerrainAsset* getTerrainAsset(TerrainHandle handle);

} // namespace ffe::renderer
