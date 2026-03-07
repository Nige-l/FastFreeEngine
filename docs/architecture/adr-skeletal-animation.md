# ADR: Skeletal Animation

**Status:** DRAFT — awaiting security shift-left review
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (OpenGL 3.3 core)
**Security Review Required:** YES — extends glTF parsing (new data paths from untrusted .glb files)

---

## 1. Problem Statement

FFE can load and render static 3D meshes (ADR-007). The next step is skeletal animation: loading skinned meshes from glTF, computing bone transforms per frame, and applying them in the vertex shader. This ADR specifies the data structures, GPU skinning approach, shader changes, and Lua API.

---

## 2. Scope

**In scope:**
- Bone hierarchy data structures and ECS components
- Animation clip storage and playback
- GPU skinning via vertex shader (uniform bone matrices)
- Extending cgltf mesh loading to parse skins and animations
- Two new shaders: `MESH_SKINNED` and `SHADOW_DEPTH_SKINNED`
- Lua API for animation control
- Catch2 tests

**Out of scope:**
- Animation blending / crossfade (future)
- Inverse kinematics (future)
- Morph targets / blend shapes (future)
- Animation events / callbacks (future)
- STANDARD/MODERN tier optimizations (SSBO, compute skinning)

---

## 3. Bone Hierarchy

### Constants

```cpp
inline constexpr u32 MAX_BONES = 64;
inline constexpr u32 MAX_BONE_INFLUENCES = 4; // per vertex
```

**Why 64:** OpenGL 3.3 guarantees at least 1024 uniform components per vertex shader (`GL_MAX_VERTEX_UNIFORM_COMPONENTS`). 64 mat4 = 64 * 16 = 1024 floats = 4096 bytes. This is within spec and covers the vast majority of game-ready character models. Models exceeding 64 bones must be rejected at load time with a clear error.

### Bone Data (stored per mesh asset, not per entity)

```cpp
struct BoneInfo {
    glm::mat4 inverseBindMatrix;   // transforms vertex from model space to bone space
    i32       parentIndex;         // -1 for root bones
};

struct SkeletonData {
    BoneInfo bones[MAX_BONES];
    u32      boneCount = 0;
};
```

`SkeletonData` is owned by the mesh asset cache (alongside `MeshGpuRecord`). Multiple entities sharing the same `MeshHandle` share the same `SkeletonData`. This is read-only after load.

---

## 4. Animation Clip Storage

### Data Structures (stored per mesh asset)

```cpp
inline constexpr u32 MAX_ANIMATIONS_PER_MESH = 16;
inline constexpr u32 MAX_KEYFRAMES_PER_CHANNEL = 256;

struct AnimationChannel {
    u32       boneIndex;
    // Separate arrays for T, R, S channels.
    // Not all channels are present — count == 0 means channel unused.
    f32       translationTimes[MAX_KEYFRAMES_PER_CHANNEL];
    glm::vec3 translationValues[MAX_KEYFRAMES_PER_CHANNEL];
    u32       translationCount = 0;

    f32       rotationTimes[MAX_KEYFRAMES_PER_CHANNEL];
    glm::quat rotationValues[MAX_KEYFRAMES_PER_CHANNEL];
    u32       rotationCount = 0;

    f32       scaleTimes[MAX_KEYFRAMES_PER_CHANNEL];
    glm::vec3 scaleValues[MAX_KEYFRAMES_PER_CHANNEL];
    u32       scaleCount = 0;
};
```

**Note on memory:** A single `AnimationChannel` with all three channels at max capacity is large (~25 KB). In practice, most channels have far fewer keyframes. The implementing agent should consider a more compact layout if profiling shows memory pressure — but for LEGACY tier with MAX_BONES=64 and typical game assets, the fixed-size approach avoids heap allocations in hot paths. An alternative is a flat arena-allocated array with offset+count per channel; the implementer may choose either approach as long as no per-frame heap allocation occurs.

```cpp
struct AnimationClipData {
    AnimationChannel channels[MAX_BONES]; // one channel per bone (sparse — unused bones have count=0)
    f32 duration = 0.0f;                  // total clip duration in seconds
    u32 channelCount = 0;                 // number of bones actually animated
};

struct MeshAnimations {
    AnimationClipData clips[MAX_ANIMATIONS_PER_MESH];
    u32 clipCount = 0;
};
```

`MeshAnimations` is owned by the mesh asset cache alongside `SkeletonData`.

### Interpolation

- **Translation:** linear interpolation (`glm::mix`)
- **Rotation:** spherical linear interpolation (`glm::slerp`) on quaternions
- **Scale:** linear interpolation (`glm::mix`)

Binary search on the keyframe time arrays to find the bracketing keyframes. Time complexity: O(log n) per channel per bone.

---

## 5. ECS Components

### `Skeleton` Component (per entity)

```cpp
struct Skeleton {
    glm::mat4 boneMatrices[MAX_BONES]; // final bone transforms for this frame (model-space)
    u32       boneCount = 0;
};
```

**Size:** 64 * 64 + 4 = 4100 bytes per entity. This is the per-entity mutable state — each entity with the same mesh may be at a different animation frame. `boneMatrices` holds the final transforms (inverse bind * current pose) ready for GPU upload.

**Why per-entity:** Two characters sharing the same mesh asset may play different animations at different times. The skeleton pose is instance state, not asset state.

### `AnimationState` Component (per entity)

```cpp
struct AnimationState {
    u32  clipIndex  = 0;      // index into MeshAnimations::clips[]
    f32  time       = 0.0f;   // current playback time in seconds
    f32  speed      = 1.0f;   // playback speed multiplier (1.0 = normal)
    bool looping    = true;
    bool playing    = false;
};
```

**Size:** 16 bytes. Lightweight — no pointers, no heap.

### Component Registration (in `render_system.h`)

Both components are added to the existing ECS components section. They are optional — a 3D entity without `Skeleton` + `AnimationState` renders as a static mesh (existing behavior, zero overhead).

---

## 6. glTF Parsing Extensions

Extend `mesh_loader.cpp` to parse skins and animations from cgltf data. All parsing happens at load time (cold path).

### Skin Parsing

1. After loading the mesh primitive (existing code), check `cgltf_data->skins_count`.
2. If `skins_count > 0`, parse `skins[0]`:
   - Read `joints` array to build the bone hierarchy (parent indices from node tree).
   - Read `inverse_bind_matrices` accessor into `SkeletonData::bones[].inverseBindMatrix`.
   - Validate: `joints_count <= MAX_BONES`. Reject with error if exceeded.
3. Parse vertex attributes `JOINTS_0` (uvec4) and `WEIGHTS_0` (vec4) from the mesh primitive.
   - These are uploaded to the VBO alongside position/normal/texcoord.
   - Vertex layout expands from `{pos, normal, texcoord}` to `{pos, normal, texcoord, joints, weights}`.

### Animation Parsing

1. Iterate `cgltf_data->animations` (up to `MAX_ANIMATIONS_PER_MESH`).
2. For each animation, iterate its channels:
   - Map `channel.target_node` to a bone index via the skin's joint list.
   - Read the sampler's input (times) and output (values) accessors.
   - Validate: keyframe count <= `MAX_KEYFRAMES_PER_CHANNEL`. Truncate with warning if exceeded.
   - Store into `AnimationClipData`.
3. Compute `duration` as `max(time)` across all channels in the clip.

### Security Considerations

- All accessor reads must be bounds-checked (existing cgltf validation + our own checks).
- Bone count, keyframe count, and animation count are capped by constants.
- Joint indices in vertex data must be validated: `joint_index < boneCount`. Reject mesh if any vertex references an out-of-range bone.
- Weight values should be normalized (sum to 1.0). Renormalize at load time if they don't.

### Extended MeshGpuRecord

```cpp
struct MeshGpuRecord {
    // ... existing fields ...
    bool hasSkeleton = false;  // true if this mesh has skin data
};
```

`SkeletonData` and `MeshAnimations` are stored in parallel arrays in the mesh asset cache, indexed by `MeshHandle::id`, same as `MeshGpuRecord`.

---

## 7. Shader Modifications

### New Shaders

Add two new entries to `BuiltinShader` enum:

```cpp
MESH_SKINNED        = 6,  // Blinn-Phong + bone skinning
SHADOW_DEPTH_SKINNED = 7, // Depth-only + bone skinning
```

### `MESH_SKINNED` Vertex Shader (GLSL 330 core)

Extends the existing `MESH_BLINN_PHONG` vertex shader:

```glsl
// Additional uniforms
uniform mat4 u_boneMatrices[64];
uniform bool u_hasBones;  // false for static meshes using this shader (shouldn't happen, but safe fallback)

// Additional vertex attributes
layout(location = 3) in ivec4 a_joints;
layout(location = 4) in vec4  a_weights;

// In main():
mat4 skinMatrix = a_weights.x * u_boneMatrices[a_joints.x]
               + a_weights.y * u_boneMatrices[a_joints.y]
               + a_weights.z * u_boneMatrices[a_joints.z]
               + a_weights.w * u_boneMatrices[a_joints.w];
vec4 skinnedPos    = skinMatrix * vec4(a_position, 1.0);
vec3 skinnedNormal = mat3(skinMatrix) * a_normal; // no non-uniform scale assumed
```

The fragment shader is identical to `MESH_BLINN_PHONG` (Blinn-Phong lighting + shadow sampling).

### `SHADOW_DEPTH_SKINNED` Vertex Shader

Same bone matrix multiplication as above, but outputs only `gl_Position` (depth-only pass). Fragment shader is identical to `SHADOW_DEPTH` (empty or writes depth).

### Uniform Upload Strategy

Per skinned entity per frame:
- Upload `u_boneMatrices` as an array of 64 `mat4` via `glUniformMatrix4fv`.
- 64 * 16 floats * 4 bytes = 4096 bytes per entity per pass.
- For shadow pass + main pass = 8192 bytes per skinned entity per frame.

This is well within GL 3.3 uniform limits and bus bandwidth for LEGACY hardware.

---

## 8. Render Integration

### Animation Update System (runs per tick, before render)

New function: `animationUpdateSystem(World& world, f32 dt)`

1. Query all entities with `AnimationState` + `Skeleton` + `Mesh`.
2. For each entity where `AnimationState::playing == true`:
   a. Advance `time += dt * speed`.
   b. If `time >= clip.duration`: wrap if `looping`, else stop and clamp.
   c. For each bone in the clip's channels:
      - Sample T, R, S at current time (binary search + interpolation).
      - Compose local transform: `T * R * S`.
   d. Walk the bone hierarchy (parent-first order) to compute world-space bone matrices.
   e. Multiply each bone's world-space matrix by its inverse bind matrix.
   f. Store results in `Skeleton::boneMatrices[]`.

**Call site:** `Application::fixedUpdate()` or `Application::update()` — after gameplay Lua tick, before render. The implementing agent should place it in the update loop alongside other pre-render systems.

### Mesh Render System Changes

In `meshRenderSystem()`:

1. When iterating entities with `Transform3D` + `Mesh`:
   - Check if entity also has `Skeleton` component.
   - If yes: bind `MESH_SKINNED` shader, upload `u_boneMatrices`, render.
   - If no: bind `MESH_BLINN_PHONG` shader (existing path, unchanged).
2. Shadow pass: same logic — use `SHADOW_DEPTH_SKINNED` for skinned entities, `SHADOW_DEPTH` for static.

No new render passes. Skinned meshes render in the same pass as static meshes, just with a different shader and the bone uniform upload.

---

## 9. Performance Budget

### Per-Frame Costs (LEGACY tier)

| Operation | Cost per entity | Notes |
|-----------|----------------|-------|
| Animation sampling | ~64 bones * 3 channels * O(log n) lerp | CPU, per tick |
| Hierarchy walk | ~64 mat4 multiplies | CPU, per tick |
| Uniform upload (main pass) | 4 KB `glUniformMatrix4fv` | GPU bus |
| Uniform upload (shadow pass) | 4 KB `glUniformMatrix4fv` | GPU bus |
| Vertex skinning | 4 mat4*vec4 per vertex | GPU, vertex shader |

### Target: 30 skinned entities at 60 fps on LEGACY

- CPU animation: 30 entities * 64 bones * ~10 ops = ~19,200 operations per tick. Trivial for any CPU from 2012+.
- Uniform uploads: 30 * 8 KB = 240 KB/frame. Well within PCIe bandwidth.
- Vertex shader cost: 4 extra mat4*vec4 multiplies per vertex. On LEGACY GPUs (~600 GFLOPS), this adds negligible overhead for meshes under 50K vertices.
- **Bottleneck will be draw calls, not skinning.** Each skinned entity is one draw call (no instancing in this ADR). 30 draw calls is fine for LEGACY.

### Optimization Notes (not in this ADR, future work)

- Dual-quaternion skinning (better for twisting joints, same uniform size)
- SSBO bone storage on STANDARD+ tier (removes 64-bone limit)
- Compute shader skinning on MODERN tier
- Animation LOD (reduce bone count at distance)

---

## 10. Lua API

All bindings are registered in `script_engine.cpp` alongside existing `ffe.*` bindings.

### `ffe.playAnimation3D(entity, animIndex, loop)`

Start playing animation `animIndex` (0-based) on the entity. `loop` is a boolean (default `true`). If the entity has no `Skeleton`/`AnimationState`, logs a warning and returns. If `animIndex >= clipCount`, logs an error and returns.

Adds `AnimationState` component if not present. Sets `playing=true`, `clipIndex=animIndex`, `looping=loop`, `time=0`.

### `ffe.stopAnimation3D(entity)`

Stops animation playback. Sets `AnimationState::playing = false`. The entity freezes at its current pose. Does nothing if no `AnimationState` present.

### `ffe.setAnimationSpeed(entity, speed)`

Sets the playback speed multiplier. `1.0` = normal, `0.5` = half speed, `2.0` = double. Negative values are not supported (clamp to 0). Does nothing if no `AnimationState` present.

### `ffe.getAnimationProgress(entity)`

Returns `time / duration` as a float in [0.0, 1.0]. Returns `0.0` if no `AnimationState` or no clip loaded.

### `ffe.isAnimation3DPlaying(entity)`

Returns `true` if `AnimationState::playing == true`. Returns `false` if no `AnimationState` present.

### `ffe.getAnimationCount(entity)`

Returns the number of animation clips in the entity's mesh. Returns `0` if the entity has no `Mesh` component or the mesh has no animations.

---

## 11. Files to Create or Modify

### New Files

| File | Contents |
|------|----------|
| `engine/renderer/skeleton.h` | `BoneInfo`, `SkeletonData`, `AnimationChannel`, `AnimationClipData`, `MeshAnimations` structs, constants |
| `engine/renderer/animation_system.h` | `animationUpdateSystem()` declaration |
| `engine/renderer/animation_system.cpp` | Animation sampling, hierarchy walk, bone matrix computation |
| `tests/renderer/test_skeleton.cpp` | Catch2 tests for bone hierarchy, interpolation, animation state |

### Modified Files

| File | Changes |
|------|---------|
| `engine/renderer/render_system.h` | Add `Skeleton` and `AnimationState` ECS components |
| `engine/renderer/mesh_loader.h` | Add `hasSkeleton` to `MeshGpuRecord`, declare skin/anim accessor functions |
| `engine/renderer/mesh_loader.cpp` | Parse skins (joints, inverse bind matrices) and animations from cgltf |
| `engine/renderer/mesh_renderer.h` | No changes needed (same function signature) |
| `engine/renderer/mesh_renderer.cpp` | Detect `Skeleton` component, bind skinned shader, upload bone uniforms |
| `engine/renderer/shader_library.h` | Add `MESH_SKINNED` and `SHADOW_DEPTH_SKINNED` to enum |
| `engine/renderer/shader_library.cpp` | Add GLSL source for both new shaders |
| `engine/core/application.cpp` | Call `animationUpdateSystem()` in update loop |
| `engine/scripting/script_engine.cpp` | Register 6 new Lua bindings |
| `tests/CMakeLists.txt` | Add `test_skeleton.cpp` |

---

## 12. Test Plan

1. **Bone hierarchy computation** — unit test: given known inverse bind matrices and local transforms, verify final bone matrices match expected values.
2. **Interpolation** — unit test: verify linear lerp for translation/scale, slerp for rotation at known time values.
3. **AnimationState transitions** — unit test: play/stop/loop/speed changes produce correct time advancement.
4. **Bone count validation** — unit test: mesh with >64 bones is rejected at load time.
5. **Joint index validation** — unit test: vertex referencing out-of-range bone index is rejected.
6. **Weight normalization** — unit test: non-normalized weights are renormalized at load time.
7. **Shader compilation** — integration test: `MESH_SKINNED` and `SHADOW_DEPTH_SKINNED` compile without errors (headless may skip this; build-engineer verifies).
8. **Lua bindings** — scripting tests: each of the 6 Lua functions with valid and invalid inputs.
