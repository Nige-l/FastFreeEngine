#pragma once

// terrain.h -- Heightmap terrain loading and CPU-side height queries for FFE.
//
// NOT a per-frame API. loadTerrain() and loadTerrainFromImage() perform heap
// allocation and GPU uploads. Call at scene load time only.
//
// Thread safety: single-threaded. All functions must be called from the render thread.
//
// Tier support: LEGACY (OpenGL 3.3 core profile).
// Each chunk is an independent VAO/VBO/IBO with MeshVertex (32 bytes) layout.
// Maximum 4 terrain assets loaded simultaneously (~56 MB GPU per terrain).

#include "core/types.h"
#include "renderer/rhi_types.h"

namespace ffe::renderer {

// --- Opaque handle to a loaded terrain asset ---
// Value 0 is always invalid (null handle). Same pattern as MeshHandle.
struct TerrainHandle { u32 id = 0; };
inline bool isValid(const TerrainHandle h) { return h.id != 0; }
static_assert(sizeof(TerrainHandle) == 4);

// Maximum terrain assets that can be loaded simultaneously.
// On LEGACY tier (1 GB VRAM), each terrain at default resolution uses ~56 MB GPU.
// 4 terrains = ~224 MB, leaving ample room for meshes, textures, shadow maps.
inline constexpr u32 MAX_TERRAIN_ASSETS = 4;

// Maximum vertices per chunk side. 128x128 = 16,384 vertices per chunk.
inline constexpr u32 MAX_CHUNK_RESOLUTION = 128;

// Maximum total chunks per terrain (e.g. 16x16 = 256).
inline constexpr u32 MAX_CHUNKS_TOTAL = 256;

// Maximum heightmap resolution (width or height). Pre-parse guard.
inline constexpr u32 MAX_HEIGHTMAP_DIMENSION = 2048;

// Maximum heightmap file size (bytes). Pre-parse guard.
inline constexpr u64 MAX_HEIGHTMAP_FILE_BYTES = 16ull * 1024ull * 1024ull;

// --- Terrain configuration ---
struct TerrainConfig {
    f32 worldWidth      = 256.0f;   // Total terrain width in world units (X axis)
    f32 worldDepth      = 256.0f;   // Total terrain depth in world units (Z axis)
    f32 heightScale     = 50.0f;    // Height multiplier: sample 1.0 maps to this Y
    u32 chunkResolution = 64;       // Vertices per chunk side (both X and Z)
    u32 chunkCountX     = 16;       // Number of chunks along X
    u32 chunkCountZ     = 16;       // Number of chunks along Z
};

// --- Loading ---

// Load terrain from raw float heightmap data.
// heightData: width * height float samples in [0, 1], row-major (Z rows, X columns).
// The caller owns the data; the terrain system copies it during mesh generation.
//
// On success: returns a valid TerrainHandle (handle.id > 0).
// On failure: returns TerrainHandle{0} and logs via FFE_LOG_ERROR.
//
// Cold path only -- do NOT call per frame.
TerrainHandle loadTerrain(const float* heightData, u32 dataWidth, u32 dataHeight,
                          const TerrainConfig& config);

// Load terrain from a PNG grayscale image file.
// path: relative path (same rules as loadTexture -- no traversal, no absolute).
// Pixel values 0-255 are normalized to [0.0, 1.0] float range.
//
// On success: returns a valid TerrainHandle.
// On failure: returns TerrainHandle{0} and logs via FFE_LOG_ERROR.
//
// Cold path only.
TerrainHandle loadTerrainFromImage(const char* path, const TerrainConfig& config);

// --- Unloading ---

// Unload a specific terrain and free all GPU resources (VAOs, VBOs, IBOs).
// Safe to call with an invalid handle (id == 0) -- no-op.
void unloadTerrain(TerrainHandle handle);

// Unload all terrains. Called by Application::shutdown().
void unloadAllTerrains();

// --- CPU-side height queries ---

// Get interpolated height at world position (bilinear interpolation).
// Returns 0.0f if handle is invalid or position is outside terrain bounds.
// O(1) -- direct array lookup after coordinate mapping.
f32 getTerrainHeight(TerrainHandle handle, f32 worldX, f32 worldZ);

// --- Texture ---

// Set an optional diffuse texture for terrain rendering.
// If not set, terrain renders with a default green-ish diffuse color.
void setTerrainTexture(TerrainHandle handle, rhi::TextureHandle texture);

// --- Convenience ---

// Get the first active terrain handle (for Lua convenience).
// Returns TerrainHandle{0} if no terrains are loaded.
TerrainHandle getFirstActiveTerrain();

} // namespace ffe::renderer
