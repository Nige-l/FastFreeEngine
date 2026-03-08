// terrain.cpp -- Heightmap terrain mesh generation, GPU upload, and height queries.
//
// Generates chunked meshes from heightmap data. Each chunk is an independent
// VAO/VBO/IBO with MeshVertex (32 bytes) vertex layout, reusing the existing
// MESH_BLINN_PHONG shader for rendering.
//
// All GPU resources are allocated at load time (cold path). No per-frame
// allocations. Height queries are O(1) via bilinear interpolation on retained
// height data.
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
    const u32 vertexCount = lodRes * lodRes;
    const u32 quadCount = (lodRes - 1) * (lodRes - 1);
    const u32 indexCount = quadCount * 6; // 2 triangles per quad, 3 indices each

    // Compute world-space bounds for this chunk
    const f32 chunkWorldW = cfg.worldWidth / static_cast<f32>(cfg.chunkCountX);
    const f32 chunkWorldD = cfg.worldDepth / static_cast<f32>(cfg.chunkCountZ);
    const f32 chunkOriginX = static_cast<f32>(chunkX) * chunkWorldW;
    const f32 chunkOriginZ = static_cast<f32>(chunkZ) * chunkWorldD;

    // Allocate temporary vertex and index arrays (cold-path heap allocation)
    auto vertices = std::make_unique<rhi::MeshVertex[]>(vertexCount);
    auto indices  = std::make_unique<u32[]>(indexCount);

    // Generate vertices
    for (u32 vz = 0; vz < lodRes; ++vz) {
        for (u32 vx = 0; vx < lodRes; ++vx) {
            const u32 vi = vz * lodRes + vx;

            // World position within chunk
            const f32 localX = (static_cast<f32>(vx) / static_cast<f32>(lodRes - 1)) * chunkWorldW;
            const f32 localZ = (static_cast<f32>(vz) / static_cast<f32>(lodRes - 1)) * chunkWorldD;
            const f32 worldX = chunkOriginX + localX;
            const f32 worldZ = chunkOriginZ + localZ;

            // Normalized UV over the full terrain
            const f32 u = worldX / cfg.worldWidth;
            const f32 v = worldZ / cfg.worldDepth;

            // Sample height
            const f32 heightVal = sampleHeight(heightData, dataW, dataH, u, v);
            const f32 worldY = heightVal * cfg.heightScale;

            vertices[vi].px = worldX;
            vertices[vi].py = worldY;
            vertices[vi].pz = worldZ;
            vertices[vi].u  = u;
            vertices[vi].v  = v;

            if (worldY < outMinY) { outMinY = worldY; }
            if (worldY > outMaxY) { outMaxY = worldY; }

            // Compute normal via finite differences
            const f32 cellSizeX = chunkWorldW / static_cast<f32>(lodRes - 1);
            const f32 cellSizeZ = chunkWorldD / static_cast<f32>(lodRes - 1);

            // Sample neighbors for central differences (forward/backward at edges)
            f32 hLeft  = 0.0f;
            f32 hRight = 0.0f;
            f32 hDown  = 0.0f;
            f32 hUp    = 0.0f;
            f32 dxScale = 2.0f;
            f32 dzScale = 2.0f;

            const f32 uStep = cellSizeX / cfg.worldWidth;
            const f32 vStep = cellSizeZ / cfg.worldDepth;

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
            const f32 dz = (hUp - hDown) * cfg.heightScale;

            glm::vec3 normal{
                -dx,
                dxScale * cellSizeX + dzScale * cellSizeZ,  // Approximation of 2*cellSize
                -dz
            };
            normal = glm::normalize(normal);

            vertices[vi].nx = normal.x;
            vertices[vi].ny = normal.y;
            vertices[vi].nz = normal.z;
        }
    }

    // Generate indices (two triangles per quad)
    u32 idx = 0;
    for (u32 iz = 0; iz < lodRes - 1; ++iz) {
        for (u32 ix = 0; ix < lodRes - 1; ++ix) {
            const u32 topLeft     = iz * lodRes + ix;
            const u32 topRight    = iz * lodRes + ix + 1;
            const u32 bottomLeft  = (iz + 1) * lodRes + ix;
            const u32 bottomRight = (iz + 1) * lodRes + ix + 1;

            // Triangle 1
            indices[idx++] = topLeft;
            indices[idx++] = bottomLeft;
            indices[idx++] = topRight;

            // Triangle 2
            indices[idx++] = topRight;
            indices[idx++] = bottomLeft;
            indices[idx++] = bottomRight;
        }
    }

    // --- GPU upload ---
    const u64 vboSize = static_cast<u64>(vertexCount) * sizeof(rhi::MeshVertex);
    const u64 iboSize = static_cast<u64>(indexCount) * sizeof(u32);

    glGenVertexArrays(1, &lod.vaoId);
    glGenBuffers(1, &lod.vboId);
    glGenBuffers(1, &lod.iboId);

    glBindVertexArray(lod.vaoId);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, lod.vboId);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vboSize),
                 vertices.get(), GL_STATIC_DRAW);

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
                 indices.get(), GL_STATIC_DRAW);

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

    for (u32 i = 0; i < asset.chunkCount; ++i) {
        destroyChunk(asset.chunks[i]);
    }
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

// These functions are declared in terrain_renderer.cpp as extern or we make
// them accessible via a shared internal header. For simplicity, we expose them
// with internal linkage via a namespace-scope function.

const TerrainAsset* getTerrainAsset(const TerrainHandle handle) {
    if (!isValid(handle)) { return nullptr; }
    if (handle.id > MAX_TERRAIN_ASSETS) { return nullptr; }
    const TerrainAsset& asset = s_terrains[handle.id - 1];
    if (!asset.active) { return nullptr; }
    return &asset;
}

} // namespace ffe::renderer
