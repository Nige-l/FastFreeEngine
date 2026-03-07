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

// Internal per-chunk GPU data.
struct TerrainChunkGpu {
    GLuint vaoId      = 0;
    GLuint vboId      = 0;
    GLuint iboId      = 0;
    u32    indexCount  = 0;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
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
};

// Get internal terrain asset data. Returns nullptr for invalid/inactive handles.
// Defined in terrain.cpp.
const TerrainAsset* getTerrainAsset(TerrainHandle handle);

} // namespace ffe::renderer
