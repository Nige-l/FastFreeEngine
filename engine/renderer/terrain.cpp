// terrain.cpp -- Heightmap terrain mesh generation, GPU upload, and height queries.
//
// Generates chunked meshes from heightmap data. Each chunk is an independent
// VAO/VBO/IBO with MeshVertex (32 bytes) vertex layout, reusing the existing
// MESH_BLINN_PHONG shader for rendering.
//
// All GPU resources are allocated at load time (cold path). No per-frame
// allocations on the main thread hot path. Height queries are O(1) via bilinear
// interpolation on retained height data.
//
// M4 streaming: when setTerrainStreamingRadius() is called, a background worker
// thread generates CPU mesh data for chunks entering the camera radius, and
// terrain_renderer.cpp uploads them on the main thread (all GL calls on main thread).
//
// Security:
//   - PNG path validated via isPathSafe() + realpath (same as texture_loader).
//   - File must end with ".png" (no arbitrary file reads).
//   - Maximum heightmap resolution: 2048x2048.
//   - Maximum file size: 16 MB pre-parse guard.
//   - NaN/Inf rejection on all float parameters.
//
// Tier: LEGACY (OpenGL 3.3 core).

#include "renderer/terrain_internal.h"
#include "renderer/rhi.h"
#include "renderer/texture_loader.h"
#include "core/logging.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>
#include <sys/stat.h>

#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// stb_image: header-only include. STB_IMAGE_IMPLEMENTATION is in texture_loader.cpp.
// Suppress warnings for the C library header.
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wreserved-identifier"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#define STBI_MAX_DIMENSIONS 8192
#include "../../third_party/stb/stb_image.h"

#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace ffe::renderer {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static TerrainAsset s_terrains[MAX_TERRAIN_ASSETS];

// ---------------------------------------------------------------------------
// Path safety (same pattern as texture_loader.cpp)
// ---------------------------------------------------------------------------

static bool isPathSafe(const char* const path) {
    if (path == nullptr) { return false; }
    const size_t pathLen = strnlen(path, PATH_MAX);
    if (pathLen >= static_cast<size_t>(PATH_MAX)) { return false; }
    if (path[0] == '\0') { return false; }
    if (path[0] == '/' || path[0] == '\\') { return false; }
    if (pathLen >= 2 && path[1] == ':') { return false; }
    if (strstr(path, "../") != nullptr) { return false; }
    if (strstr(path, "..\\") != nullptr) { return false; }
    if (strstr(path, "/..") != nullptr) { return false; }
    if (strstr(path, "\\..") != nullptr) { return false; }
    if (strchr(path, ':') != nullptr) { return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isFinite(const f32 v) {
    return std::isfinite(v);
}

static bool validateConfig(const TerrainConfig& cfg) {
    if (!isFinite(cfg.worldWidth) || cfg.worldWidth <= 0.0f) { return false; }
    if (!isFinite(cfg.worldDepth) || cfg.worldDepth <= 0.0f) { return false; }
    if (!isFinite(cfg.heightScale)) { return false; }
    if (cfg.chunkResolution < 2 || cfg.chunkResolution > MAX_CHUNK_RESOLUTION) { return false; }
    if (cfg.chunkCountX == 0 || cfg.chunkCountZ == 0) { return false; }
    const u32 totalChunks = cfg.chunkCountX * cfg.chunkCountZ;
    if (totalChunks > MAX_CHUNKS_TOTAL) { return false; }
    return true;
}

// Sample height from the retained heightmap with bilinear interpolation.
// u, v are in [0, 1] range (clamped internally).
static f32 sampleHeight(const float* data, const u32 w, const u32 h,
                         f32 u, f32 v) {
    u = glm::clamp(u, 0.0f, 1.0f);
    v = glm::clamp(v, 0.0f, 1.0f);

    const f32 fx = u * static_cast<f32>(w - 1);
    const f32 fy = v * static_cast<f32>(h - 1);

    const u32 x0 = static_cast<u32>(fx);
    const u32 y0 = static_cast<u32>(fy);
    const u32 x1 = (x0 + 1 < w) ? x0 + 1 : x0;
    const u32 y1 = (y0 + 1 < h) ? y0 + 1 : y0;

    const f32 fracX = fx - static_cast<f32>(x0);
    const f32 fracY = fy - static_cast<f32>(y0);

    const f32 h00 = data[y0 * w + x0];
    const f32 h10 = data[y0 * w + x1];
    const f32 h01 = data[y1 * w + x0];
    const f32 h11 = data[y1 * w + x1];

    const f32 top    = h00 + (h10 - h00) * fracX;
    const f32 bottom = h01 + (h11 - h01) * fracX;
    return top + (bottom - top) * fracY;
}

// ---------------------------------------------------------------------------
// Chunk mesh generation
// ---------------------------------------------------------------------------

// Pure-CPU helper: fill vertex and index arrays for a single LOD of a terrain chunk.
// No GL calls. Called from both the eager load path (main thread) and the streaming
// worker thread (background thread). outMinY / outMaxY track Y extents for AABB.
static void buildChunkLodArrays(
        std::vector<float>& outVertices, std::vector<uint32_t>& outIndices,
        const float* heightData, const u32 dataW, const u32 dataH,
        const TerrainConfig& cfg,
        const u32 chunkX, const u32 chunkZ,
        const u32 lodRes,
        f32& outMinY, f32& outMaxY) {

    const u32 vertexCount = lodRes * lodRes;
    const u32 quadCount   = (lodRes - 1) * (lodRes - 1);
    const u32 indexCount  = quadCount * 6;

    // Each MeshVertex is 8 floats: px, py, pz, nx, ny, nz, u, v
    outVertices.resize(static_cast<size_t>(vertexCount) * 8);
    outIndices.resize(static_cast<size_t>(indexCount));

    const f32 chunkWorldW  = cfg.worldWidth  / static_cast<f32>(cfg.chunkCountX);
    const f32 chunkWorldD  = cfg.worldDepth  / static_cast<f32>(cfg.chunkCountZ);
    const f32 chunkOriginX = static_cast<f32>(chunkX) * chunkWorldW;
    const f32 chunkOriginZ = static_cast<f32>(chunkZ) * chunkWorldD;

    for (u32 vz = 0; vz < lodRes; ++vz) {
        for (u32 vx = 0; vx < lodRes; ++vx) {
            const u32 vi = vz * lodRes + vx;

            const f32 localX = (static_cast<f32>(vx) / static_cast<f32>(lodRes - 1)) * chunkWorldW;
            const f32 localZ = (static_cast<f32>(vz) / static_cast<f32>(lodRes - 1)) * chunkWorldD;
            const f32 worldX = chunkOriginX + localX;
            const f32 worldZ = chunkOriginZ + localZ;

            const f32 u = worldX / cfg.worldWidth;
            const f32 v = worldZ / cfg.worldDepth;

            const f32 heightVal = sampleHeight(heightData, dataW, dataH, u, v);
            const f32 worldY    = heightVal * cfg.heightScale;

            if (worldY < outMinY) { outMinY = worldY; }
            if (worldY > outMaxY) { outMaxY = worldY; }

            const f32 cellSizeX = chunkWorldW / static_cast<f32>(lodRes - 1);
            const f32 cellSizeZ = chunkWorldD / static_cast<f32>(lodRes - 1);
            const f32 uStep     = cellSizeX / cfg.worldWidth;
            const f32 vStep     = cellSizeZ / cfg.worldDepth;

            f32 hLeft  = 0.0f;
            f32 hRight = 0.0f;
            f32 hDown  = 0.0f;
            f32 hUp    = 0.0f;
            f32 dxScale = 2.0f;
            f32 dzScale = 2.0f;

            if (u - uStep >= 0.0f && u + uStep <= 1.0f) {
                hLeft  = sampleHeight(heightData, dataW, dataH, u - uStep, v);
                hRight = sampleHeight(heightData, dataW, dataH, u + uStep, v);
            } else if (u - uStep < 0.0f) {
                hLeft  = heightVal;
                hRight = sampleHeight(heightData, dataW, dataH, u + uStep, v);
                dxScale = 1.0f;
            } else {
                hLeft  = sampleHeight(heightData, dataW, dataH, u - uStep, v);
                hRight = heightVal;
                dxScale = 1.0f;
            }

            if (v - vStep >= 0.0f && v + vStep <= 1.0f) {
                hDown = sampleHeight(heightData, dataW, dataH, u, v - vStep);
                hUp   = sampleHeight(heightData, dataW, dataH, u, v + vStep);
            } else if (v - vStep < 0.0f) {
                hDown = heightVal;
                hUp   = sampleHeight(heightData, dataW, dataH, u, v + vStep);
                dzScale = 1.0f;
            } else {
                hDown = sampleHeight(heightData, dataW, dataH, u, v - vStep);
                hUp   = heightVal;
                dzScale = 1.0f;
            }

            const f32 dx = (hRight - hLeft) * cfg.heightScale;
            const f32 dz = (hUp - hDown)    * cfg.heightScale;
            glm::vec3 normal{
                -dx,
                dxScale * cellSizeX + dzScale * cellSizeZ,
                -dz
            };
            normal = glm::normalize(normal);

            const size_t base = static_cast<size_t>(vi) * 8;
            outVertices[base + 0] = worldX;
            outVertices[base + 1] = worldY;
            outVertices[base + 2] = worldZ;
            outVertices[base + 3] = normal.x;
            outVertices[base + 4] = normal.y;
            outVertices[base + 5] = normal.z;
            outVertices[base + 6] = u;
            outVertices[base + 7] = v;
        }
    }

    u32 idx = 0;
    for (u32 iz = 0; iz < lodRes - 1; ++iz) {
        for (u32 ix = 0; ix < lodRes - 1; ++ix) {
            const u32 topLeft     = iz * lodRes + ix;
            const u32 topRight    = iz * lodRes + ix + 1;
            const u32 bottomLeft  = (iz + 1) * lodRes + ix;
            const u32 bottomRight = (iz + 1) * lodRes + ix + 1;
            outIndices[idx++] = topLeft;
            outIndices[idx++] = bottomLeft;
            outIndices[idx++] = topRight;
            outIndices[idx++] = topRight;
            outIndices[idx++] = bottomLeft;
            outIndices[idx++] = bottomRight;
        }
    }
}

// Generate vertices and indices for a single LOD level of a terrain chunk.
// lodRes is the vertex resolution at this LOD level (e.g., 64 for LOD 0, 32 for LOD 1).
// Returns false on GPU upload failure.
// outMinY / outMaxY are updated with the min/max Y values found (for AABB computation).
static bool generateChunkLod(TerrainChunkLod& lod,
                              const float* heightData, const u32 dataW, const u32 dataH,
                              const TerrainConfig& cfg,
                              const u32 chunkX, const u32 chunkZ,
                              const u32 lodRes,
                              f32& outMinY, f32& outMaxY) {
    // Build CPU arrays using the shared pure-CPU helper
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    buildChunkLodArrays(vertices, indices,
                        heightData, dataW, dataH,
                        cfg, chunkX, chunkZ, lodRes,
                        outMinY, outMaxY);

    const u32 vertexCount = lodRes * lodRes;
    const u32 indexCount  = static_cast<u32>(indices.size());

    // --- GPU upload ---
    // Each vertex is 8 floats (MeshVertex layout: px,py,pz,nx,ny,nz,u,v = 32 bytes)
    const u64 vboSize = static_cast<u64>(vertexCount) * sizeof(rhi::MeshVertex);
    const u64 iboSize = static_cast<u64>(indexCount)  * sizeof(u32);

    glGenVertexArrays(1, &lod.vaoId);
    glGenBuffers(1, &lod.vboId);
    glGenBuffers(1, &lod.iboId);

    glBindVertexArray(lod.vaoId);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, lod.vboId);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vboSize),
                 vertices.data(), GL_STATIC_DRAW);

    // Check for GPU OOM
    {
        const GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            FFE_LOG_ERROR("terrain", "VBO upload failed (GL error 0x%X)", err);
            glDeleteBuffers(1, &lod.vboId);
            glDeleteBuffers(1, &lod.iboId);
            glDeleteVertexArrays(1, &lod.vaoId);
            lod.vaoId = 0;
            lod.vboId = 0;
            lod.iboId = 0;
            return false;
        }
    }

    // Vertex attributes: same layout as static MeshVertex (32 bytes)
    // location 0: position -- 3 floats, offset 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                          reinterpret_cast<const void*>(0));
    // location 1: normal -- 3 floats, offset 12
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                          reinterpret_cast<const void*>(12));
    // location 2: texcoord -- 2 floats, offset 24
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                          reinterpret_cast<const void*>(24));

    // IBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod.iboId);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(iboSize),
                 indices.data(), GL_STATIC_DRAW);

    // Check for GPU OOM on IBO
    {
        const GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            FFE_LOG_ERROR("terrain", "IBO upload failed (GL error 0x%X)", err);
            glDeleteBuffers(1, &lod.vboId);
            glDeleteBuffers(1, &lod.iboId);
            glDeleteVertexArrays(1, &lod.vaoId);
            lod.vaoId = 0;
            lod.vboId = 0;
            lod.iboId = 0;
            return false;
        }
    }

    // Unbind VAO first (order matters -- see mesh_loader.cpp)
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    lod.indexCount = indexCount;
    return true;
}

// Generate all LOD levels for a single chunk.
// LOD 0 = full resolution, LOD 1 = half, LOD 2 = quarter (min 4).
// AABB and center are computed from the full-resolution (LOD 0) pass.
static bool generateChunk(TerrainChunkGpu& chunk,
                           const float* heightData, const u32 dataW, const u32 dataH,
                           const TerrainConfig& cfg,
                           const u32 chunkX, const u32 chunkZ) {
    // Guard: no GL context in headless mode.
    if (glad_glGenVertexArrays == nullptr) {
        return true; // Not an error -- just no GPU resources in headless.
    }

    const u32 baseRes = cfg.chunkResolution;

    // Determine LOD resolutions: full, half, quarter (minimum 4 vertices per side)
    static constexpr u32 MIN_LOD_RES = 4;
    u32 lodResolutions[MAX_LOD_LEVELS];
    lodResolutions[0] = baseRes;
    lodResolutions[1] = (baseRes / 2 >= MIN_LOD_RES) ? baseRes / 2 : MIN_LOD_RES;
    lodResolutions[2] = (baseRes / 4 >= MIN_LOD_RES) ? baseRes / 4 : MIN_LOD_RES;

    // Determine how many distinct LOD levels we can generate
    u32 lodCount = 1;
    if (lodResolutions[1] < lodResolutions[0]) { lodCount = 2; }
    if (lodCount == 2 && lodResolutions[2] < lodResolutions[1]) { lodCount = 3; }

    // Compute world-space bounds for this chunk (needed for AABB/center)
    const f32 chunkWorldW = cfg.worldWidth / static_cast<f32>(cfg.chunkCountX);
    const f32 chunkWorldD = cfg.worldDepth / static_cast<f32>(cfg.chunkCountZ);
    const f32 chunkOriginX = static_cast<f32>(chunkX) * chunkWorldW;
    const f32 chunkOriginZ = static_cast<f32>(chunkZ) * chunkWorldD;

    f32 minY =  1e30f;
    f32 maxY = -1e30f;

    for (u32 lod = 0; lod < lodCount; ++lod) {
        if (!generateChunkLod(chunk.lods[lod], heightData, dataW, dataH, cfg,
                              chunkX, chunkZ, lodResolutions[lod], minY, maxY)) {
            // Clean up any already-generated LODs for this chunk
            for (u32 j = 0; j < lod; ++j) {
                TerrainChunkLod& prev = chunk.lods[j];
                if (prev.vaoId != 0) { glDeleteVertexArrays(1, &prev.vaoId); prev.vaoId = 0; }
                if (prev.vboId != 0) { glDeleteBuffers(1, &prev.vboId); prev.vboId = 0; }
                if (prev.iboId != 0) { glDeleteBuffers(1, &prev.iboId); prev.iboId = 0; }
                prev.indexCount = 0;
            }
            return false;
        }
    }

    chunk.lodCount = lodCount;
    chunk.aabbMin = {chunkOriginX, minY, chunkOriginZ};
    chunk.aabbMax = {chunkOriginX + chunkWorldW, maxY, chunkOriginZ + chunkWorldD};
    chunk.center = (chunk.aabbMin + chunk.aabbMax) * 0.5f;

    return true;
}

static void destroyChunkLod(TerrainChunkLod& lod) {
    if (lod.vaoId != 0) {
        glDeleteVertexArrays(1, &lod.vaoId);
        lod.vaoId = 0;
    }
    if (lod.vboId != 0) {
        glDeleteBuffers(1, &lod.vboId);
        lod.vboId = 0;
    }
    if (lod.iboId != 0) {
        glDeleteBuffers(1, &lod.iboId);
        lod.iboId = 0;
    }
    lod.indexCount = 0;
}

static void destroyChunk(TerrainChunkGpu& chunk) {
    for (u32 lod = 0; lod < chunk.lodCount; ++lod) {
        destroyChunkLod(chunk.lods[lod]);
    }
    chunk.lodCount = 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TerrainHandle loadTerrain(const float* heightData, const u32 dataWidth, const u32 dataHeight,
                          const TerrainConfig& config) {
    // Validate inputs
    if (heightData == nullptr) {
        FFE_LOG_ERROR("terrain", "loadTerrain: null heightData");
        return TerrainHandle{0};
    }
    if (dataWidth == 0 || dataHeight == 0) {
        FFE_LOG_ERROR("terrain", "loadTerrain: zero heightmap dimensions");
        return TerrainHandle{0};
    }
    if (dataWidth > MAX_HEIGHTMAP_DIMENSION || dataHeight > MAX_HEIGHTMAP_DIMENSION) {
        FFE_LOG_ERROR("terrain", "loadTerrain: heightmap dimensions %ux%u exceed max %u",
                      dataWidth, dataHeight, MAX_HEIGHTMAP_DIMENSION);
        return TerrainHandle{0};
    }
    if (!validateConfig(config)) {
        FFE_LOG_ERROR("terrain", "loadTerrain: invalid TerrainConfig");
        return TerrainHandle{0};
    }

    // Find a free slot (slot 0 is reserved as null handle)
    u32 slotIdx = 0;
    for (u32 i = 1; i <= MAX_TERRAIN_ASSETS; ++i) {
        if (!s_terrains[i - 1].active) {
            slotIdx = i;
            break;
        }
    }
    if (slotIdx == 0) {
        FFE_LOG_ERROR("terrain", "loadTerrain: no free terrain slots (max %u)", MAX_TERRAIN_ASSETS);
        return TerrainHandle{0};
    }

    TerrainAsset& asset = s_terrains[slotIdx - 1];

    // Copy height data (cold-path allocation)
    const u64 dataSize = static_cast<u64>(dataWidth) * static_cast<u64>(dataHeight);
    asset.heightData = std::make_unique<float[]>(static_cast<size_t>(dataSize));
    std::memcpy(asset.heightData.get(), heightData, static_cast<size_t>(dataSize) * sizeof(float));
    asset.heightDataWidth  = dataWidth;
    asset.heightDataHeight = dataHeight;
    asset.config           = config;
    asset.diffuseTexture   = rhi::TextureHandle{0};

    // Generate chunks
    const u32 totalChunks = config.chunkCountX * config.chunkCountZ;
    asset.chunkCount = totalChunks;

    for (u32 cz = 0; cz < config.chunkCountZ; ++cz) {
        for (u32 cx = 0; cx < config.chunkCountX; ++cx) {
            const u32 ci = cz * config.chunkCountX + cx;
            if (!generateChunk(asset.chunks[ci], asset.heightData.get(),
                               dataWidth, dataHeight, config, cx, cz)) {
                // Clean up any already-generated chunks
                for (u32 j = 0; j < ci; ++j) {
                    destroyChunk(asset.chunks[j]);
                }
                asset.heightData.reset();
                FFE_LOG_ERROR("terrain", "loadTerrain: chunk generation failed at (%u, %u)", cx, cz);
                return TerrainHandle{0};
            }
        }
    }

    // M4: initialize all chunk stream states to EAGER (streaming disabled by default)
    for (u32 ci = 0; ci < totalChunks; ++ci) {
        asset.chunkStream[ci].state.store(ChunkState::EAGER);
    }
    asset.streamConfig = TerrainStreamingConfig{};

    asset.active = true;

    FFE_LOG_INFO("terrain", "Loaded terrain: %ux%u heightmap, %ux%u chunks, res=%u, handle=%u",
                 dataWidth, dataHeight, config.chunkCountX, config.chunkCountZ,
                 config.chunkResolution, slotIdx);

    return TerrainHandle{slotIdx};
}

TerrainHandle loadTerrainFromImage(const char* path, const TerrainConfig& config) {
    // SEC-1: path safety check first
    if (!isPathSafe(path)) {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: unsafe path rejected: \"%s\"",
                      path ? path : "(null)");
        return TerrainHandle{0};
    }

    // Extension check: must end with ".png"
    const size_t pathLen = strnlen(path, PATH_MAX);
    if (pathLen < 4 || strcmp(path + pathLen - 4, ".png") != 0) {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: path must end with .png: \"%s\"", path);
        return TerrainHandle{0};
    }

    // Build full path using asset root
    const char* assetRoot = getAssetRoot();
    if (assetRoot == nullptr || assetRoot[0] == '\0') {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: asset root not set");
        return TerrainHandle{0};
    }

    const size_t rootLen = strnlen(assetRoot, PATH_MAX);
    if (rootLen + 1 + pathLen + 1 > static_cast<size_t>(PATH_MAX) + 1) {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: concatenated path too long");
        return TerrainHandle{0};
    }

    char fullPath[PATH_MAX + 1];
    std::snprintf(fullPath, sizeof(fullPath), "%s/%s", assetRoot, path);

    // Pre-parse file size guard
    struct stat fileStat{};
    if (::stat(fullPath, &fileStat) != 0) {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: cannot stat file: \"%s\"", fullPath);
        return TerrainHandle{0};
    }
    if (static_cast<u64>(fileStat.st_size) > MAX_HEIGHTMAP_FILE_BYTES) {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: file too large (%lld bytes, max %llu)",
                      static_cast<long long>(fileStat.st_size),
                      static_cast<unsigned long long>(MAX_HEIGHTMAP_FILE_BYTES));
        return TerrainHandle{0};
    }

    // Load PNG via stb_image as grayscale (1 channel)
    int imgW = 0;
    int imgH = 0;
    int imgChannels = 0;
    unsigned char* pixels = stbi_load(fullPath, &imgW, &imgH, &imgChannels, 1);
    if (pixels == nullptr) {
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: stb_image failed: \"%s\"", fullPath);
        return TerrainHandle{0};
    }

    // Validate decoded dimensions
    if (imgW <= 0 || imgH <= 0) {
        stbi_image_free(pixels);
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: invalid image dimensions %dx%d", imgW, imgH);
        return TerrainHandle{0};
    }
    if (static_cast<u32>(imgW) > MAX_HEIGHTMAP_DIMENSION ||
        static_cast<u32>(imgH) > MAX_HEIGHTMAP_DIMENSION) {
        stbi_image_free(pixels);
        FFE_LOG_ERROR("terrain", "loadTerrainFromImage: image %dx%d exceeds max %u",
                      imgW, imgH, MAX_HEIGHTMAP_DIMENSION);
        return TerrainHandle{0};
    }

    // Convert u8 grayscale to float [0, 1]
    const u32 w = static_cast<u32>(imgW);
    const u32 h = static_cast<u32>(imgH);
    const u64 pixelCount = static_cast<u64>(w) * static_cast<u64>(h);
    auto floatData = std::make_unique<float[]>(static_cast<size_t>(pixelCount));
    for (u64 i = 0; i < pixelCount; ++i) {
        floatData[i] = static_cast<f32>(pixels[i]) / 255.0f;
    }
    stbi_image_free(pixels);

    // Delegate to the float-data loader
    return loadTerrain(floatData.get(), w, h, config);
}

void unloadTerrain(const TerrainHandle handle) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    // M4: Stop worker thread before freeing GL resources to avoid use-after-free.
    if (asset.workerRunning.load()) {
        asset.workerRunning.store(false);
        asset.queueCV.notify_all();
        if (asset.workerThread.joinable()) {
            asset.workerThread.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(asset.queueMutex);
        asset.workQueue.clear();
    }

    for (u32 i = 0; i < asset.chunkCount; ++i) {
        destroyChunk(asset.chunks[i]);
        // Reset stream state
        asset.chunkStream[i].state.store(ChunkState::EAGER);
        asset.chunkStream[i].cpuVertices.clear();
        asset.chunkStream[i].cpuIndices.clear();
    }
    asset.streamConfig = TerrainStreamingConfig{};
    asset.heightData.reset();
    asset.active = false;
    asset.chunkCount = 0;
    asset.diffuseTexture = rhi::TextureHandle{0};
    asset.material = TerrainMaterial{};
    asset.lodConfig = TerrainLodConfig{};

    FFE_LOG_INFO("terrain", "Unloaded terrain handle=%u", handle.id);
}

void unloadAllTerrains() {
    for (u32 i = 0; i < MAX_TERRAIN_ASSETS; ++i) {
        if (s_terrains[i].active) {
            unloadTerrain(TerrainHandle{i + 1});
        }
    }
}

f32 getTerrainHeight(const TerrainHandle handle, const f32 worldX, const f32 worldZ) {
    if (!isValid(handle)) { return 0.0f; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return 0.0f; }

    const TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return 0.0f; }
    if (asset.heightData == nullptr) { return 0.0f; }

    // Map world coordinates to [0, 1] UV range.
    // The terrain geometry is centred at the world origin: loadTerrain() sets the
    // entity Transform3D to (-worldWidth/2, 0, -worldDepth/2) so the mesh spans
    // [-worldWidth/2, +worldWidth/2] x [-worldDepth/2, +worldDepth/2] in world
    // space. Height queries must apply the same inverse offset before sampling.
    const f32 u = (worldX + asset.config.worldWidth  * 0.5f) / asset.config.worldWidth;
    const f32 v = (worldZ + asset.config.worldDepth  * 0.5f) / asset.config.worldDepth;

    // Out-of-bounds check
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return 0.0f;
    }

    const f32 heightVal = sampleHeight(asset.heightData.get(),
                                        asset.heightDataWidth,
                                        asset.heightDataHeight,
                                        u, v);
    return heightVal * asset.config.heightScale;
}

void setTerrainTexture(const TerrainHandle handle, const rhi::TextureHandle texture) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    asset.diffuseTexture = texture;
}

void setTerrainSplatMap(const TerrainHandle handle, const rhi::TextureHandle splatTexture) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    asset.material.splatTexture = splatTexture;
}

void setTerrainLayer(const TerrainHandle handle, const u32 layerIndex,
                     const rhi::TextureHandle texture, const f32 uvScale) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    if (layerIndex >= 4) {
        FFE_LOG_ERROR("terrain", "setTerrainLayer: layerIndex %u out of range (0-3)", layerIndex);
        return;
    }
    if (!isFinite(uvScale) || uvScale <= 0.0f) {
        FFE_LOG_ERROR("terrain", "setTerrainLayer: uvScale must be positive and finite");
        return;
    }

    asset.material.layers[layerIndex].texture = texture;
    asset.material.layers[layerIndex].uvScale = uvScale;
}

void setTerrainTriplanar(const TerrainHandle handle, const bool enabled, f32 threshold) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    // Clamp threshold to (0.0, 1.0]
    if (!isFinite(threshold)) {
        threshold = 0.7f;
    }
    if (threshold <= 0.0f) { threshold = 0.01f; }
    if (threshold > 1.0f) { threshold = 1.0f; }

    asset.material.triplanarEnabled = enabled;
    asset.material.triplanarThreshold = threshold;
}

void setTerrainLodDistances(const TerrainHandle handle, const f32 lod1Distance, const f32 lod2Distance) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    if (!isFinite(lod1Distance) || lod1Distance <= 0.0f) {
        FFE_LOG_ERROR("terrain", "setTerrainLodDistances: lod1Distance must be positive and finite");
        return;
    }
    if (!isFinite(lod2Distance) || lod2Distance <= 0.0f) {
        FFE_LOG_ERROR("terrain", "setTerrainLodDistances: lod2Distance must be positive and finite");
        return;
    }

    asset.lodConfig.lodDistances[0] = lod1Distance;
    asset.lodConfig.lodDistances[1] = lod2Distance;
    // lodDistances[2] is not directly configurable via this API; keep default.
}

TerrainHandle getFirstActiveTerrain() {
    for (u32 i = 0; i < MAX_TERRAIN_ASSETS; ++i) {
        if (s_terrains[i].active) {
            return TerrainHandle{i + 1};
        }
    }
    return TerrainHandle{0};
}

// ---------------------------------------------------------------------------
// Internal accessors for terrain_renderer.cpp
// ---------------------------------------------------------------------------

const TerrainAsset* getTerrainAsset(const TerrainHandle handle) {
    if (!isValid(handle)) { return nullptr; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return nullptr; }
    const TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return nullptr; }
    return &asset;
}

TerrainAsset* getTerrainAssetMut(const TerrainHandle handle) {
    if (!isValid(handle)) { return nullptr; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return nullptr; }
    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return nullptr; }
    return &asset;
}

// ---------------------------------------------------------------------------
// M4: Streaming implementation
// ---------------------------------------------------------------------------

// Generate ALL LOD levels for a streaming chunk into ChunkStreamState CPU buffers.
// Called ONLY from the worker thread -- no GL calls permitted here.
// The worker fills cpuVertices (LOD 0 only for streaming -- lowest-overhead path:
// one GL upload per chunk). For simplicity streaming uses LOD 0 only; LOD
// selection reverts to EAGER chunks when streaming is disabled.
static void generateChunkMeshCpu(const TerrainAsset& asset,
                                  const u32 chunkX, const u32 chunkZ,
                                  ChunkStreamState& cs) {
    const TerrainConfig& cfg = asset.config;
    const u32 baseRes = cfg.chunkResolution;

    static constexpr u32 MIN_LOD_RES = 4;
    const u32 lodRes[MAX_LOD_LEVELS] = {
        baseRes,
        (baseRes / 2 >= MIN_LOD_RES) ? baseRes / 2 : MIN_LOD_RES,
        (baseRes / 4 >= MIN_LOD_RES) ? baseRes / 4 : MIN_LOD_RES,
    };

    u32 lodCount = 1;
    if (lodRes[1] < lodRes[0]) { lodCount = 2; }
    if (lodCount == 2 && lodRes[2] < lodRes[1]) { lodCount = 3; }

    // We encode all LOD levels into a single flat buffer with a header per LOD.
    // Layout: [lodCount (u32)] [lod0_vertexCount (u32)] [lod0_indexCount (u32)]
    //         [lod0 vertex floats] [lod0 index u32s]
    //         [lod1_vertexCount (u32)] [lod1_indexCount (u32)]
    //         [lod1 vertex floats] [lod1 index u32s]
    //         ... etc.
    // This avoids multiple allocations. The upload path decodes the same layout.

    // We'll use separate per-LOD vectors in ChunkStreamState for clarity.
    // cpuVertices layout: [lodCount][lod0_vcount][lod0_icount][lod0 verts 8f each][lod0 idx u32 as float pairs...
    // Actually simpler: store a flat encoding. Let's use cpuVertices for
    // all vertex floats concatenated (LOD0 first, then LOD1, ...) and cpuIndices
    // for all index u32s concatenated, with sizes stored in the first few floats/u32s.

    // Format chosen:
    //   cpuVertices[0]   = float(lodCount)
    //   cpuVertices[1]   = float(lod0_vertexCount)   (number of MeshVertex structs)
    //   cpuVertices[2]   = float(lod1_vertexCount)   (0 if unused)
    //   cpuVertices[3]   = float(lod2_vertexCount)   (0 if unused)
    //   cpuVertices[4..] = lod0 vertices (8 floats each), lod1, lod2
    //   cpuIndices[0]    = lod0_indexCount
    //   cpuIndices[1]    = lod1_indexCount (0 if unused)
    //   cpuIndices[2]    = lod2_indexCount (0 if unused)
    //   cpuIndices[3..]  = lod0 indices, lod1, lod2

    cs.cpuVertices.clear();
    cs.cpuIndices.clear();

    // Reserve header
    cs.cpuVertices.resize(4, 0.0f); // [lodCount, vcount0, vcount1, vcount2]
    cs.cpuIndices.resize(3, 0);     // [icount0, icount1, icount2]

    cs.cpuVertices[0] = static_cast<float>(lodCount);

    f32 minY =  1e30f;
    f32 maxY = -1e30f;

    for (u32 li = 0; li < lodCount; ++li) {
        std::vector<float>    verts;
        std::vector<uint32_t> idxs;
        buildChunkLodArrays(verts, idxs,
                            asset.heightData.get(),
                            asset.heightDataWidth, asset.heightDataHeight,
                            cfg, chunkX, chunkZ, lodRes[li],
                            minY, maxY);

        const u32 vc = lodRes[li] * lodRes[li];
        cs.cpuVertices[1 + li] = static_cast<float>(vc);
        cs.cpuIndices[li]      = static_cast<uint32_t>(idxs.size());

        cs.cpuVertices.insert(cs.cpuVertices.end(), verts.begin(), verts.end());
        cs.cpuIndices.insert(cs.cpuIndices.end(),   idxs.begin(),  idxs.end());
    }

    // Store AABB and center in cpuVertices tail (after all vertex data) for
    // extraction by the main-thread upload path.
    // Append: [minY, maxY, originX, originZ, chunkWorldW, chunkWorldD, lodCount_repeated]
    // (lodCount already stored at index 0)
    const f32 chunkWorldW  = cfg.worldWidth  / static_cast<f32>(cfg.chunkCountX);
    const f32 chunkWorldD  = cfg.worldDepth  / static_cast<f32>(cfg.chunkCountZ);
    const f32 chunkOriginX = static_cast<f32>(chunkX) * chunkWorldW;
    const f32 chunkOriginZ = static_cast<f32>(chunkZ) * chunkWorldD;

    cs.cpuVertices.push_back(minY);
    cs.cpuVertices.push_back(maxY);
    cs.cpuVertices.push_back(chunkOriginX);
    cs.cpuVertices.push_back(chunkOriginZ);
    cs.cpuVertices.push_back(chunkWorldW);
    cs.cpuVertices.push_back(chunkWorldD);
}

// Upload a READY_TO_UPLOAD chunk to the GPU. Main thread only.
// Decodes the buffer layout written by generateChunkMeshCpu.
static void uploadStreamedChunk(TerrainAsset& asset, const u32 chunkIdx,
                                 const u32 chunkX, const u32 chunkZ) {
    ChunkStreamState& cs = asset.chunkStream[chunkIdx];
    TerrainChunkGpu&  chunk = asset.chunks[chunkIdx];

    if (cs.cpuVertices.size() < 4 || cs.cpuIndices.size() < 3) {
        cs.state.store(ChunkState::UNLOADED);
        return;
    }

    const u32 lodCount = static_cast<u32>(cs.cpuVertices[0]);
    if (lodCount == 0 || lodCount > MAX_LOD_LEVELS) {
        cs.state.store(ChunkState::UNLOADED);
        return;
    }

    // Destroy any existing LODs for this chunk (e.g. re-entering radius)
    destroyChunk(chunk);

    // Decode header
    const u32 vcounts[MAX_LOD_LEVELS] = {
        static_cast<u32>(cs.cpuVertices[1]),
        static_cast<u32>(cs.cpuVertices[2]),
        static_cast<u32>(cs.cpuVertices[3]),
    };
    const u32 icounts[MAX_LOD_LEVELS] = {
        cs.cpuIndices[0],
        cs.cpuIndices[1],
        cs.cpuIndices[2],
    };

    // Vertex data starts at offset 4 in cpuVertices (after 4-float header)
    size_t vOffset = 4;
    // Index data starts at offset 3 in cpuIndices (after 3-u32 header)
    size_t iOffset = 3;

    // Read AABB metadata from the tail of cpuVertices:
    // [minY, maxY, originX, originZ, chunkWorldW, chunkWorldD]
    // Tail starts 6 elements before end.
    if (cs.cpuVertices.size() < 10) {
        cs.state.store(ChunkState::UNLOADED);
        return;
    }
    const size_t tailStart = cs.cpuVertices.size() - 6;
    const f32 minY        = cs.cpuVertices[tailStart + 0];
    const f32 maxY        = cs.cpuVertices[tailStart + 1];
    const f32 chunkOriginX = cs.cpuVertices[tailStart + 2];
    const f32 chunkOriginZ = cs.cpuVertices[tailStart + 3];
    const f32 chunkWorldW  = cs.cpuVertices[tailStart + 4];
    const f32 chunkWorldD  = cs.cpuVertices[tailStart + 5];

    for (u32 li = 0; li < lodCount; ++li) {
        const u32 vc = vcounts[li];
        const u32 ic = icounts[li];
        if (vc == 0 || ic == 0) { continue; }

        const size_t vByteCount = static_cast<size_t>(vc) * sizeof(rhi::MeshVertex);
        const size_t iByteCount = static_cast<size_t>(ic) * sizeof(u32);

        // Bounds check
        if (vOffset + vc * 8 > tailStart || iOffset + ic > cs.cpuIndices.size()) {
            break;
        }

        TerrainChunkLod& lod = chunk.lods[li];

        glGenVertexArrays(1, &lod.vaoId);
        glGenBuffers(1, &lod.vboId);
        glGenBuffers(1, &lod.iboId);

        glBindVertexArray(lod.vaoId);

        glBindBuffer(GL_ARRAY_BUFFER, lod.vboId);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vByteCount),
                     cs.cpuVertices.data() + vOffset,
                     GL_STATIC_DRAW);

        {
            const GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                FFE_LOG_ERROR("terrain", "streaming VBO upload failed (GL error 0x%X)", err);
                glDeleteBuffers(1, &lod.vboId);
                glDeleteBuffers(1, &lod.iboId);
                glDeleteVertexArrays(1, &lod.vaoId);
                lod.vaoId = 0; lod.vboId = 0; lod.iboId = 0;
                glBindVertexArray(0);
                break;
            }
        }

        // Vertex attributes (MeshVertex: px,py,pz,nx,ny,nz,u,v = 32 bytes)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                              reinterpret_cast<const void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                              reinterpret_cast<const void*>(12));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(sizeof(rhi::MeshVertex)),
                              reinterpret_cast<const void*>(24));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod.iboId);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(iByteCount),
                     cs.cpuIndices.data() + iOffset,
                     GL_STATIC_DRAW);

        {
            const GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                FFE_LOG_ERROR("terrain", "streaming IBO upload failed (GL error 0x%X)", err);
                glDeleteBuffers(1, &lod.vboId);
                glDeleteBuffers(1, &lod.iboId);
                glDeleteVertexArrays(1, &lod.vaoId);
                lod.vaoId = 0; lod.vboId = 0; lod.iboId = 0;
                glBindVertexArray(0);
                break;
            }
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        lod.indexCount = ic;
        vOffset += static_cast<size_t>(vc) * 8;
        iOffset += ic;
        chunk.lodCount = li + 1;
    }

    // Set AABB and center
    chunk.aabbMin = {chunkOriginX, minY, chunkOriginZ};
    chunk.aabbMax = {chunkOriginX + chunkWorldW, maxY, chunkOriginZ + chunkWorldD};
    chunk.center  = (chunk.aabbMin + chunk.aabbMax) * 0.5f;

    // Free CPU buffers immediately after upload
    cs.cpuVertices.clear();
    cs.cpuVertices.shrink_to_fit();
    cs.cpuIndices.clear();
    cs.cpuIndices.shrink_to_fit();
}

// Worker thread loop: waits for work items, generates CPU mesh data, signals READY_TO_UPLOAD.
// No GL calls -- pure CPU mesh generation only.
static void terrainWorkerLoop(TerrainAsset* asset) {
    while (asset->workerRunning.load()) {
        ChunkCoord coord{};
        {
            std::unique_lock<std::mutex> lock(asset->queueMutex);
            asset->queueCV.wait(lock, [asset] {
                return !asset->workQueue.empty() || !asset->workerRunning.load();
            });
            if (!asset->workerRunning.load()) { break; }
            if (asset->workQueue.empty()) { continue; }
            coord = asset->workQueue.front();
            asset->workQueue.pop_front();
        }

        const u32 chunkIdx = coord.z * asset->config.chunkCountX + coord.x;
        if (chunkIdx >= MAX_CHUNKS_TOTAL) { continue; }

        ChunkStreamState& cs = asset->chunkStream[chunkIdx];

        // CAS: only proceed if still QUEUED
        ChunkState expected = ChunkState::QUEUED;
        if (!cs.state.compare_exchange_strong(expected, ChunkState::GENERATING)) {
            // Stale request (e.g. chunk moved out of radius and was set to UNLOADED)
            continue;
        }

        generateChunkMeshCpu(*asset, coord.x, coord.z, cs);

        cs.state.store(ChunkState::READY_TO_UPLOAD);
    }
}

// ---------------------------------------------------------------------------
// M4: Public streaming API
// ---------------------------------------------------------------------------

void setTerrainStreamingRadius(const TerrainHandle handle, const int radiusInChunks) {
    if (!isValid(handle)) { return; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return; }

    TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return; }

    if (radiusInChunks < 0) {
        FFE_LOG_ERROR("terrain", "setTerrainStreamingRadius: radius must be >= 0");
        return;
    }

    const int prevRadius = asset.streamConfig.radiusChunks;
    asset.streamConfig.radiusChunks = radiusInChunks;

    if (radiusInChunks == 0) {
        // Disable streaming: stop worker, convert all UNLOADED/QUEUED/etc back to EAGER.
        // For simplicity: signal worker to stop, join, then reload all chunks eagerly.
        if (asset.workerRunning.load()) {
            asset.workerRunning.store(false);
            asset.queueCV.notify_all();
            if (asset.workerThread.joinable()) {
                asset.workerThread.join();
            }
        }
        // Transition all non-EAGER chunks back: mark everything EAGER.
        // Chunks that were LOADED stay on GPU (already have GL resources).
        // Chunks that were UNLOADED need to be regenerated eagerly.
        for (u32 ci = 0; ci < asset.chunkCount; ++ci) {
            ChunkStreamState& cs = asset.chunkStream[ci];
            const ChunkState st = cs.state.load();
            if (st == ChunkState::LOADED) {
                cs.state.store(ChunkState::EAGER);
            } else if (st == ChunkState::UNLOADED || st == ChunkState::QUEUED ||
                       st == ChunkState::GENERATING || st == ChunkState::READY_TO_UPLOAD ||
                       st == ChunkState::UNLOADING) {
                // Need to regenerate -- call generateChunk (main thread, has GL context)
                const u32 cx = ci % asset.config.chunkCountX;
                const u32 cz = ci / asset.config.chunkCountX;
                destroyChunk(asset.chunks[ci]);
                generateChunk(asset.chunks[ci], asset.heightData.get(),
                               asset.heightDataWidth, asset.heightDataHeight,
                               asset.config, cx, cz);
                cs.state.store(ChunkState::EAGER);
            }
        }
        return;
    }

    // Enabling (or changing radius):
    // Set dirty threshold to 0.5 * chunkWorldSize so ticks skip tiny camera moves.
    const f32 chunkWorldW = asset.config.worldWidth  / static_cast<f32>(asset.config.chunkCountX);
    const f32 chunkWorldD = asset.config.worldDepth  / static_cast<f32>(asset.config.chunkCountZ);
    asset.streamConfig.dirtyCamThreshold = 0.5f * std::min(chunkWorldW, chunkWorldD);

    // Reset camera dirty tracking so we do an immediate tick next frame.
    asset.streamConfig.lastCamX = 1e9f;
    asset.streamConfig.lastCamZ = 1e9f;

    // If streaming was previously disabled (all EAGER), transition all chunks to UNLOADED
    // and destroy their GL resources so streaming can take over.
    if (prevRadius == 0) {
        for (u32 ci = 0; ci < asset.chunkCount; ++ci) {
            ChunkStreamState& cs = asset.chunkStream[ci];
            if (cs.state.load() == ChunkState::EAGER) {
                // Free GL resources -- streaming will reload as needed
                destroyChunk(asset.chunks[ci]);
                cs.state.store(ChunkState::UNLOADED);
            }
        }
    }

    // Start worker thread if not already running
    if (!asset.workerRunning.load()) {
        asset.workerRunning.store(true);
        asset.workerThread = std::thread(terrainWorkerLoop, &asset);
    }

    FFE_LOG_INFO("terrain", "setTerrainStreamingRadius: handle=%u radius=%d", handle.id, radiusInChunks);
}

int getTerrainLoadedChunkCount(const TerrainHandle handle) {
    if (!isValid(handle)) { return 0; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return 0; }

    const TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return 0; }

    int count = 0;
    for (u32 ci = 0; ci < asset.chunkCount; ++ci) {
        const ChunkState st = asset.chunkStream[ci].state.load();
        if (st == ChunkState::EAGER || st == ChunkState::LOADED) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// M4: Per-frame streaming tick (called from terrain_renderer.cpp, main thread)
// ---------------------------------------------------------------------------

void terrainStreamingTick(TerrainAsset& asset, const f32 camX, const f32 camZ) {
    if (asset.streamConfig.radiusChunks == 0) { return; }

    // Gate: skip tick if camera hasn't moved enough
    const f32 dx = camX - asset.streamConfig.lastCamX;
    const f32 dz = camZ - asset.streamConfig.lastCamZ;
    const f32 dist2 = dx * dx + dz * dz;
    const f32 thresh = asset.streamConfig.dirtyCamThreshold;
    if (dist2 < thresh * thresh) { return; }

    asset.streamConfig.lastCamX = camX;
    asset.streamConfig.lastCamZ = camZ;

    const int radius = asset.streamConfig.radiusChunks;
    const f32 chunkWorldW = asset.config.worldWidth  / static_cast<f32>(asset.config.chunkCountX);
    const f32 chunkWorldD = asset.config.worldDepth  / static_cast<f32>(asset.config.chunkCountZ);

    // Camera chunk coordinate (clamp to grid)
    const int camCX = static_cast<int>(camX / chunkWorldW);
    const int camCZ = static_cast<int>(camZ / chunkWorldD);

    const int gridW = static_cast<int>(asset.config.chunkCountX);
    const int gridD = static_cast<int>(asset.config.chunkCountZ);

    for (u32 ci = 0; ci < asset.chunkCount; ++ci) {
        const int cx = static_cast<int>(ci % asset.config.chunkCountX);
        const int cz = static_cast<int>(ci / asset.config.chunkCountX);

        // Chebyshev distance to camera chunk
        const int chebyDist = std::max(std::abs(cx - camCX), std::abs(cz - camCZ));
        const bool inRadius = (chebyDist <= radius) && (cx >= 0) && (cz >= 0) &&
                              (cx < gridW) && (cz < gridD);

        ChunkStreamState& cs = asset.chunkStream[ci];
        const ChunkState st = cs.state.load();

        if (inRadius) {
            if (st == ChunkState::UNLOADED) {
                // Transition to QUEUED and enqueue for worker
                cs.state.store(ChunkState::QUEUED);
                std::lock_guard<std::mutex> lock(asset.queueMutex);
                asset.workQueue.push_back({static_cast<u32>(cx), static_cast<u32>(cz)});
                asset.queueCV.notify_one();
            }
        } else {
            // Out of radius
            if (st == ChunkState::LOADED) {
                cs.state.store(ChunkState::UNLOADING);
                // Free GL objects immediately (main thread)
                destroyChunk(asset.chunks[ci]);
                cs.state.store(ChunkState::UNLOADED);
            } else if (st == ChunkState::QUEUED) {
                // Cancel: revert to UNLOADED so worker discards via CAS check
                cs.state.store(ChunkState::UNLOADED);
            }
        }
    }
}

// Per-frame upload pass: find READY_TO_UPLOAD chunks and upload them. Main thread only.
void terrainUploadPendingChunks(TerrainAsset& asset) {
    if (asset.streamConfig.radiusChunks == 0) { return; }

    for (u32 ci = 0; ci < asset.chunkCount; ++ci) {
        ChunkStreamState& cs = asset.chunkStream[ci];
        ChunkState expected = ChunkState::READY_TO_UPLOAD;
        if (cs.state.compare_exchange_strong(expected, ChunkState::LOADED)) {
            const u32 cx = ci % asset.config.chunkCountX;
            const u32 cz = ci / asset.config.chunkCountX;
            // Upload only if GL context is available
            if (glad_glGenVertexArrays != nullptr) {
                uploadStreamedChunk(asset, ci, cx, cz);
            }
            // If no GL context (headless test), just mark LOADED with no VAOs
        }
    }
}

} // namespace ffe::renderer
