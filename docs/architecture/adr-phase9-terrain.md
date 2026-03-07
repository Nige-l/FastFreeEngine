# ADR: Phase 9 M1 — Heightmap Terrain System

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (OpenGL 3.3, 1 GB VRAM)
**Security Review Required:** YES — terrain accepts external height data (PNG files via stb_image, float arrays from game code). Asset loading path traversal and bounds validation apply.

---

## 1. Context

Every mainstream game engine (Unity, Unreal, Godot) ships a terrain system. FFE's 3D foundation (mesh loading, Blinn-Phong lighting, shadows, skybox, PBR, skeletal animation) is mature, but there is no way to render large outdoor environments. Developers must either model terrain as a static mesh in Blender (impractical for large worlds) or build their own heightmap system on top of FFE's mesh API (defeats the purpose of an engine).

Phase 9 ("Terrain and Open World") addresses this gap. M1 delivers the foundation: a heightmap-based terrain system that generates chunked meshes on the GPU, computes normals, supports texture mapping, and provides CPU-side height queries for gameplay (character grounding, object placement).

M1 deliberately reuses existing infrastructure (MeshVertex format, MESH_BLINN_PHONG shader, stb_image for PNG loading) to minimise new code and risk. Texture splatting (multi-texture blending based on slope/height) is deferred to M2.

---

## 2. Decision

### 2.1 Handle Pattern

Terrain assets use the same handle pattern as meshes:

```cpp
struct TerrainHandle { u32 id = 0; };
inline bool isValid(const TerrainHandle h) { return h.id != 0; }
static_assert(sizeof(TerrainHandle) == 4);

inline constexpr u32 MAX_TERRAIN_ASSETS = 4;
```

`MAX_TERRAIN_ASSETS = 4` because terrains are large GPU resources. On LEGACY tier (1 GB VRAM), a single terrain with 16x16 chunks at 64x64 vertices per chunk consumes ~32 MB of vertex/index data. Four terrains (128 MB) is the safe ceiling. Most games use one terrain per scene.

Handle slot 0 is always invalid (null handle), consistent with `MeshHandle`, `BufferHandle`, and `TextureHandle`.

### 2.2 Chunked Mesh Architecture

The terrain is divided into a grid of chunks. Each chunk is an independent VAO/VBO/IBO — a self-contained indexed triangle mesh.

| Parameter | Default | Range | Notes |
|-----------|---------|-------|-------|
| Chunk grid | 16x16 | 1x1 to 32x32 | Number of chunks in X and Z |
| Vertices per chunk | 64x64 | 8x8 to 128x128 | Resolution per chunk |
| Total vertices (max) | 1,048,576 | — | 32x32 chunks * 128x128 verts = 16.7M (capped at 1M for LEGACY) |
| Terrain world size | Configurable | — | Width/depth in world units; height range [0, maxHeight] |

**Why chunks?**

1. **Frustum culling.** Only chunks inside the view frustum are drawn. A 16x16 grid means up to 75% of chunks can be skipped for a typical camera angle.
2. **Future LOD.** M2+ can swap chunk meshes for lower-resolution versions based on distance. Chunks are the natural LOD granularity.
3. **LEGACY vertex limits.** OpenGL 3.3 works best with draw calls under 64K vertices. At 64x64 vertices per chunk (4,096 vertices, 2x63x63 = 7,938 triangles), each chunk is well within this limit.
4. **Streaming.** Future milestones can load/unload chunks as the camera moves, enabling open-world terrain.

Each chunk generates a triangle strip converted to indexed triangles:

```
For a chunk with (resX) x (resZ) vertices:
  Vertex count:   resX * resZ
  Triangle count: 2 * (resX - 1) * (resZ - 1)
  Index count:    3 * 2 * (resX - 1) * (resZ - 1)
```

At 64x64 default: 4,096 vertices, 7,938 triangles, 23,814 indices per chunk. For 16x16 chunks: 1,048,576 vertices, 2,032,128 triangles total.

### 2.3 Height Data Input

**C++ API:** Accept `const float*` array of width x depth samples, plus dimensions and a height scale factor. The caller owns the data; the terrain system copies it during mesh generation.

```cpp
struct TerrainDesc {
    const float* heightData;  // width * depth float samples, row-major
    u32 width;                // heightmap width in samples
    u32 depth;                // heightmap depth in samples
    f32 worldWidth;           // terrain extent in world-space X
    f32 worldDepth;           // terrain extent in world-space Z
    f32 maxHeight;            // height scale: sample value 1.0 maps to this Y
    u32 chunkGridX;           // chunks in X direction (default 16)
    u32 chunkGridZ;           // chunks in Z direction (default 16)
    u32 chunkResX;            // vertices per chunk in X (default 64)
    u32 chunkResZ;            // vertices per chunk in Z (default 64)
};
```

**Lua API:** Accept a PNG grayscale image path. The terrain loader reads the PNG via the existing `stb_image` integration (already vendored for texture loading), converts pixel values to floats (0-255 mapped to 0.0-1.0), and passes them through the same pipeline as the C++ API.

```lua
local terrain = ffe.loadTerrain("heightmap.png", {
    worldWidth = 256,
    worldDepth = 256,
    maxHeight = 40,
})
```

**Validation (security):**
- PNG path goes through the existing `isPathSafe()` + `realpath` check (same as texture_loader and mesh_loader).
- File must end with `.png` (no arbitrary file reads).
- Maximum heightmap resolution: 2048x2048 (16 MB of float data). Reject larger images.
- Maximum file size: 16 MB (pre-parse guard, consistent with other asset loaders).
- NaN/Inf rejection on all float parameters in TerrainDesc.

### 2.4 Normal Computation

Normals are computed at mesh generation time using finite differences on the height data:

```
For sample at (x, z):
  dx = height(x+1, z) - height(x-1, z)
  dz = height(x, z+1) - height(x, z-1)
  normal = normalize(vec3(-dx * heightScale, 2.0 * cellSize, -dz * heightScale))
```

Edge samples use forward/backward differences instead of central differences.

Normals are stored per-vertex in the VBO. No per-frame recomputation — the terrain is static after generation. This matches MeshVertex layout (position + normal + UV).

### 2.5 Vertex Format

Terrain vertices use the existing `MeshVertex` format:

```cpp
struct MeshVertex {
    f32 px, py, pz;  // Position (12 bytes)
    f32 nx, ny, nz;  // Normal   (12 bytes)
    f32 u, v;        // Texcoord (8 bytes)
};
static_assert(sizeof(MeshVertex) == 32);
```

This is identical to the 3D mesh vertex layout. No new vertex format, no new VAO configuration, no new shader attribute setup. The terrain reuses the exact same GPU pipeline as regular meshes.

UV coordinates are computed as:

```
u = worldX / worldWidth
v = worldZ / worldDepth
```

This maps the entire terrain to [0,1] UV space. A single diffuse texture tiles across the terrain using the material's texture. Texture splatting (M2) will add multi-layer UV mapping.

### 2.6 Rendering

**No new shader for M1.** Terrain chunks render through the existing `MESH_BLINN_PHONG` shader (enum value 3). Each chunk is a standard indexed triangle mesh — the shader does not need to know it is terrain.

The terrain render pass:
1. For each loaded terrain with a corresponding `Terrain` ECS component:
2. Compute the AABB of each chunk from the chunk grid position and terrain dimensions.
3. Frustum cull: skip chunks whose AABB is outside the view frustum.
4. For visible chunks: bind the chunk's VAO, set the model matrix (terrain world transform), draw indexed.

Terrain participates in shadow mapping. The shadow depth pass draws terrain chunks the same way — the `SHADOW_DEPTH` shader works with the existing vertex layout. No new shadow shader needed.

**Draw call count:** Up to 256 draw calls for a 16x16 grid (worst case, all chunks visible). On LEGACY hardware, this is acceptable — each draw call is ~4K vertices, and modern OpenGL 3.3 drivers handle hundreds of small draw calls efficiently. If profiling shows this is a bottleneck, M2 can merge visible chunks into a single draw call.

### 2.7 ECS Component

```cpp
struct Terrain {
    TerrainHandle terrainHandle;  // 4 bytes
    u32           _pad = 0;       // 4 bytes — explicit padding
};
static_assert(sizeof(Terrain) == 8, "Terrain component must be 8 bytes");
```

Same pattern as the `Mesh` component. An entity with `Transform3D` + `Terrain` is rendered by the terrain render pass. The `Transform3D` provides the world-space origin of the terrain.

**M1 limitation:** One terrain per scene. The terrain render function queries all entities with `Transform3D` + `Terrain` and renders them. Multiple terrains are supported by the data model but not optimised or tested in M1.

### 2.8 CPU-Side Height Query

```cpp
// Returns the interpolated height at world-space (x, z) for the given terrain.
// Uses bilinear interpolation on the original height data.
// Returns 0.0f if (x, z) is outside the terrain bounds.
// O(1) — direct array lookup after coordinate mapping.
f32 getTerrainHeight(TerrainHandle handle, f32 worldX, f32 worldZ);

// Returns the interpolated surface normal at world-space (x, z).
// Uses the same finite-difference method as vertex normal computation.
// Returns vec3(0, 1, 0) if (x, z) is outside the terrain bounds.
glm::vec3 getTerrainNormal(TerrainHandle handle, f32 worldX, f32 worldZ);
```

**Implementation:** The original `float*` height data is copied into a `std::unique_ptr<float[]>` owned by the terrain asset slot (cold-path allocation at load time). The height query maps world-space (x, z) to heightmap (col, row) coordinates:

```
col = (worldX / worldWidth) * (width - 1)
row = (worldZ / worldDepth) * (depth - 1)
```

Then bilinear interpolation between the four surrounding samples. This is O(1) with no search, no spatial data structure, and no per-frame allocation.

**Lua bindings:**

```lua
local y = ffe.getTerrainHeight(terrain, x, z)
local nx, ny, nz = ffe.getTerrainNormal(terrain, x, z)
```

### 2.9 Memory Layout

**CPU side (per terrain asset):**

| Data | Size (default 16x16 chunks, 64x64 verts) |
|------|------------------------------------------|
| Height data (unique_ptr<float[]>) | width * depth * 4 bytes. For 1024x1024 source: 4 MB |
| Chunk metadata (AABB, VAO/VBO/IBO IDs) | 16x16 * ~64 bytes = 16 KB |
| Total CPU | ~4 MB |

**GPU side (per terrain asset):**

| Data | Size |
|------|------|
| VBO (MeshVertex, 32 bytes * 1M vertices) | 32 MB |
| IBO (u32, 4 bytes * ~6M indices) | 24 MB |
| Total GPU | ~56 MB |

On LEGACY tier (1 GB VRAM), one terrain at default resolution uses ~5.6% of VRAM. Four terrains at MAX_TERRAIN_ASSETS would use ~22.4%. This leaves ample room for mesh assets, textures, shadow maps, and framebuffers.

**No per-frame allocations.** All memory is allocated at terrain load time (cold path). The render loop reads chunk metadata and issues draw calls — no allocation, no deallocation.

### 2.10 Physics

M1 provides CPU-side height queries (`getTerrainHeight`, `getTerrainNormal`) for gameplay code to implement character grounding and object placement. This is sufficient for basic terrain interaction.

Jolt Physics `HeightFieldShape` integration is deferred to M2. This will allow rigid bodies to collide with the terrain surface natively through the physics engine, without game code manually querying heights.

### 2.11 Frustum Culling

Each chunk stores a precomputed AABB (min/max corners in local space). At render time, the AABB is transformed by the terrain's `Transform3D` model matrix and tested against the view frustum's six planes.

The frustum is extracted from the view-projection matrix (Griess-Hartmann method). This is a standard technique — no new math library code needed, just a utility function.

**Chunk AABB computation:** At mesh generation time, scan the chunk's height values for min/max Y. The X/Z extents are known from the chunk grid position. Store as `glm::vec3 aabbMin, aabbMax` per chunk.

---

## 3. File Plan

### New Files

| File | Owner | Purpose |
|------|-------|---------|
| `engine/renderer/terrain.h` | renderer-specialist | TerrainHandle, TerrainDesc, TerrainChunkRecord, public API (loadTerrain, unloadTerrain, getTerrainHeight, getTerrainNormal, terrainRenderSystem) |
| `engine/renderer/terrain.cpp` | renderer-specialist | Implementation: mesh generation, normal computation, frustum culling, render loop, height queries |
| `engine/renderer/frustum.h` | renderer-specialist | Frustum struct (6 planes), extractFrustum(viewProj), isAABBInFrustum(frustum, min, max) — small utility, inline where possible |
| `tests/renderer/test_terrain.cpp` | engine-dev | Unit tests: handle lifecycle, height query accuracy, normal computation, bounds rejection, chunk generation, frustum culling |
| `docs/architecture/adr-phase9-terrain.md` | architect | This document |

### Modified Files

| File | Owner | Changes |
|------|-------|---------|
| `engine/renderer/CMakeLists.txt` | renderer-specialist | Add `terrain.cpp` to sources |
| `engine/renderer/render_system.h` | renderer-specialist | Add `Terrain` ECS component |
| `engine/renderer/mesh_renderer.h` | renderer-specialist | Forward-declare `terrainRenderSystem` (or add to mesh_renderer if cleaner) |
| `engine/core/application.cpp` | engine-dev | Call `terrainRenderSystem` in render loop (after meshRenderSystem, before 2D pass) |
| `engine/scripting/script_engine.cpp` | engine-dev | Add Lua bindings: `ffe.loadTerrain`, `ffe.unloadTerrain`, `ffe.createTerrainEntity`, `ffe.getTerrainHeight`, `ffe.getTerrainNormal` |
| `engine/renderer/.context.md` | api-designer | Document terrain API |
| `tests/CMakeLists.txt` | engine-dev | Add `test_terrain.cpp` |

---

## 4. Tier Support

| Tier | Support | Notes |
|------|---------|-------|
| RETRO | NO | OpenGL 2.1 lacks VAO support; terrain requires VAOs |
| LEGACY | YES (primary) | OpenGL 3.3. Chunks stay under 64K vertices. Single diffuse texture per terrain. |
| STANDARD | YES | Same as LEGACY for M1. M2+ may add tessellation shaders on STANDARD+ |
| MODERN | YES | Same as LEGACY for M1. M2+ may add GPU-side LOD and virtual texturing |

---

## 5. Consequences

### What Changes

- FFE gains outdoor environment support for the first time.
- Game developers can create terrain from heightmap images (a standard workflow in every engine).
- CPU-side height queries enable character grounding without physics engine integration.
- The frustum culling utility (`frustum.h`) is reusable by other systems (mesh culling, particle culling) in future sessions.

### What Is Deferred to M2+

| Feature | Milestone | Rationale |
|---------|-----------|-----------|
| Texture splatting (multi-texture blending) | M2 | Requires a new shader; M1 reuses MESH_BLINN_PHONG |
| Jolt HeightFieldShape physics | M2 | Requires Jolt integration work; CPU queries suffice for M1 |
| LOD (distance-based chunk resolution) | M2 | Requires mesh swapping infrastructure; uniform resolution is fine for LEGACY |
| Terrain streaming (load/unload chunks) | M3 | Requires async loading; entire terrain fits in memory for M1 |
| Terrain editing (raise/lower/smooth) | M3+ | Editor feature; M1 is runtime-only |
| Vegetation/foliage placement | M4+ | Requires instancing improvements and a placement system |
| Terrain holes/caves | M4+ | Requires per-vertex visibility flags or mesh modification |

### Risks

1. **Draw call count on LEGACY.** 256 draw calls (16x16 chunks) is within budget for OpenGL 3.3, but if profiling shows otherwise, chunk merging or multi-draw-indirect (STANDARD+ only) may be needed.
2. **VRAM usage.** 56 MB per terrain is significant on 1 GB VRAM. The 4-terrain limit and per-terrain VRAM tracking mitigate this.
3. **Heightmap resolution vs. visual quality.** 1024x1024 source heightmap sampled into 16x16 chunks at 64x64 vertices gives good visual fidelity for LEGACY. Higher-resolution terrains (2048x2048) are supported but may stress VRAM.

---

## 6. Performance Budget

| Metric | Target | Justification |
|--------|--------|---------------|
| Terrain load time | < 500 ms | Mesh generation is O(vertices), ~1M vertices at 32 bytes each. CPU-bound, single-threaded. |
| Per-frame render cost | < 2 ms at 1080p on LEGACY | Frustum culling eliminates ~50-75% of chunks. Visible chunks are simple indexed draws with the existing Blinn-Phong shader. |
| Height query latency | < 1 us | O(1) array lookup + bilinear interpolation. No cache miss for sequential queries (height data is contiguous float array). |
| CPU memory | < 5 MB per terrain | Height data (4 MB for 1024x1024) + chunk metadata (16 KB). |
| GPU memory | < 60 MB per terrain | VBO (32 MB) + IBO (24 MB) at default resolution. |
| Draw calls per frame | <= 256 (worst case) | 16x16 chunk grid, all visible. Typical: 64-128 after frustum culling. |

All targets assume LEGACY tier hardware (~2012 GPU, OpenGL 3.3, 1 GB VRAM). STANDARD and MODERN tiers will have significant headroom.

---

## 7. Phase 9 M2: Terrain Texturing

**Status:** PROPOSED
**Date:** 2026-03-07

M1 renders terrain with a single diffuse texture via the existing `MESH_BLINN_PHONG` shader. This is adequate for prototyping but visually insufficient for shipping games — real terrains blend multiple surface types (grass, dirt, rock, snow) based on artist-painted weight maps, and steep cliff faces suffer from texture stretching under standard UV projection.

M2 introduces **splat-map texturing**, **4-layer blending**, and **triplanar projection** for steep surfaces, all behind a new dedicated `TERRAIN` shader that stays within LEGACY tier (GLSL 330 core, OpenGL 3.3).

### 7.1 Splat Map

A splat map is an RGBA texture where each channel encodes the blend weight for one of four terrain layers:

| Channel | Default Layer | Purpose |
|---------|--------------|---------|
| R | Layer 0 (e.g. grass) | Primary ground cover |
| G | Layer 1 (e.g. dirt) | Paths, exposed earth |
| B | Layer 2 (e.g. rock) | Cliff faces, outcrops |
| A | Layer 3 (e.g. snow) | High altitude, caps |

**Authoring convention:** Channels should sum to ~1.0 per texel, but the fragment shader normalizes weights (`weights /= dot(weights, vec4(1.0))`) to guarantee correctness regardless of authoring precision.

**Resolution:** The splat map resolution is independent of the heightmap resolution. A 512x512 splat map provides sufficient blending detail for a 2048x2048 heightmap — each splat texel covers a ~4x4 patch of height samples. Lower resolutions (256x256) are acceptable; higher resolutions (1024x1024) are supported but rarely needed since blend transitions are inherently soft.

**Loading:** The splat map is loaded through the existing `loadTexture` pipeline (stb_image, RGBA8). Same path validation and size guards apply. The texture is bound to unit 1 during the terrain render pass.

### 7.2 Texture Layers

Each terrain supports exactly 4 texture layers. Each layer consists of:

```cpp
struct TerrainLayer {
    rhi::TextureHandle texture;   // Diffuse texture for this layer (0 = unused)
    f32                uvScale;   // Tiling multiplier (default 16.0)
};
```

Layer textures are sampled with **tiled UVs**: the terrain's base UV (which spans [0,1] over the entire terrain) is multiplied by the layer's `uvScale`. At the default `uvScale = 16.0`, a 512x512 grass texture repeats 16 times across the terrain — providing good detail without visible stretching at ground level.

**UV computation per fragment:**

```glsl
vec2 tiledUV = v_texCoord * u_layerScale[i];
vec4 layerColor = texture(u_layer0, tiledUV); // (layer 0 example)
```

Layers are sampled from texture units 2–5. Unused layers (texture handle 0) contribute zero weight — the shader skips their sample via the splat weight being zero. No branching needed; multiplication by zero eliminates the contribution.

### 7.3 Triplanar Projection

Standard UV mapping projects textures from above (the XZ plane). On steep surfaces — cliff faces, overhangs — this causes severe stretching because the UV gradient approaches zero along the face normal.

**Triplanar projection** solves this by blending three axis-aligned projections weighted by the surface normal:

| Projection | UV Source | Dominant When |
|------------|-----------|---------------|
| XZ (top-down) | worldPos.xz | `abs(normal.y)` is large (flat ground) |
| XY (front/back) | worldPos.xy | `abs(normal.z)` is large |
| YZ (left/right) | worldPos.yz | `abs(normal.x)` is large |

**Blend weights:**

```glsl
vec3 blend = abs(v_worldNormal);
blend = pow(blend, vec3(4.0));          // Sharpen the transition
blend /= (blend.x + blend.y + blend.z); // Normalize
```

**Activation threshold:** Triplanar sampling is only applied on fragments where `abs(normal.y) < u_triplanarThreshold` (default 0.7). On flat ground (`normal.y ≈ 1.0`), the standard top-down UV is used — this is the common case and avoids the extra texture samples. The threshold is configurable per terrain.

**Per-layer triplanar:** When triplanar is active for a fragment, each layer texture is sampled 3 times (XZ, XY, YZ projections) and blended. This means steep fragments perform up to 12 layer samples (3 projections x 4 layers) plus 1 splat sample, versus 5 samples (1 splat + 4 layers) on flat ground.

**Implementation in GLSL:**

```glsl
vec4 sampleLayerTriplanar(sampler2D tex, float scale, vec3 worldPos, vec3 blend) {
    vec4 xz = texture(tex, worldPos.xz * scale);
    vec4 xy = texture(tex, worldPos.xy * scale);
    vec4 yz = texture(tex, worldPos.yz * scale);
    return xz * blend.y + xy * blend.z + yz * blend.x;
}
```

### 7.4 TERRAIN Shader

A new builtin shader is added to the `BuiltinShader` enum:

```cpp
TERRAIN = 19, // Terrain splat-map texturing with triplanar projection
```

This follows `SSAO_BLUR = 18` and increments `COUNT` to 20.

**Vertex shader:** Identical to `MESH_BLINN_PHONG` — MVP transform, world-space position and normal passed to fragment stage, UV passthrough. No new vertex attributes; terrain continues to use the `MeshVertex` layout.

**Fragment shader (GLSL 330 core):**

Uniforms:

| Uniform | Type | Binding | Purpose |
|---------|------|---------|---------|
| `u_splatMap` | `sampler2D` | unit 1 | RGBA splat weight texture |
| `u_layer0` | `sampler2D` | unit 2 | Layer 0 diffuse texture |
| `u_layer1` | `sampler2D` | unit 3 | Layer 1 diffuse texture |
| `u_layer2` | `sampler2D` | unit 4 | Layer 2 diffuse texture |
| `u_layer3` | `sampler2D` | unit 5 | Layer 3 diffuse texture |
| `u_layerScale[4]` | `float[4]` | — | UV tiling scale per layer |
| `u_triplanarEnabled` | `int` | — | 0 = off, 1 = on |
| `u_triplanarThreshold` | `float` | — | Normal.y threshold for triplanar activation |
| (existing) | — | unit 0 | Shadow depth map (reused from Blinn-Phong) |
| (existing) | — | — | All lighting, shadow, and fog uniforms from MESH_BLINN_PHONG |

The fragment shader logic:

1. Sample splat map at base UV → `vec4 weights`; normalize.
2. Determine if triplanar is needed: `bool steep = u_triplanarEnabled && abs(v_worldNormal.y) < u_triplanarThreshold`.
3. For each layer: sample with tiled UV (flat) or triplanar (steep), multiply by splat weight, accumulate.
4. Apply Blinn-Phong lighting (directional + point lights), shadow sampling, fog — identical to `MESH_BLINN_PHONG` fragment logic.

**Why a new shader instead of extending MESH_BLINN_PHONG?** The terrain shader has fundamentally different texture binding semantics (5 textures with specific roles vs. 1 diffuse + optional normal/specular). Extending MESH_BLINN_PHONG with conditionals would add branching to every mesh draw call. A separate shader keeps mesh rendering lean and terrain rendering self-contained.

### 7.5 Texture Unit Layout

| Unit | Content | Used By |
|------|---------|---------|
| 0 | Shadow depth map | Shadow sampling (existing) |
| 1 | Splat map (RGBA) | Terrain blend weights |
| 2 | Layer 0 texture | First terrain surface |
| 3 | Layer 1 texture | Second terrain surface |
| 4 | Layer 2 texture | Third terrain surface |
| 5 | Layer 3 texture | Fourth terrain surface |

OpenGL 3.3 guarantees a minimum of 16 texture units. This layout uses 6, leaving 10 units free for future extensions (e.g., layer normal maps in M3).

### 7.6 Performance Budget

| Scenario | Texture Samples/Fragment | Target (1080p, LEGACY) |
|----------|--------------------------|----------------------|
| Flat ground (common case) | 6 (1 splat + 4 layers + 1 shadow) | < 1.5 ms |
| Steep surface (triplanar) | 14 (1 splat + 4 layers x 3 projections + 1 shadow) | < 2.0 ms |
| Mixed scene (typical) | ~7 average | < 2.0 ms total terrain pass |

Triplanar fragments are a minority of the screen (cliff faces, steep slopes). On a typical terrain, <15% of visible fragments activate triplanar sampling. The average sample count stays close to 7, well within LEGACY tier budget.

**Splat map fetch is coherent** — neighboring fragments sample nearby texels in the splat map, which is cache-friendly. Layer texture fetches tile at `uvScale` frequency, which is also cache-friendly for typical tiling factors (8–32).

**No per-frame allocations.** All textures are bound once per terrain draw pass. Splat map and layer textures are loaded at scene load time (cold path).

### 7.7 TerrainMaterial Struct

```cpp
struct TerrainMaterial {
    rhi::TextureHandle splatTexture;         // RGBA splat map
    TerrainLayer       layers[4];            // Per-layer texture + UV scale
    bool               triplanarEnabled;     // Default: true
    f32                triplanarThreshold;   // Default: 0.7
};
```

This struct is stored per terrain asset slot (alongside the existing height data and chunk metadata). It is set via the API functions below and read by the terrain render pass when binding uniforms.

### 7.8 API Changes

**New C++ API (cold path only):**

```cpp
// Set the splat map texture for a terrain.
// The texture must be an RGBA image loaded via loadTexture().
void setTerrainSplatMap(TerrainHandle handle, rhi::TextureHandle splatTexture);

// Set a layer texture and UV tiling scale.
// layerIndex: 0-3 (corresponding to splat map R, G, B, A channels).
// uvScale: tiling multiplier (default 16.0). Higher = more repetitions.
void setTerrainLayer(TerrainHandle handle, u32 layerIndex,
                     rhi::TextureHandle texture, f32 uvScale = 16.0f);

// Enable/disable triplanar projection and set the normal.y threshold.
// threshold: fragments with abs(normal.y) < threshold use triplanar (default 0.7).
void setTerrainTriplanar(TerrainHandle handle, bool enabled, f32 threshold = 0.7f);
```

**New Lua bindings:**

```lua
-- Load splat map and assign to terrain
local splatTex = ffe.loadTexture("terrain_splat.png")
ffe.setTerrainSplatMap(terrain, splatTex)

-- Set layer textures with tiling
local grass = ffe.loadTexture("grass_diffuse.png")
local dirt  = ffe.loadTexture("dirt_diffuse.png")
local rock  = ffe.loadTexture("rock_diffuse.png")
local snow  = ffe.loadTexture("snow_diffuse.png")

ffe.setTerrainLayer(terrain, 0, grass, 16.0)
ffe.setTerrainLayer(terrain, 1, dirt,  16.0)
ffe.setTerrainLayer(terrain, 2, rock,  12.0)
ffe.setTerrainLayer(terrain, 3, snow,  20.0)

-- Enable triplanar for cliff faces
ffe.setTerrainTriplanar(terrain, true, 0.7)
```

**Validation:**
- `layerIndex` must be 0–3; values outside this range log an error and return.
- `uvScale` must be > 0 and finite; NaN/Inf rejected.
- `threshold` must be in (0.0, 1.0]; values outside this range are clamped.
- Invalid `TerrainHandle` or `TextureHandle` → no-op with error log.

### 7.9 Rendering Flow Change

The terrain render pass (called after `meshRenderSystem`, before 2D pass) changes from:

**M1:** Bind `MESH_BLINN_PHONG` → set lighting/shadow uniforms → for each visible chunk: bind VAO, draw.

**M2:** Bind `TERRAIN` → set lighting/shadow/fog uniforms → bind splat map (unit 1) → bind layer textures (units 2–5) → set layer scales + triplanar uniforms → for each visible chunk: bind VAO, draw.

If no splat map is set (M1 fallback), the terrain render pass falls back to `MESH_BLINN_PHONG` with the single diffuse texture — preserving backward compatibility with existing terrain code and Lua scripts that do not use the M2 API.

### 7.10 File Plan (M2 Additions)

**Modified files:**

| File | Owner | Changes |
|------|-------|---------|
| `engine/renderer/terrain.h` | renderer-specialist | Add `TerrainLayer`, `TerrainMaterial` structs; declare `setTerrainSplatMap`, `setTerrainLayer`, `setTerrainTriplanar` |
| `engine/renderer/terrain.cpp` | renderer-specialist | Implement new API functions; update terrain render pass to use TERRAIN shader when splat map is set |
| `engine/renderer/shader_library.h` | renderer-specialist | Add `TERRAIN = 19` to `BuiltinShader` enum, update `COUNT` |
| `engine/renderer/shader_library.cpp` | renderer-specialist | Add TERRAIN vertex/fragment shader source strings |
| `engine/scripting/script_engine.cpp` | engine-dev | Add Lua bindings: `ffe.setTerrainSplatMap`, `ffe.setTerrainLayer`, `ffe.setTerrainTriplanar` |
| `engine/renderer/.context.md` | api-designer | Document M2 terrain texturing API |
| `tests/renderer/test_terrain.cpp` | engine-dev | Add tests: splat map assignment, layer configuration, triplanar parameter validation, fallback behavior |

No new files required — M2 extends the files created in M1.

### 7.11 Consequences

**What M2 delivers:**
- Artist-controlled multi-texture terrain blending via industry-standard splat maps.
- Visually correct cliff-face texturing via triplanar projection.
- Full backward compatibility — terrains without a splat map render identically to M1.
- All within LEGACY tier constraints (GLSL 330, 6 texture units, no compute shaders).

**What is deferred to M3+:**

| Feature | Milestone | Rationale |
|---------|-----------|-----------|
| Per-layer normal maps | M3 | Requires 4 additional texture units (units 6–9) and tangent-space computation; deferred to keep M2 focused |
| Height-based blending (parallax splatting) | M3 | Requires per-layer height textures; visual improvement over linear splat blending |
| Runtime splat map painting | M3+ | Editor feature; requires FBO render-to-texture for splat map modification |
| More than 4 layers | M3+ | Requires multi-pass rendering or texture arrays; 4 layers cover the vast majority of terrains |
| Procedural splat generation (slope/height rules) | M3 | Quality-of-life feature; artists can generate splat maps externally for M2 |
