# ADR-007: 3D Foundation

**Status:** APPROVED — ready for implementation (revised per Session 37 security review)
**Author:** architect
**Date:** 2026-03-06
**Tiers:** LEGACY (primary — all work in this ADR targets LEGACY unless explicitly stated)
**Security Review Required:** YES — this ADR introduces new file I/O (mesh loading from disk), a new Lua API surface, and new asset parsing paths. `security-auditor` must perform a shift-left review of this document before implementation begins.

---

## 1. Problem Statement

FFE Phase 1 is complete. The engine has a full 2D rendering pipeline, ECS, audio, scripting, input, physics, and save/load. Phase 2 begins here.

The goal of this ADR is to extend the renderer to support 3D meshes while preserving full 2D capability. A game that uses no 3D components must behave identically to Phase 1. A game that uses 3D must be able to load a glTF mesh, attach it to an entity, and have it rendered with basic Blinn-Phong lighting.

This document is complete enough that `engine-dev` can implement the full 3D foundation in a single session without any architectural decisions. Every decision has been made here. Every struct field, every Lua binding signature, every shader string, every file to create or modify — all specified.

---

## 2. Scope of This ADR

**In scope (implement now):**
- cgltf dependency (vendored in `third_party/`)
- `Transform3D` and `Mesh` and `Material3D` ECS components
- `MeshHandle` opaque asset handle
- Mesh loading pipeline (`loadMesh`) with path security and size limits
- Per-mesh VAO strategy
- Mesh renderer system (`meshRenderSystem`)
- Blinn-Phong GLSL 330 core shader (compile-time string in `shader_library.cpp`)
- Perspective camera (`m_camera3d` in `Application`)
- `ffe.set3DCamera` and related Lua bindings
- `Application::render()` changes to support 3D pass before 2D pass
- Depth buffer activation (GLFW hint + `GL_DEPTH_TEST` management)
- Catch2 tests
- `.context.md` for `engine/renderer/` updated by `api-designer` post-implementation

**Explicitly out of scope (future ADRs):**
- Skeletal animation / bone hierarchy
- Shadow mapping
- Normal maps / specular maps
- 3D physics integration
- Skybox
- 3D positional audio
- Instanced rendering
- STANDARD / MODERN tier rendering paths for 3D

---

## 3. Decision A: Mesh Library

**Decision: cgltf, vendored in `third_party/cgltf.h`.**

### Evaluation

| Library | Language | Integration | License | glTF 2.0 | Notes |
|---------|----------|-------------|---------|----------|-------|
| **cgltf** | Single-header C | `third_party/` — no vcpkg | MIT | Full | Zero heap by default; caller supplies allocator or uses static parse. Smallest attack surface. |
| tinygltf | C++ header-only | vcpkg or vendor | MIT | Full | Pulls in `nlohmann/json` and `stb_image` as sub-dependencies. Already have both but adds coupling. |
| fastgltf | C++20 | vcpkg | MIT | Full | SIMD-accelerated; appropriate for STANDARD+. Too heavy for LEGACY. Adds a vcpkg dependency. |
| tinyobjloader | C++ header-only | vendor | MIT | OBJ only | OBJ is not glTF 2.0. Not chosen. |

**Rationale for cgltf:** The single-header-C pattern is established FFE practice (stb_image, miniaudio, glad). cgltf has no sub-dependencies. It does no heap allocation by default — it uses a `cgltf_options` struct where the caller can supply an allocator. For FFE we pass a zero-initialised `cgltf_options` which causes cgltf to use its internal malloc/free, but only in the cold loading path (acceptable — see Section J: no per-frame allocations). Attack surface is minimised because the library is a single 7000-line C file with no external dependencies.

**vcpkg.json change:** None. cgltf is embedded as `third_party/cgltf.h`. The commit message must state: `feat(renderer): add 3D mesh loading via cgltf (header in third_party/)`.

**glTF 2.0 is the primary format.** OBJ is not supported in this session.

**Only `.glb` (binary glTF) files are accepted in this session.** Files with a `.gltf` extension are rejected at path validation before any parsing. `.glb` embeds the binary buffer chunk in-file, which eliminates the external `.bin` URI path traversal surface identified in the security review (H-1) and the per-external-file size limit gap (M-1). `.gltf` support can be added in a future ADR once external URI allowlisting is designed and reviewed.

---

## 4. Decision B: New ECS Components

All components live in `engine/renderer/render_system.h` alongside the existing 2D components (`Transform`, `Sprite`, etc.).

### 4.1 Transform3D

```cpp
// 3D transform component. Separate from Transform (2D-only).
// Does NOT replace Transform. 2D entities use Transform; 3D entities use Transform3D.
// Entities that need to appear in both 2D and 3D views may hold both — that is unusual
// and not the primary use case.
struct Transform3D {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};  // 12 bytes
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f}; // 16 bytes — identity quaternion
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};  // 12 bytes
    u32       _pad     = 0;                     //  4 bytes — explicit alignment padding
};
static_assert(sizeof(Transform3D) == 44, "Transform3D must be 44 bytes");
```

**Why quaternion for rotation:** Euler angles suffer from gimbal lock and have ambiguous composition order (XYZ? ZYX?). Quaternions are the universal representation for 3D orientations in game engines. The Lua API accepts Euler angles in degrees for human readability and converts to quaternion internally — this conversion happens in the cold Lua binding path, never per-frame. `glm::quat` is included via the existing `<glm/gtc/quaternion.hpp>` which is already transitively available through glm.

**Why separate from Transform:** `Transform` uses `f32 rotation` (a scalar z-axis angle in radians) — this is correct for 2D. Merging 3D rotation into `Transform` would break every existing 2D system and test. The ECS allows entities to carry either or both.

### 4.2 MeshHandle

```cpp
// Opaque handle to a loaded mesh asset. Value 0 is always invalid (null handle).
// Same pattern as rhi::BufferHandle, rhi::TextureHandle.
struct MeshHandle { u32 id = 0; };
inline bool isValid(const MeshHandle h) { return h.id != 0; }
static_assert(sizeof(MeshHandle) == 4);
```

`MeshHandle` lives in `engine/renderer/mesh_loader.h` alongside the mesh loading API.

### 4.3 Mesh (ECS component)

```cpp
// ECS component: references a loaded mesh asset.
// The GPU resources (VAO, VBO, IBO) are owned by the mesh asset cache,
// not by the component. Destroying a MeshHandle invalidates any Mesh
// components that reference it.
struct Mesh {
    MeshHandle meshHandle;  // 4 bytes — which loaded mesh to render
    u32        _pad = 0;    // 4 bytes
};
static_assert(sizeof(Mesh) == 8, "Mesh component must be 8 bytes");
```

**Why no raw GPU handles in the component:** The ECS component is the logical reference; the asset cache owns the GPU resources. This is the same pattern as `Sprite` which holds a `TextureHandle` (logical reference) rather than a raw `GLuint`. It also means multiple entities can share the same mesh without duplicating GPU state.

### 4.4 Material3D (ECS component)

```cpp
// ECS component: per-entity 3D material override.
// If not present on a 3D entity, the mesh renderer uses default values.
struct Material3D {
    glm::vec4         diffuseColor   = {1.0f, 1.0f, 1.0f, 1.0f}; // 16 bytes
    rhi::TextureHandle diffuseTexture;  // 4 bytes (0 = use default white texture)
    rhi::ShaderHandle  shaderOverride; // 4 bytes (0 = use builtin MESH_BLINN_PHONG shader)
};
static_assert(sizeof(Material3D) == 24, "Material3D must be 24 bytes");
```

---

## 5. Decision C: 2D/3D Coexistence in the Render Loop

**Decision: 3D renders before 2D. Depth test is enabled for the 3D pass and disabled for the 2D pass.**

### 5.1 Render Sequence

The modified `Application::render(float alpha)` sequence is:

```
1. renderPrepareSystem(world, alpha)          [unchanged — populates 2D render queue]
2. if headless: return                         [unchanged]
3. sortRenderQueue(renderQueue)                [unchanged]
4. beginFrame(clearColor)                      [clears color + depth buffers — unchanged]
5. [NEW] 3D pass:
   a. Enable GL_DEPTH_TEST (LESS), enable GL_CULL_FACE (BACK), disable blend
   b. Set VP matrix from m_camera3d
   c. meshRenderSystem(world, m_meshCache, m_shaderLibrary)
   d. Disable GL_DEPTH_TEST, disable GL_CULL_FACE
6. Set VP matrix from 2D camera (shakeCamera)  [moved from pre-batch to here]
7. beginSpriteBatch, draw 2D queue, renderTilemaps, renderParticles, endSpriteBatch
8. flushText
9. endFrame(window)                            [unchanged]
```

### 5.2 Depth Buffer Handling

`beginFrame` already calls `glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)`. This is correct and unchanged.

The GLFW window currently does **not** request a depth buffer explicitly — `GLFW_DEPTH_BITS` is not set in `Application::startup()`, which means GLFW uses its default of 24 bits. On all tested platforms (Linux with Mesa/NV/AMD) this default is honoured for GL 3.3 core profile. However, relying on GLFW defaults is fragile. `engine-dev` must add the following hint to `Application::startup()` before `glfwCreateWindow`:

```cpp
glfwWindowHint(GLFW_DEPTH_BITS, 24);
```

This guarantees the 24-bit depth buffer that the 3D pass requires. There is no performance cost — the buffer already exists at runtime, this just makes the request explicit.

### 5.3 2D Entities Are Unaffected

When no 3D entities exist (i.e., the ECS has no entities with `Mesh` component), `meshRenderSystem` iterates zero entities and returns immediately. The depth buffer is cleared each frame regardless. The 2D pass always runs with `GL_DEPTH_TEST` disabled. An existing 2D-only game sees no behavioural change.

### 5.4 Blending with 3D

The 3D pass uses `BlendMode::NONE` (opaque geometry only in this session). Transparent 3D meshes are not supported in Phase 2 session 1 — they require depth-sorted alpha blending which is a separate ADR. The 2D pass re-enables `BlendMode::ALPHA` (the existing state) so sprites render correctly on top of 3D geometry.

---

## 6. Decision D: VAO Strategy

**Decision: one VAO per loaded mesh, created at mesh load time, owned by the mesh asset cache.**

### 6.1 Rationale

The existing renderer uses a single `s_spriteVao` that reconfigures its attrib pointers when the VBO changes. That approach works for the sprite batch because there is effectively one VBO. For 3D meshes, different meshes have independent VBOs — a separate VAO per mesh eliminates per-draw attrib reconfiguration and matches the standard OpenGL 3.3 practice.

### 6.2 Mesh GPU Record

The mesh asset cache stores per-mesh GPU state:

```cpp
struct MeshGpuRecord {
    GLuint vaoId      = 0;
    GLuint vboId      = 0;  // raw GL IDs, not rhi handles — mesh renderer manages lifecycle
    GLuint iboId      = 0;
    u32    indexCount = 0;
    bool   alive      = false;
};
```

**Why raw `GLuint` rather than `rhi::BufferHandle`:** The mesh VAO must bind the VBO and IBO inside the VAO's state capture. Using `rhi::BufferHandle` for this would require passing handles back into the RHI for raw GL ID lookup, which leaks implementation details. The mesh loader is a renderer subsystem (lives in `engine/renderer/`) and is permitted to use GLAD directly alongside RHI — the same pattern as `sprite_batch.cpp` which calls `glGenBuffers` directly via `rhi::createBuffer`. Here, the VAO creation is done once at load time and owned entirely by the mesh cache, so raw `GLuint` values stored in `MeshGpuRecord` are correct. The `rhi_opengl.cpp` functions for `createBuffer`, `destroyBuffer` are still used for the VBO and IBO creation (they register with the RHI buffer pool for VRAM tracking). The VAO itself is created directly with `glGenVertexArrays` because the RHI has no VAO abstraction (correct — VAOs are an implementation detail of the OpenGL backend, not a portable RHI concept).

### 6.3 No New RHI Functions

No new functions are added to `rhi.h`. The VAO is an OpenGL-only concept that does not belong in the portable RHI. `mesh_loader.cpp` includes `<glad/glad.h>` directly.

---

## 7. Decision E: Depth Buffer Activation

### 7.1 GLFW Hint (cold path — application startup)

Add to `Application::startup()`, immediately before `glfwCreateWindow`:

```cpp
glfwWindowHint(GLFW_DEPTH_BITS, 24);
```

This is the only required change to the window creation path.

### 7.2 Per-Frame Depth State

`beginFrame` already calls `glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)` — **no change required.**

The 3D pass enables `GL_DEPTH_TEST` via `rhi::applyPipelineState` before drawing mesh entities. The `applyPipelineState` function already handles `GL_DEPTH_TEST` enable/disable via the `DepthFunc` field in `PipelineState`. No new RHI functions are needed.

The `meshRenderSystem` must call:
```cpp
rhi::PipelineState ps3d;
ps3d.blend     = rhi::BlendMode::NONE;
ps3d.depth     = rhi::DepthFunc::LESS;
ps3d.cull      = rhi::CullMode::BACK;
ps3d.depthWrite = true;
rhi::applyPipelineState(ps3d);
```

After the 3D pass, restore for the 2D sprite batch:
```cpp
rhi::PipelineState ps2d;
ps2d.blend     = rhi::BlendMode::ALPHA;
ps2d.depth     = rhi::DepthFunc::NONE;
ps2d.cull      = rhi::CullMode::NONE;
ps2d.depthWrite = false;
rhi::applyPipelineState(ps2d);
```

The `applyPipelineState` function uses redundancy elimination (compares desired vs `s_currentPipeline`) so no-op state calls are free.

---

## 8. Decision F: Perspective Camera

**Decision: add `m_camera3d` (type `renderer::Camera`) to `Application`. The existing `m_camera` (2D orthographic) is unchanged.**

### 8.1 Application Member

In `application.h`, add alongside `m_camera`:

```cpp
renderer::Camera m_camera3d;  // 3D perspective camera — set via ffe.set3DCamera()
```

In `Application::startup()`, initialise the 3D camera defaults:

```cpp
m_camera3d.projType    = renderer::ProjectionType::PERSPECTIVE;
m_camera3d.fovDegrees  = 60.0f;
m_camera3d.nearPlane   = 0.1f;
m_camera3d.farPlane    = 1000.0f;
m_camera3d.position    = {0.0f, 0.0f, 5.0f};
m_camera3d.target      = {0.0f, 0.0f, 0.0f};
m_camera3d.up          = {0.0f, 1.0f, 0.0f};
m_camera3d.viewportWidth  = static_cast<f32>(m_config.windowWidth);
m_camera3d.viewportHeight = static_cast<f32>(m_config.windowHeight);
```

The existing `Camera` struct already supports `ProjectionType::PERSPECTIVE` and `computeViewProjectionMatrix` already handles it — confirmed from `camera.h`. No changes to `camera.h` or `camera.cpp` are required.

### 8.2 Exposing the Camera to Lua

The 3D camera is set exclusively via `ffe.set3DCamera`. The Lua binding writes to `m_camera3d` stored in the ECS context:

```cpp
// Emplace in Application::startup() after camera3d initialisation:
m_world.registry().ctx().emplace<renderer::Camera*>(&m_camera3d);
```

The Lua binding retrieves it via `ctx().get<renderer::Camera*>()`.

### 8.3 FPS and Orbit Controller Data Model

FPS and orbit camera controllers are **helper functions, not baked into the Camera struct**. The `Camera` struct remains a pure data struct (no virtual functions, no update logic). Controllers are free functions that compute a new `Camera` state from input and a previous state, then write it.

These are out of scope for this session — they are Phase 2 follow-on work. For now, `ffe.set3DCamera` is the only way to control the 3D camera from Lua.

---

## 9. Decision G: 3D Mesh Shader

**Decision: hardcoded compile-time string in `shader_library.cpp`. Shader name: `MESH_BLINN_PHONG`. Added to the `BuiltinShader` enum.**

### 9.1 Enum Addition

In `shader_library.h`, add to `BuiltinShader`:

```cpp
enum class BuiltinShader : u8 {
    SOLID            = 0,
    TEXTURED         = 1,
    SPRITE           = 2,
    MESH_BLINN_PHONG = 3,  // NEW
    COUNT
};
```

### 9.2 Vertex Shader (GLSL 330 core)

```glsl
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_viewProjection;
uniform mat3 u_normalMatrix;  // transpose(inverse(mat3(u_model)))

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_texcoord;

void main() {
    vec4 worldPos  = u_model * vec4(a_position, 1.0);
    v_fragPos      = worldPos.xyz;
    v_normal       = u_normalMatrix * a_normal;
    v_texcoord     = a_texcoord;
    gl_Position    = u_viewProjection * worldPos;
}
```

### 9.3 Fragment Shader (GLSL 330 core)

```glsl
#version 330 core

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_texcoord;

uniform sampler2D u_diffuseTexture;
uniform vec4      u_diffuseColor;    // Tint multiplied with texture sample
uniform vec3      u_lightDir;        // Directional light direction (world space, normalised)
uniform vec3      u_lightColor;      // RGB, typically (1,1,1)
uniform vec3      u_ambientColor;    // RGB ambient term, typically (0.15, 0.15, 0.15)
uniform vec3      u_viewPos;         // Camera position (world space), for specular

out vec4 fragColor;

void main() {
    vec3  norm      = normalize(v_normal);
    vec3  lightDir  = normalize(-u_lightDir);  // u_lightDir points from scene toward light

    // Diffuse
    float diff      = max(dot(norm, lightDir), 0.0);
    vec3  diffuse   = diff * u_lightColor;

    // Specular (Blinn-Phong half-vector)
    vec3  viewDir   = normalize(u_viewPos - v_fragPos);
    vec3  halfDir   = normalize(lightDir + viewDir);
    float spec      = pow(max(dot(norm, halfDir), 0.0), 32.0);
    vec3  specular  = spec * u_lightColor * 0.3;

    // Combine
    vec4  texSample = texture(u_diffuseTexture, v_texcoord);
    vec3  lighting  = u_ambientColor + diffuse + specular;
    fragColor       = vec4(lighting, 1.0) * texSample * u_diffuseColor;
}
```

### 9.4 Uniform Summary

| Uniform | Type | Set by |
|---------|------|--------|
| `u_model` | `mat4` | `meshRenderSystem` per entity (from `Transform3D`) |
| `u_viewProjection` | `mat4` | `rhi::setViewProjection` at start of 3D pass |
| `u_normalMatrix` | `mat3` | `meshRenderSystem` per entity — `transpose(inverse(mat3(model)))` |
| `u_diffuseTexture` | `sampler2D` | `meshRenderSystem` per entity (from `Material3D`) |
| `u_diffuseColor` | `vec4` | `meshRenderSystem` per entity (from `Material3D`) |
| `u_lightDir` | `vec3` | Scene-global default `(0.5, -1.0, 0.3)` normalised, overridable from Lua |
| `u_lightColor` | `vec3` | Scene-global default `(1.0, 1.0, 1.0)`, overridable from Lua |
| `u_ambientColor` | `vec3` | Scene-global default `(0.15, 0.15, 0.15)`, overridable from Lua |
| `u_viewPos` | `vec3` | Extracted from `m_camera3d.position` at start of 3D pass |

**Scene-global lighting values** are stored as three fields on a new `SceneLighting3D` context struct emplaced in the ECS context at startup:

```cpp
struct SceneLighting3D {
    glm::vec3 lightDir     = glm::normalize(glm::vec3{0.5f, -1.0f, 0.3f});
    glm::vec3 lightColor   = {1.0f, 1.0f, 1.0f};
    glm::vec3 ambientColor = {0.15f, 0.15f, 0.15f};
};
```

Lua can override these via `ffe.setLightDirection`, `ffe.setLightColor`, `ffe.setAmbientColor` (all documented in Section I).

### 9.5 Performance Note on normalMatrix

`transpose(inverse(mat3(model)))` involves a matrix inverse per entity per frame. For LEGACY tier with a reasonable entity budget (100 mesh slots, not all moving), this is acceptable. For non-uniform scale meshes it is mathematically required. If the object uses uniform scale, `mat3(model)` is sufficient — but detecting uniform scale per-entity at runtime adds branching. Compute the full normal matrix for all entities; optimise if profiling warrants it.

---

## 10. Decision H: Mesh Asset Loading Pipeline

### 10.1 Public API — `engine/renderer/mesh_loader.h`

```cpp
#pragma once

#include "core/types.h"

namespace ffe::renderer {

// Opaque handle to a GPU-resident mesh.
// Value 0 is always invalid.
struct MeshHandle { u32 id = 0; };
inline bool isValid(const MeshHandle h) { return h.id != 0; }

// Maximum mesh assets loaded simultaneously.
inline constexpr u32 MAX_MESH_ASSETS = 100;

// Maximum glTF file size accepted by loadMesh().
inline constexpr u64 MESH_FILE_SIZE_LIMIT = 64ull * 1024ull * 1024ull; // 64 MB

// Maximum vertex count per mesh (single primitive).
inline constexpr u32 MAX_MESH_VERTICES = 1'000'000;

// Maximum index count per mesh.
inline constexpr u32 MAX_MESH_INDICES = 3'000'000;

// Load a glTF 2.0 mesh from a file path relative to the asset root.
// Path validation identical to texture_loader (path traversal prevention, realpath check).
// On success: returns a valid MeshHandle.
// On failure: returns MeshHandle{0} and logs via FFE_LOG_ERROR.
// Loads the FIRST mesh primitive from the FIRST mesh in the glTF scene.
// Not a hot-path operation. Call at scene load time only.
MeshHandle loadMesh(const char* path);

// Destroy a loaded mesh and free all GPU resources (VAO, VBO, IBO).
// Safe to call with an invalid handle.
void unloadMesh(MeshHandle handle);

// Destroy all loaded meshes. Called by Application::shutdown().
void unloadAllMeshes();

// Forward declaration for mesh renderer — returns pointer to internal record.
// Only for use by meshRenderSystem in mesh_renderer.cpp.
struct MeshGpuRecord;
const MeshGpuRecord* getMeshGpuRecord(MeshHandle handle);

} // namespace ffe::renderer
```

### 10.2 Data Flow

```
1. Path validation (same rules as texture_loader — see SEC constraints below);
   additionally, reject any path whose extension is not ".glb" (case-insensitive) — SEC-M8
2. File size check: stat() the file, reject if > MESH_FILE_SIZE_LIMIT
3. cgltf_parse_file() — parses the binary glTF header (always .glb at this point)
4. cgltf_load_buffers() — loads the embedded BIN chunk from the .glb
4a. [NEW] For every buffer in data->buffers: verify buffer->data != nullptr and
    buffer->data_size >= buffer->size. Reject + cgltf_free + return error if any fail — SEC-M3/H-2
5. cgltf_validate() — validates the parsed scene
6. Extract first primitive from first mesh:
   a. Verify primitive type is TRIANGLES
   b. Read POSITION accessor (required) → f32[3] array
   c. Read NORMAL accessor (required) → f32[3] array
   d. Read TEXCOORD_0 accessor (optional, default to 0,0 if absent)
   e. Read indices accessor (required — unindexed meshes not supported in session 1)
7. Validate counts: vertex count <= MAX_MESH_VERTICES, index count <= MAX_MESH_INDICES
8. Pack into MeshVertex array (using rhi::MeshVertex — already defined in rhi_types.h)
9. Upload to GPU:
   a. vboId = RHI createBuffer(VERTEX, STATIC, meshVertices, sizeBytes)
   b. iboId = RHI createBuffer(INDEX, STATIC, indices, sizeBytes)
   c. glGenVertexArrays(1, &vaoId)
   d. glBindVertexArray(vaoId)
   e. glBindBuffer(GL_ARRAY_BUFFER, rawGlId(vboId))
   f. Configure attribs per Section 10.3
   g. glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rawGlId(iboId))
   h. glBindVertexArray(0)
10. Free cgltf data: cgltf_free()
11. CPU vertex/index arrays freed (were stack or arena — see note below)
12. Store MeshGpuRecord in s_meshPool[slot]
13. Return MeshHandle{slot}
```

**Note on CPU-side vertex data:** The vertex and index arrays are allocated from the frame arena during loading. The frame arena is large enough for a 1M-vertex mesh (1M * 32 bytes = 32 MB, within the 48 MB default arena). If the mesh exceeds arena capacity, `loadMesh` falls back to a heap allocation using `new (std::nothrow)` — the nothrow form is required because engine core disables exceptions. The result must be checked for null immediately; if null, call `cgltf_free` and return `MeshHandle{0}` (see M-2). The heap allocation is freed immediately after GPU upload. Either way, CPU-side data is discarded after upload and no per-frame CPU copy is retained.

**Getting the raw GL ID from an rhi handle:** `mesh_loader.cpp` includes `renderer/opengl/rhi_opengl.h` which exposes `getGlBufferId(BufferHandle)` — a thin accessor already present in the internal header for use by sprite_batch (check `rhi_opengl.h`; if not present, `engine-dev` adds it). If `rhi_opengl.h` does not expose this function, `engine-dev` adds:

```cpp
// In engine/renderer/opengl/rhi_opengl.h
GLuint getGlBufferId(rhi::BufferHandle handle);
```

### 10.3 VAO Attribute Layout

Matches `rhi::MeshVertex` exactly (already defined in `rhi_types.h`):

```
layout(location = 0): position — 3 floats, offset 0,      stride 32
layout(location = 1): normal   — 3 floats, offset 12,     stride 32
layout(location = 2): texcoord — 2 floats, offset 24,     stride 32
```

```cpp
// location 0 — position
glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(rhi::MeshVertex),
                      reinterpret_cast<const void*>(0));

// location 1 — normal
glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(rhi::MeshVertex),
                      reinterpret_cast<const void*>(12));

// location 2 — texcoord
glEnableVertexAttribArray(2);
glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(rhi::MeshVertex),
                      reinterpret_cast<const void*>(24));
```

### 10.4 Security Constraints (input to security-auditor shift-left review)

These are numbered for traceability. Each must be enforced in the implementation.

**SEC-M1: Path Traversal Prevention**
Apply identical validation to `texture_loader.cpp`'s `isPathSafe()`:
- Reject null, empty
- Reject paths starting with `/` or `\`
- Reject paths containing `../`, `..\`, `/..`, `\..`
- Reject embedded null bytes
- Concatenate asset root + `/` + path, call `realpath()`, verify canonical path begins with asset root prefix
- Only after both checks pass does `cgltf_parse_file` receive the full path

**SEC-M2: File Size Limit**
`stat()` the file before calling `cgltf_parse_file`. Reject if `st_size > MESH_FILE_SIZE_LIMIT` (64 MB). This prevents cgltf from attempting to load a multi-gigabyte file.

**SEC-M3: cgltf Output Validation**
cgltf parses untrusted binary data. After `cgltf_parse_file` and `cgltf_load_buffers`:
- Verify `result == cgltf_result_success` at each step
- Verify `cgltf_validate()` returns `cgltf_result_success`
- Verify at least one mesh and one primitive exist
- Verify primitive type is `cgltf_primitive_type_triangles`
- Verify POSITION accessor exists, component type is `cgltf_component_type_r_32f`, type is `cgltf_type_vec3`
- Verify NORMAL accessor (if present) is `cgltf_component_type_r_32f`, type `cgltf_type_vec3`
- Verify index accessor exists (unindexed meshes rejected), component type is `cgltf_component_type_r_16u` or `r_32u`
- **[H-2 addition]** After `cgltf_load_buffers` succeeds, verify that for every buffer in `data->buffers`: `buffer->data != nullptr` and `buffer->data_size >= buffer->size`. If any buffer fails this check, reject the mesh, call `cgltf_free`, and return an error. This catches truncated `.glb` BIN chunks before any vertex extraction.
- **[H-2 addition]** Vertex and index extraction (Step 6) must use the cgltf safe accessor API exclusively — `cgltf_accessor_read_float()` and `cgltf_accessor_read_index()`. Direct pointer arithmetic on `buffer->data` or `buffer_view->data` is prohibited. The safe accessor API performs its own bounds checking and is the only extraction path that is safe against malformed accessor offsets/strides.

**SEC-M4: Vertex and Index Count Limits**
After reading accessor counts from cgltf:
```
if accessor_count > MAX_MESH_VERTICES → reject
if index_count > MAX_MESH_INDICES → reject
```
Check these before any memory allocation or GPU upload.

**SEC-M5: Integer Overflow in Size Calculations**
All size calculations use `u64` arithmetic before comparison:
```cpp
const u64 vboBytes = static_cast<u64>(vertexCount) * sizeof(rhi::MeshVertex);
const u64 iboBytes = static_cast<u64>(indexCount)  * sizeof(u32);
```
Never compute sizes in 32-bit arithmetic.

**SEC-M6: cgltf_free on all paths**
`cgltf_free(&cgtlfData)` must be called on every exit path after `cgltf_parse_file` succeeds, including all error paths. Use RAII or a goto-cleanup label pattern.

**SEC-M7: No per-frame loading**
`loadMesh` is explicitly a cold-path function. Its header comment must state this. The implementation may assert in debug builds if called during a frame (i.e., if the frame allocator has already been reset for the current frame — use a flag).

**SEC-M8 — .glb only (no external URI references)**
`loadMesh` only accepts `.glb` files (binary glTF). Files with a `.gltf` extension are rejected at path validation before any parsing. This check is performed as the first extension check in `isPathSafe` (after null/empty and traversal checks), so no file I/O or cgltf call is ever made for a non-`.glb` path. `.glb` embeds the binary buffer chunk in-file, eliminating the external `.bin` URI path traversal surface (H-1) and the per-external-file size limit gap (M-1). OBJ support is explicitly deferred. This restriction can be lifted in a future session once external URI allowlisting is designed and reviewed.

**Implementation constraint — M-2 (heap fallback null handling):**
If the CPU-side vertex/index staging buffer must fall back from the frame arena to the heap (see Section 10.2 note), use `new (std::nothrow)`. Check the result for null before proceeding. If null, call `cgltf_free` and return `MeshHandle{0}`.

**Implementation constraint — M-3 (GPU OOM detection):**
After each `glBufferData` call (vertex buffer and index buffer upload in Step 9), call `glGetError()` and check for `GL_OUT_OF_MEMORY`. If GPU OOM is detected, free the already-allocated GPU objects (`glDeleteBuffers` for VBO and IBO, `glDeleteVertexArrays` for the VAO if already created), call `cgltf_free`, and return `MeshHandle{0}`. This is a cold path so the `glGetError()` calls are acceptable.

**Implementation constraint — M-4 (zero-length lightDir guard):**
The `ffe.setLightDirection(x, y, z)` Lua binding must validate that the input vector is not zero-length before normalising. Compute `float len = glm::length(inputVec)`. If `len < 0.0001f`, log a warning and leave the existing `SceneLighting3D.lightDir` value unchanged (do not write the zero vector or a NaN-normalised value). Only if `len >= 0.0001f` normalise and store. This prevents `normalize(0,0,0)` from producing NaN in the GLSL fragment shader.

---

## 11. Decision I: Lua Bindings

All new bindings are added to `engine/scripting/lua_bindings.cpp` (the existing Lua binding file). No new files.

### 11.1 Exact Function Signatures

```lua
-- Load a glTF mesh from a path relative to the asset root.
-- Returns an integer mesh handle (> 0 on success, 0 on failure).
-- Cold path only. Call at game init, not per frame.
local meshHandle = ffe.loadMesh(path: string) -> integer

-- Create an entity with Transform3D and Mesh components at the given position.
-- Returns the entity ID.
-- meshHandle must be > 0.
local entityId = ffe.createEntity3D(meshHandle: integer, x: number, y: number, z: number) -> integer

-- Set the full 3D transform of an entity.
-- rx, ry, rz are Euler angles in DEGREES (converted to quaternion internally).
-- Rotation order: Y first, then X, then Z (YXZ — standard game convention, avoids most gimbal lock).
-- sx, sy, sz are scale factors (1.0 = no scale).
ffe.setTransform3D(entityId: integer, x: number, y: number, z: number,
                   rx: number, ry: number, rz: number,
                   sx: number, sy: number, sz: number)

-- Set the perspective camera position and look-at target.
-- All values are world-space coordinates.
ffe.set3DCamera(x: number, y: number, z: number,
                targetX: number, targetY: number, targetZ: number)

-- Set the diffuse color tint on a 3D entity's Material3D component.
-- r, g, b, a are in [0.0, 1.0].
-- Creates Material3D if not present on the entity.
ffe.setMeshColor(entityId: integer, r: number, g: number, b: number, a: number)

-- Set the scene directional light direction (world space, does not need to be normalised).
-- Normalisation happens in the C++ binding.
ffe.setLightDirection(x: number, y: number, z: number)

-- Set the directional light color. Values typically [0.0, 1.0].
ffe.setLightColor(r: number, g: number, b: number)

-- Set the ambient light color. Values typically [0.0, 0.15].
ffe.setAmbientColor(r: number, g: number, b: number)
```

### 11.2 C++ Binding Implementation Notes

- `ffe.loadMesh`: calls `renderer::loadMesh(path)`, returns `(int)handle.id`. If handle is invalid, returns 0.
- `ffe.createEntity3D`: calls `world.createEntity()`, emplaces `Transform3D` with given position, emplaces `Mesh{MeshHandle{meshHandle}}`. Returns `(int)entity`.
- `ffe.setTransform3D`: validates entity exists; constructs `glm::quat` from Euler YXZ degrees using `glm::quat(glm::radians(glm::vec3{rx, ry, rz}))` — this uses GLM's euler-to-quat constructor with YXZ order when using `glm::quat(vec3)`. Writes to `Transform3D`. If entity has no `Transform3D`, emplaces one.
- `ffe.set3DCamera`: retrieves `renderer::Camera*` from `ctx().get<renderer::Camera*>()`, sets `position` and `target`. The `up` vector remains `{0,1,0}` unless overridden.
- `ffe.setMeshColor`: retrieves or emplaces `Material3D` on entity, sets `diffuseColor`.
- `ffe.setLightDirection`, `ffe.setLightColor`, `ffe.setAmbientColor`: retrieve `SceneLighting3D` from ECS context, set the appropriate field. `setLightDirection` normalises the input.

### 11.3 Error Handling in Bindings

All bindings that receive entity IDs or mesh handles validate them before use:
- Invalid entity ID (handle not alive in registry): log error, return 0 or nil
- Invalid mesh handle (id == 0 or slot not alive): log error, return 0
- Path null or empty: log error, return 0

No Lua errors (`lua_error`) are thrown from these paths — return 0 and log, consistent with existing binding conventions.

---

## 12. Decision J: File Layout

### 12.1 New Files to Create

| File | Owner | Description |
|------|-------|-------------|
| `engine/renderer/mesh_loader.h` | engine-dev | `MeshHandle`, `loadMesh`, `unloadMesh`, `unloadAllMeshes`, `getMeshGpuRecord` declarations |
| `engine/renderer/mesh_loader.cpp` | engine-dev | Implementation: cgltf integration, path security, GPU upload, mesh asset cache |
| `engine/renderer/mesh_renderer.h` | engine-dev | `meshRenderSystem` declaration, `SceneLighting3D` struct |
| `engine/renderer/mesh_renderer.cpp` | engine-dev | `meshRenderSystem` implementation: ECS query, model matrix construction, uniform upload, indexed draw |
| `third_party/cgltf.h` | engine-dev | cgltf single-header library (download from https://github.com/jkuhlmann/cgltf, latest v1.x release) |
| `tests/renderer/test_mesh_loader.cpp` | engine-dev | Catch2 unit tests (see Section L) |

### 12.2 Existing Files to Modify

| File | Change |
|------|--------|
| `engine/core/application.h` | Add `renderer::Camera m_camera3d;` member |
| `engine/core/application.cpp` | (1) Add `glfwWindowHint(GLFW_DEPTH_BITS, 24)` before `glfwCreateWindow`. (2) Initialise `m_camera3d` defaults. (3) Emplace `renderer::Camera*` and `SceneLighting3D` into ECS context. (4) Register `meshRenderSystem` call in `render()`. (5) Add `#include "renderer/mesh_loader.h"` and `#include "renderer/mesh_renderer.h"`. (6) Call `renderer::unloadAllMeshes()` in `shutdown()`. |
| `engine/renderer/shader_library.h` | Add `MESH_BLINN_PHONG = 3` to `BuiltinShader` enum. Update `COUNT`. |
| `engine/renderer/shader_library.cpp` | Add Blinn-Phong vertex + fragment shader source strings (from Section G). Register and compile in `initShaderLibrary`. |
| `engine/renderer/CMakeLists.txt` | Add `mesh_loader.cpp` and `mesh_renderer.cpp` to `RENDERER_SOURCES`. |
| `engine/scripting/lua_bindings.cpp` | Add all new Lua bindings from Section I. |
| `tests/CMakeLists.txt` | Add `tests/renderer/test_mesh_loader.cpp` to the test build. |
| `engine/renderer/opengl/rhi_opengl.h` | Add `GLuint getGlBufferId(rhi::BufferHandle handle)` if not already present. |
| `engine/renderer/render_system.h` | Add `Transform3D`, `Mesh`, `Material3D` struct definitions. |

---

## 13. Decision K: LEGACY Tier Constraints

All 3D rendering in this session targets LEGACY tier. The following OpenGL 3.3 constraints apply:

### 13.1 No DSA (Direct State Access)

DSA (`glNamedBufferData`, `glVertexArrayAttribFormat`, etc.) requires OpenGL 4.5. Not available on LEGACY.

Use only:
- `glGenVertexArrays` / `glBindVertexArray` / `glDeleteVertexArrays`
- `glGenBuffers` / `glBindBuffer` / `glBufferData` / `glDeleteBuffers`
- `glVertexAttribPointer` (not `glVertexAttribFormat`)
- `glEnableVertexAttribArray` (not `glEnableVertexArrayAttrib`)

### 13.2 Depth Buffer

24-bit depth buffer (requested via `GLFW_DEPTH_BITS, 24`). No 32-bit float depth, no packed depth-stencil.

### 13.3 No Instanced Rendering

`glDrawElementsInstanced` is available in GL 3.3 but not used in this session. All meshes are drawn with separate `glDrawElements` calls. Instancing is a STANDARD-tier optimisation for future work.

### 13.4 No Shadow Mapping

Shadow maps require render-to-texture (FBOs) and depth comparison samplers. Excluded from this session. Planned for a future Phase 2 ADR targeting STANDARD tier.

### 13.5 No Skeletal Animation

Bone matrices and skinning require uniform buffer objects or texture uploads per entity. Excluded from this session.

### 13.6 VRAM Budget

The mesh asset pool is a fixed-size array of `MAX_MESH_ASSETS = 100` slots. This is the hard cap on simultaneously loaded mesh assets.

Estimated VRAM per mesh (ballpark for small game meshes):
- 10K vertices * 32 bytes/vertex = 320 KB VBO
- 20K indices * 4 bytes/index = 80 KB IBO
- Total ~400 KB per mesh * 100 = ~40 MB

40 MB is within the 1 GB LEGACY tier VRAM budget with significant headroom for textures and the framebuffer.

Large meshes (up to the 1M vertex / 3M index limits) would require ~32 MB + ~12 MB = ~44 MB each. Users loading very large meshes must manage the slot budget manually. A warning is logged when the pool is more than 75% full.

### 13.7 Index Type

All indices are stored internally as `u32` (32-bit unsigned integer). cgltf may provide `u16` indices; these are widened to `u32` during the CPU-side packing step. GPU upload uses `GL_UNSIGNED_INT`. This simplifies the index handling path at a cost of double the index memory versus `u16` — acceptable for a 64 MB file cap and the LEGACY VRAM budget.

### 13.8 No Compute Shaders

LEGACY tier OpenGL 3.3 core does not support compute shaders. No compute used anywhere in this ADR.

---

## 14. Decision L: Test Coverage Plan

### 14.1 Test File: `tests/renderer/test_mesh_loader.cpp`

All tests use programmatic vertex data — no dependency on external `.glb` files in the test suite. This avoids test fragility from missing assets.

#### Helper: Programmatic Unit Cube

Define a minimal unit-cube mesh in the test file itself as inline C++ data arrays:

```cpp
// Unit cube: 8 vertices, 36 indices (12 triangles, 2 per face, 6 faces)
// All normals point outward, texcoords are trivial (0,0 for all vertices).
static const rhi::MeshVertex UNIT_CUBE_VERTICES[8] = {
    // position              normal          uv
    {-0.5f,-0.5f,-0.5f,  0.f,0.f,-1.f,  0.f,0.f},
    { 0.5f,-0.5f,-0.5f,  0.f,0.f,-1.f,  1.f,0.f},
    { 0.5f, 0.5f,-0.5f,  0.f,0.f,-1.f,  1.f,1.f},
    {-0.5f, 0.5f,-0.5f,  0.f,0.f,-1.f,  0.f,1.f},
    {-0.5f,-0.5f, 0.5f,  0.f,0.f, 1.f,  0.f,0.f},
    { 0.5f,-0.5f, 0.5f,  0.f,0.f, 1.f,  1.f,0.f},
    { 0.5f, 0.5f, 0.5f,  0.f,0.f, 1.f,  1.f,1.f},
    {-0.5f, 0.5f, 0.5f,  0.f,0.f, 1.f,  0.f,1.f},
};
static const u32 UNIT_CUBE_INDICES[36] = {
    0,1,2, 2,3,0,  // -Z face
    4,5,6, 6,7,4,  // +Z face
    0,4,7, 7,3,0,  // -X face
    1,5,6, 6,2,1,  // +X face
    3,2,6, 6,7,3,  // +Y face
    0,1,5, 5,4,0,  // -Y face
};
```

Tests that require GPU operations (VAO creation, buffer upload) run in headless mode only (the RHI headless path does not call any GL functions). Tests that validate pure CPU-side logic (path validation, size checking, cgltf parsing) do not require the RHI at all.

#### Test Cases to Implement

**Path validation (CPU-only, no RHI):**
- `TEST_CASE("loadMesh: null path returns invalid handle")`
- `TEST_CASE("loadMesh: empty path returns invalid handle")`
- `TEST_CASE("loadMesh: absolute path rejected")`
- `TEST_CASE("loadMesh: path traversal ../ rejected")`
- `TEST_CASE("loadMesh: path traversal /../ rejected")`
- `TEST_CASE("loadMesh: path traversal embedded null rejected")`
- `TEST_CASE("loadMesh: valid relative path to non-existent file returns invalid handle")`

**Struct layout (no RHI):**
- `TEST_CASE("MeshVertex: sizeof == 32")`
- `TEST_CASE("Transform3D: sizeof == 44")`
- `TEST_CASE("Mesh: sizeof == 8")`
- `TEST_CASE("Material3D: sizeof == 24")`
- `TEST_CASE("MeshHandle: isValid returns false for id==0")`

**Asset pool (headless RHI):**
- `TEST_CASE("unloadMesh: safe to call with invalid handle")`
- `TEST_CASE("unloadAllMeshes: safe to call when pool empty")`

**SceneLighting3D (CPU-only):**
- `TEST_CASE("SceneLighting3D: default lightDir is normalised")` — `glm::length(defaults.lightDir)` approximately 1.0

**Camera (CPU-only):**
- `TEST_CASE("camera3d: computeViewProjectionMatrix returns non-identity for perspective")`

**ECS integration (headless):**
- `TEST_CASE("createEntity3D Lua path: entity has Transform3D and Mesh components")`
- `TEST_CASE("setMeshColor: Material3D diffuseColor updated correctly")`
- `TEST_CASE("setTransform3D: position and scale set correctly")`
- `TEST_CASE("setTransform3D: Euler 0,0,0 produces identity quaternion")`
- `TEST_CASE("setTransform3D: Euler 90,0,0 around X produces expected quaternion")`

---

## 15. New Dependencies

| Dependency | Source | vcpkg.json change |
|------------|--------|-------------------|
| cgltf | `third_party/cgltf.h` (embedded, single header) | **No** — same pattern as stb_image, miniaudio |

The commit message must include: `feat(renderer): 3D mesh loading and rendering via cgltf (header in third_party/)`.

**glm quaternion support** (`<glm/gtc/quaternion.hpp>`) is already available transitively through the existing glm vcpkg dependency. No new vcpkg entry required.

---

## 16. Open Questions for security-auditor (Shift-Left Review)

The following questions are raised explicitly for the shift-left review. security-auditor should answer each.

**Q-M1: cgltf attack surface.** cgltf parses untrusted binary data (the binary buffer within a .glb). Is the `cgltf_validate()` call sufficient to prevent malformed buffer data from causing out-of-bounds accessor reads during the vertex extraction step? Or does FFE need to bounds-check each `cgltf_accessor_read_float` call independently?

**Q-M2: .gltf vs .glb.** .gltf files have an external `.bin` buffer loaded by `cgltf_load_buffers`. The `.bin` path is specified inside the JSON. Is there a path traversal risk in the `.bin` buffer reference being resolved relative to the `.gltf` file location? cgltf resolves buffer URIs relative to the base path — is the base path constrained to the asset root?

**Q-M3: File size limit adequacy.** The 64 MB limit applies to the primary file read. A `.gltf` + external `.bin` pair could have a small `.gltf` JSON and a very large `.bin`. Does the 64 MB limit need to apply to the `.bin` as well, and how is that enforced through cgltf's API?

**Q-M4: Arena fallback to heap.** Section H notes that very large meshes may fall back to a heap allocation for the CPU-side vertex staging buffer. Is this fallback path safe? Specifically: if heap allocation fails (`new` returns nullptr or throws), does the error path correctly call `cgltf_free` and return an invalid handle?

---

## 17. Session Sequencing Note

This ADR introduces new file I/O (mesh loading), new asset parsing (cgltf), and new Lua API surface. Per CLAUDE.md Section 7 (Shift-Left Security Review), `security-auditor` reviews this document **before** implementation begins. Implementation is blocked until security-auditor returns no CRITICAL or HIGH findings. MEDIUM or LOW findings are addressed in Phase 4 (remediation) of the development flow.

After implementation:
- Phase 3 Expert Panel runs in parallel: `performance-critic`, `security-auditor` (post-impl), `api-designer`
- `api-designer` updates `engine/renderer/.context.md` with 3D API documentation
- `game-dev-tester` is invoked (new API paradigm — 3D rendering is categorically new, not just a new binding in an existing pattern)

---

## 18. Revision History

| Version | Date | Author | Summary |
|---------|------|--------|---------|
| v1.0 | 2026-03-06 | architect | Initial ADR — approved, pending security-auditor shift-left review |
| v1.1 | 2026-03-06 | architect | Revised per `ADR-007-security-review.md` (Session 37 shift-left review). HIGH findings H-1 and H-2 resolved. MEDIUM findings M-2, M-3, M-4 documented as implementation constraints. Specific changes: (1) Added SEC-M8 (`.glb`-only restriction, resolves H-1 and implicitly M-1). (2) Expanded SEC-M3 to require `buffer->data_size >= buffer->size` check after `cgltf_load_buffers`, and to require exclusive use of `cgltf_accessor_read_float`/`cgltf_accessor_read_index` safe accessor API (resolves H-2). (3) Added implementation constraints for M-2 (heap fallback `new (std::nothrow)` + null check), M-3 (`glGetError()` after `glBufferData` for GPU OOM), and M-4 (`ffe.setLightDirection` zero-vector guard). (4) Updated data flow steps 3, 4, and 4a to reflect `.glb`-only parsing and buffer data-size validation. (5) Updated mesh library rationale in Section 3 and data flow note in Section 10.2. |

*ADR-007 v1.1 — approved, HIGH findings resolved, implementation unblocked.*
