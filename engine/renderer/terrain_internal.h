#pragma once

// terrain_internal.h -- Internal accessors for terrain data.
//
// NOT part of the public API. Used by terrain_renderer.cpp to access
// terrain asset data (chunk VAOs, diffuse texture, config) without
// exposing internal data structures in the public header.

#include "renderer/terrain.h"
#include "renderer/rhi_types.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// M4: World Streaming (GL-free types — no glad dependency)
// ---------------------------------------------------------------------------

// Per-chunk streaming state machine.
//   EAGER            -- streaming disabled; loaded at loadTerrain(), never evicted (default)
//   UNLOADED         -- streaming enabled; not on GPU, eligible for load
//   QUEUED           -- enqueued for background mesh generation
//   GENERATING       -- worker thread is building vertex/index data
//   READY_TO_UPLOAD  -- CPU buffers ready; main thread must GL-upload
//   LOADED           -- on GPU; participates in render, LOD, frustum
//   UNLOADING        -- eviction pending; main thread frees GL objects, sets UNLOADED
enum class ChunkState : uint8_t {
    EAGER,
    UNLOADED,
    QUEUED,
    GENERATING,
    READY_TO_UPLOAD,
    LOADED,
    UNLOADING,
};

// Per-chunk CPU-side streaming buffers. Populated by worker, consumed by main thread upload.
// Pre-allocated in TerrainAsset -- no per-frame heap allocation on the main thread hot path.
struct ChunkStreamState {
    std::atomic<ChunkState> state{ChunkState::EAGER};
    std::vector<float>    cpuVertices;   // populated by worker; freed after upload
    std::vector<uint32_t> cpuIndices;    // populated by worker; freed after upload

    // Non-copyable, non-movable: atomics and vectors cannot be trivially relocated
    // once a background thread holds a pointer to this struct.
    ChunkStreamState() = default;
    ChunkStreamState(const ChunkStreamState&) = delete;
    ChunkStreamState& operator=(const ChunkStreamState&) = delete;
    ChunkStreamState(ChunkStreamState&&) = delete;
    ChunkStreamState& operator=(ChunkStreamState&&) = delete;
};

// Coordinate of a chunk in the grid. Used as work-queue items.
struct ChunkCoord {
    u32 x = 0;
    u32 z = 0;
};

// Streaming configuration per terrain asset.
struct TerrainStreamingConfig {
    int   radiusChunks       = 0;     // 0 = disabled (EAGER mode for all chunks)
    float lastCamX           = 1e9f;  // last camera X that triggered a tick
    float lastCamZ           = 1e9f;  // last camera Z that triggered a tick
    float dirtyCamThreshold  = 0.0f;  // 0.5 * chunkWorldSize, set at enablement
};

// ---------------------------------------------------------------------------
// GL-dependent types (require glad/glad.h)
// ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <glm/glm.hpp>

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

    // M4: per-chunk streaming state (pre-allocated, never moved)
    std::array<ChunkStreamState, MAX_CHUNKS_TOTAL> chunkStream;

    // M4: streaming configuration
    TerrainStreamingConfig streamConfig;

    // M4: background worker thread state
    std::thread              workerThread;
    std::mutex               queueMutex;
    std::condition_variable  queueCV;
    std::atomic<bool>        workerRunning{false};
    std::deque<ChunkCoord>   workQueue;

    // TerrainAsset is non-copyable due to mutex/thread/atomic members.
    TerrainAsset() = default;
    TerrainAsset(const TerrainAsset&) = delete;
    TerrainAsset& operator=(const TerrainAsset&) = delete;
    TerrainAsset(TerrainAsset&&) = delete;
    TerrainAsset& operator=(TerrainAsset&&) = delete;
};

// Get internal terrain asset data. Returns nullptr for invalid/inactive handles.
// Defined in terrain.cpp.
const TerrainAsset* getTerrainAsset(TerrainHandle handle);

// Get mutable internal terrain asset data. Returns nullptr for invalid/inactive handles.
// Defined in terrain.cpp. Used by terrain_renderer.cpp for streaming tick.
TerrainAsset* getTerrainAssetMut(TerrainHandle handle);

// M4: Per-frame streaming tick -- classify chunks in/out of radius, enqueue work.
// Called from terrainRenderSystem() on the main thread. No-op if radiusChunks == 0.
// Defined in terrain.cpp.
void terrainStreamingTick(TerrainAsset& asset, f32 camX, f32 camZ);

// M4: Upload READY_TO_UPLOAD chunks to GPU. Called from terrainRenderSystem() on main thread.
// No-op if radiusChunks == 0. Defined in terrain.cpp.
void terrainUploadPendingChunks(TerrainAsset& asset);

} // namespace ffe::renderer
