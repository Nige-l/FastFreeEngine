# ADR: Phase 9 M5 — Vegetation System

**Status:** Accepted
**Tier:** LEGACY (OpenGL 3.3 core profile)
**Owner:** renderer-specialist (vegetation.h/cpp, shader), engine-dev (bindings, tests)

---

## 1. Scope

GPU-instanced billboard grass patches and simple procedural trees placed on terrain surfaces.
No tessellation. No compute shaders. No geometry shaders. GLSL 330 core only.

Out of scope: wind animation, collision with vegetation, distance-based LOD per instance,
per-instance color variation, vegetation shadow casting.

---

## 2. Grass — Design Decisions

**Representation:** Y-axis billboard quads. Each quad is a unit square in local space
(two triangles, 4 vertices). The vertex shader rotates each instance to face the camera
around the Y axis only (cylindrical billboard). No X-axis tilt — avoids grass lying flat
on steep slopes.

**Y-billboard in vertex shader:** Compute angle = atan2(cameraPos.x - instancePos.x,
cameraPos.z - instancePos.z). Build a 2D rotation matrix in XZ plane. Apply to the quad's
local X vertex offset only. Y component of vertex position is untouched — grass always
grows upward.

**Alpha test:** Discard fragment if `texture(grassTex, uv).a < 0.5`. No blending.
Eliminates sorting requirement. Required for LEGACY tier (no OIT).

**Fallback texture:** When `textureHandle == 0`, bind a 4x4 solid green RGBA texture
generated once at `VegetationSystem::init()` time. No heap allocation after init.

**Distance fade:** Two uniforms: `u_fadeStart` (default 40.0) and `u_fadeEnd` (default 80.0).
Alpha multiplied by `1.0 - smoothstep(fadeStart, fadeEnd, dist)` before discard test.
Grass at or beyond `fadeEnd` discards entirely. Fade distances are per-system constants,
not per-patch — one set of uniforms per draw call.

**Instance buffer:** One `GL_ARRAY_BUFFER` per patch. Stores `GrassInstance` structs
(see below). Buffer is populated once at `addPatch` time via `glBufferData` with
`GL_STATIC_DRAW`. No per-frame CPU work. No `glBufferSubData` at runtime.

**Density → instance count:** `density` is clamped to [1, 256]. Instance positions are
distributed across the patch bounding box using a fixed-seed deterministic scatter
(LCG or similar — no `std::random_device`, no heap, stack-only). Each instance Y is
queried from `ffe::renderer::getTerrainHeight(terrainHandle, x, z)`.

**GrassInstance struct (24 bytes):**
```cpp
struct GrassInstance {
    glm::vec3 position;  // world-space XYZ (12 bytes)
    float     scale;     // uniform scale (4 bytes)
    float     rotation;  // Y-axis pre-rotation in radians (4 bytes)
    float     _pad;      // pad to 24 bytes (4 bytes)
};
```
Attribute layout: slot 4 = vec3 position, slot 5 = float scale, slot 6 = float rotation.
Divisor = 1 for all three. Slots 0-3 reserved for quad vertex position + UV.

**Patch bounding box:** Computed from terrain `TerrainConfig` (worldWidth, worldDepth)
scaled by a `patchRadius` parameter passed at `addPatch` time. Default: scatter within
a 20x20 world-unit square centered on an origin the caller specifies. `addPatch` does
not take an explicit center — it scatters across the full terrain surface. Caller
controls density, not placement radius.

**Max patches:** 32 simultaneous grass patches. Each patch: 1 VAO + 1 VBO (quad vertices)
+ 1 VBO (instance data). Total GPU: 32 × (256 × 24 B instance + 96 B quad) ≈ 200 KB. Trivial.

**VegetationHandle:** `struct VegetationHandle { u32 id = 0; }`. Value 0 = invalid.
Same pattern as `TerrainHandle`, `MeshHandle`.

---

## 3. Trees — Design Decisions

**Representation:** Two GPU-instanced draw calls per frame. All trunks share one
`MeshHandle` (unit cube, brown material). All crowns share a second `MeshHandle`
(unit cube, green material). Both use the existing `MESH_INSTANCED` shader (enum 12).

**TreeInstance struct (16 bytes):**
```cpp
struct TreeInstance {
    float x, y, z;   // world position (12 bytes)
    float scale;      // uniform scale (4 bytes)
};
```

**Trunk InstanceData:** Model matrix = translate(x, y, z) * scale(0.15f * s, 1.0f * s, 0.15f * s).
**Crown InstanceData:** Model matrix = translate(x, y + 1.0f * s, z) * scale(0.7f * s, 0.7f * s, 0.7f * s).
Where `s = TreeInstance::scale`.

**Instance buffers:** Two `GL_ARRAY_BUFFER` objects (trunk instances, crown instances),
both sized for `MAX_TREES = 512` entries. Allocated once at `VegetationSystem::init()`.
Rebuilt (full `glBufferData`) on `addTree` and `clearTrees`. No per-frame rebuild.

**Tree mesh creation:** `VegetationSystem::init()` creates the unit cube meshes internally
using `ffe::renderer::loadMesh` or raw VBO construction — renderer-specialist decides.
Meshes are unloaded at `VegetationSystem::shutdown()`.

**Tree materials:** Brown trunk color set via `setMeshColor` equivalent on the cube mesh.
Green crown color same path. No texture required. Falls back to Blinn-Phong diffuse color.

**Max trees:** 512. At 16 B per `TreeInstance` and 64 B per `InstanceData`, two buffers
of 512 entries each = 512 × (64 + 64) B = 64 KB. Trivial on LEGACY tier.

**Y placement:** `addTree(x, y, z, scale)` — the caller supplies Y directly. No implicit
terrain height lookup for trees. Reason: trees may be placed on non-terrain surfaces or
elevated platforms. Caller uses `ffe.getTerrainHeight` from Lua before calling `ffe.addTree`.

---

## 4. Shader — VEGETATION (enum 16)

**Enum value:** 16. Next after `SSAO_BLUR` (15).
**GLSL version:** `#version 330 core`.
**Source location:** Inline in `engine/renderer/shader_library.cpp` alongside all other shaders.

**Vertex inputs:**
- `layout(location=0) in vec3 a_pos;` — quad vertex position
- `layout(location=1) in vec2 a_uv;` — quad UV
- `layout(location=4) in vec3 i_position;` — instance world position (divisor=1)
- `layout(location=5) in float i_scale;` — instance scale (divisor=1)
- `layout(location=6) in float i_rotation;` — instance Y pre-rotation (divisor=1)

**Uniforms:**
- `mat4 u_view`, `mat4 u_projection` — camera matrices
- `vec3 u_cameraPos` — for billboard angle computation
- `float u_fadeStart`, `float u_fadeEnd` — distance fade
- `sampler2D u_grassTex` — grass diffuse+alpha texture

**Fragment:** Sample `u_grassTex`. Compute fade alpha. Discard if final alpha < 0.5.
Output `vec4(color.rgb, 1.0)` — no blending needed after discard.

**No model matrix uniform.** Position built entirely from instance attributes + billboard rotation.

---

## 5. VegetationSystem Class Interface (vegetation.h)

```cpp
namespace ffe::renderer {

inline constexpr u32 MAX_VEGETATION_PATCHES = 32;
inline constexpr u32 MAX_GRASS_INSTANCES    = 256;
inline constexpr u32 MAX_TREES              = 512;

struct VegetationHandle { u32 id = 0; };
inline bool isValid(const VegetationHandle h) { return h.id != 0; }

struct GrassInstance { glm::vec3 position; float scale; float rotation; float _pad; };
struct TreeInstance  { float x, y, z, scale; };

class VegetationSystem {
public:
    void init();      // Allocates GPU buffers, creates cube meshes, green fallback tex
    void shutdown();  // Frees all GPU resources

    // Grass
    VegetationHandle addPatch(TerrainHandle terrain, u32 density,
                              rhi::TextureHandle texture);  // density clamped [1,256]
    void             removePatch(VegetationHandle handle);
    void             renderGrass(const glm::mat4& view, const glm::mat4& proj,
                                 const glm::vec3& cameraPos);

    // Trees
    bool addTree(float x, float y, float z, float scale);  // false if MAX_TREES reached
    void clearTrees();
    void renderTrees();  // two instanced draw calls (trunk + crown)

private:
    // Patch slots — POD, fixed-size arrays, no heap
    // Tree instance storage — fixed-size arrays
};

} // namespace ffe::renderer
```

`VegetationSystem` is constructed as a member of `MeshRenderer` (or equivalent render
coordinator) alongside existing systems. It is NOT a global singleton.

---

## 6. Lua API (4 bindings)

| Binding | Signature | Notes |
|---------|-----------|-------|
| `ffe.addVegetationPatch` | `(terrainHandle: integer, density: integer, textureHandle: integer) → integer` | Returns VegetationHandle.id; density clamped [1,256]; textureHandle=0 = solid green fallback |
| `ffe.removeVegetationPatch` | `(handle: integer) → nil` | No-op on invalid handle |
| `ffe.addTree` | `(x: number, y: number, z: number, scale: number) → nil` | scale defaults to 1.0 if ≤ 0; silently ignores if MAX_TREES reached |
| `ffe.clearTrees` | `() → nil` | Removes all trees; does not affect grass patches |

All four bindings live in `engine/scripting/script_engine.cpp`. No new Lua tables or modules.

---

## 7. Files Modified / Created

| Action | File | Owner |
|--------|------|-------|
| CREATE | `engine/renderer/vegetation.h` | renderer-specialist |
| CREATE | `engine/renderer/vegetation.cpp` | renderer-specialist |
| MODIFY | `engine/renderer/shader_library.h` | renderer-specialist — add `VEGETATION = 16` to `ShaderType` enum |
| MODIFY | `engine/renderer/shader_library.cpp` | renderer-specialist — add VEGETATION shader source + compile |
| MODIFY | `engine/renderer/CMakeLists.txt` | renderer-specialist — add `vegetation.cpp` |
| MODIFY | `engine/scripting/script_engine.cpp` | engine-dev — 4 Lua bindings |
| CREATE | `tests/renderer/test_vegetation.cpp` | engine-dev — min 12 CPU-only tests |
| MODIFY | `tests/CMakeLists.txt` | engine-dev — register test_vegetation |

No new vcpkg dependencies. No new headers in `engine/core/`.

---

## 8. Test Plan (test_vegetation.cpp — min 12 tests)

CPU-only (no OpenGL context). Test logic, not GPU calls.

1. `VegetationHandle` default id is 0; `isValid` returns false.
2. `GrassInstance` is 24 bytes.
3. `TreeInstance` is 16 bytes.
4. Density clamped to 1 when input is 0.
5. Density clamped to 256 when input is 300.
6. Trunk matrix scale X and Z = 0.15 * TreeInstance::scale.
7. Trunk matrix scale Y = 1.0 * TreeInstance::scale.
8. Crown matrix translation Y offset = 1.0 * TreeInstance::scale.
9. Crown matrix scale = 0.7 * TreeInstance::scale on all axes.
10. `MAX_VEGETATION_PATCHES` is 32.
11. `MAX_GRASS_INSTANCES` is 256.
12. `MAX_TREES` is 512.
13. (bonus) Position scatter stays within terrain worldWidth/worldDepth bounds.
14. (bonus) `VEGETATION` shader enum value is 16.

---

## 9. Parallel Phase 2 Split

| Worker | Agent | Files |
|--------|-------|-------|
| A | `renderer-specialist` | `vegetation.h`, `vegetation.cpp`, `shader_library.h`, `shader_library.cpp`, `engine/renderer/CMakeLists.txt` |
| B | `engine-dev` | `engine/scripting/script_engine.cpp`, `tests/renderer/test_vegetation.cpp`, `tests/CMakeLists.txt` |

Workers A and B have no shared files. Both read `vegetation.h` (Worker A writes it first
as the foundation step — A completes vegetation.h before B starts). Then A and B run in
parallel on their remaining files.

**Foundation step (sequential):** Worker A writes `vegetation.h` first and stops.
**Parallel step:** Worker A continues with `vegetation.cpp` + shader + CMake. Worker B
reads the completed `vegetation.h` and writes bindings + tests simultaneously.

---

## 10. Constraints

- No RTTI. No exceptions. No `std::function`. No `virtual` in hot paths.
- No heap allocation per frame. Instance buffers allocated at `init()` and rebuilt only
  on `addPatch`/`addTree`/`clearTrees` (cold paths).
- `getTerrainHeight` is public in `engine/renderer/terrain.h` (line 89). Use it directly.
  No workaround needed.
- Trees render using `MESH_INSTANCED` (enum 12) — no new shader for trees.
- `VEGETATION` shader (enum 16) is grass-only.
- All GLSL 330 core. No extension pragmas.
- Naming: `VegetationSystem`, `VegetationHandle`, `GrassInstance`, `TreeInstance` (PascalCase types).
  Methods: `addPatch`, `removePatch`, `renderGrass`, `addTree`, `clearTrees`, `renderTrees` (camelCase).
- `vegetation.h` includes `terrain.h` for `TerrainHandle` and `rhi_types.h` for `TextureHandle`.
