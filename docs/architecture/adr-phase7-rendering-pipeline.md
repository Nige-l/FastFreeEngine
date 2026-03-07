# ADR: Phase 7 -- Rendering Pipeline Modernisation

**Status:** APPROVED -- ready for implementation
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (primary), STANDARD (SSAO), as specified per milestone
**Security Review Required:** NO -- no new file I/O, networking, or external input parsing is introduced. IBL cubemap loading reuses existing `loadSkybox` path which has already been security-reviewed.

---

## 1. Context and Motivation

### Current State

FFE's 3D renderer is functional but visually basic:

- **Lighting:** Blinn-Phong with a single directional light + 8 point lights. No energy conservation.
- **Materials:** `Material3D` stores diffuse color, diffuse texture, specular color, shininess, normal map, specular map. No metallic/roughness workflow.
- **Post-processing:** None. The scene renders directly to the default framebuffer in LDR (8-bit per channel). No bloom, no tone mapping, no gamma correction pass.
- **Instancing:** Every mesh is a separate draw call. 100 identical trees = 100 draw calls.
- **Skeletal animation:** Bone hierarchy, GPU skinning, and clip playback exist (skeleton.h, animation_system). Missing: animation blending, state machines, root motion.
- **Anti-aliasing:** None. GLFW MSAA hints are not set. No FXAA.
- **Ambient occlusion:** None.

### What Competitors Offer

Unity, Godot 4, and Bevy all ship PBR materials, bloom, tone mapping, SSAO, FXAA, and GPU instancing as baseline features. A scene rendered in FFE today looks flat and dated compared to the same scene in Godot 4 with default settings.

### Goal

Bring FFE's visual output to competitive parity with Godot 4's forward renderer. All core features (PBR, bloom, tone mapping, instancing, AA) must run on OpenGL 3.3 (LEGACY tier). SSAO is the one exception -- it is STANDARD+ only due to the number of texture lookups required. Vulkan is deferred to Phase 8.

### Approach

This phase is incremental. Each milestone is self-contained and shippable. PBR replaces Blinn-Phong. Post-processing wraps the existing render pass in an FBO chain. Instancing is additive. Each milestone lands with tests, Lua bindings, and updated `.context.md`.

---

## 2. Milestone 1: PBR Materials (LEGACY Tier)

### 2.1 Material Model

Adopt the **metallic-roughness workflow** from the glTF 2.0 specification. This is the industry standard and matches the assets most indie developers already have.

The **Cook-Torrance microfacet BRDF** replaces Blinn-Phong:

```
f(l, v) = D(h) * G(l, v, h) * F(v, h) / (4 * dot(n, l) * dot(n, v))
```

- **D (Normal Distribution):** GGX/Trowbridge-Reitz. One `roughness` parameter.
- **G (Geometry):** Smith-Schlick approximation. Uses `roughness` remapped as `k = (roughness + 1)^2 / 8`.
- **F (Fresnel):** Schlick approximation. `F0` derived from `metallic`: `F0 = mix(vec3(0.04), albedo, metallic)`.

All three terms are standard, well-documented, and require no GL extensions beyond 3.3.

### 2.2 PBRMaterial Component

`Material3D` is replaced by `PBRMaterial`. The old `Material3D` is retained as deprecated for one phase (removed in Phase 8). The mesh renderer checks for `PBRMaterial` first, falls back to `Material3D` for backwards compatibility.

```cpp
struct PBRMaterial {
    // Base color
    glm::vec4          albedo          = {1.0f, 1.0f, 1.0f, 1.0f};  // 16 bytes
    rhi::TextureHandle albedoMap;       // 0 = use scalar albedo        4 bytes

    // Metallic-roughness
    f32                metallic        = 0.0f;                        //  4 bytes
    f32                roughness       = 0.5f;                        //  4 bytes
    rhi::TextureHandle metallicRoughnessMap; // G=roughness, B=metallic  4 bytes

    // Normal map
    rhi::TextureHandle normalMap;       // 0 = flat normal               4 bytes
    f32                normalScale     = 1.0f;                        //  4 bytes

    // Ambient occlusion
    rhi::TextureHandle aoMap;           // 0 = no AO texture             4 bytes

    // Emissive
    glm::vec3          emissiveFactor  = {0.0f, 0.0f, 0.0f};         // 12 bytes
    rhi::TextureHandle emissiveMap;     // 0 = use scalar emissive       4 bytes

    // Shader override (0 = use builtin PBR shader)
    rhi::ShaderHandle  shaderOverride;                                //  4 bytes

    u32 _pad = 0;                                                     //  4 bytes
};
// Target: 68 bytes. Verify with static_assert at implementation time.
```

Memory note: 68 bytes per entity is acceptable. With 10,000 3D entities (unlikely at LEGACY tier), total material storage is 680 KB. The component is POD, no pointers, no heap.

### 2.3 Image-Based Lighting (IBL)

IBL provides physically plausible ambient lighting without ray tracing. Three precomputed textures:

1. **Irradiance cubemap** (32x32 per face) -- diffuse ambient term. Convolution of the environment map with a cosine lobe.
2. **Prefiltered specular cubemap** (128x128 per face, 5 mip levels) -- specular ambient term. Each mip level represents a different roughness.
3. **BRDF LUT** (512x512, RG16F) -- 2D lookup table for the split-sum approximation. Generated once at engine startup or shipped as a baked asset.

**GL 3.3 compatibility:** All three textures are standard 2D/cubemap textures. No compute shaders needed. The BRDF LUT can be generated in a fragment shader rendering to an FBO, or baked offline and loaded as a PNG. Irradiance and prefiltered cubemaps are generated from the skybox cubemap at load time using fragment-shader convolution (cold path, runs once).

**Integration with existing skybox:** The `loadSkybox` function already loads 6 faces into a cubemap. IBL generation runs as a post-step after skybox load. If no skybox is loaded, IBL falls back to a uniform ambient color (equivalent to current behaviour).

### 2.4 Shader: MESH_PBR

New `BuiltinShader::MESH_PBR` (enum value 8). GLSL 330 core. Vertex shader is identical to `MESH_BLINN_PHONG` (transforms + normals). Fragment shader implements:

1. Sample albedo, metallic-roughness, normal, AO, emissive maps (with scalar fallbacks).
2. Compute Cook-Torrance BRDF for the directional light and each active point light.
3. Add IBL diffuse (`irradiance * albedo * (1 - metallic)`) and IBL specular (`prefilteredColor * (F * scale + bias)`).
4. Add emissive term.
5. Output HDR linear colour (no tone mapping here -- that is M2).

A corresponding `MESH_PBR_SKINNED` (enum value 9) adds bone matrix skinning. `SHADOW_DEPTH` and `SHADOW_DEPTH_SKINNED` remain unchanged (depth-only, no material needed).

### 2.5 Lua Bindings

| Binding | Signature | Notes |
|---------|-----------|-------|
| `ffe.setPBRMaterial` | `(entity, { albedo={r,g,b,a}, metallic=0.0, roughness=0.5, emissive={r,g,b} })` | Table-based. Missing fields use defaults. |
| `ffe.setPBRTexture` | `(entity, slot: string, textureHandle)` | Slot is one of: `"albedo"`, `"metallicRoughness"`, `"normal"`, `"ao"`, `"emissive"`. |

No new `loadTexture` binding needed -- existing `ffe.loadTexture` works for all map types.

### 2.6 Migration Path

- Entities with `Material3D` continue to render via `MESH_BLINN_PHONG`. No breakage.
- Entities with `PBRMaterial` render via `MESH_PBR`.
- An entity must not have both. If both are present, `PBRMaterial` wins, and a warning is logged (once per entity, not per frame).
- `Material3D` is deprecated at the start of Phase 7 and removed in Phase 8.

---

## 3. Milestone 2: Post-Processing Pipeline (LEGACY Tier)

### 3.1 Architecture: FBO Chain

The scene currently renders directly to the default framebuffer. Post-processing requires rendering to an intermediate buffer first.

```
Scene Render (3D + 2D)
    |
    v
[HDR FBO] -- GL_RGBA16F color attachment, GL_DEPTH24_STENCIL8 depth
    |
    v
Pass 1: Bloom (extract bright -> blur -> blend)
    |
    v
Pass 2: Tone Mapping (HDR -> LDR)
    |
    v
Pass 3: Gamma Correction (linear -> sRGB)
    |
    v
[Default Framebuffer] -- final output
```

Each pass is a fullscreen quad (two triangles, no index buffer needed) with a fragment shader reading from the previous pass's output texture.

**GL 3.3 compatibility:** `GL_RGBA16F` textures and FBOs are core in GL 3.0+. Already supported in FFE's RHI (`TextureFormat::RGBA16F` exists in `rhi_types.h`). No extensions needed.

### 3.2 Post-Process Pass Infrastructure

```cpp
struct PostProcessPass {
    rhi::ShaderHandle shader;
    rhi::TextureHandle inputTexture;   // from previous pass
    u32 outputFBO;                     // 0 = default framebuffer (final pass)
    rhi::TextureHandle outputTexture;  // for next pass to read
};
```

A fixed-size array of passes (max 8) is evaluated in order. This avoids heap allocation and keeps the pipeline predictable. New passes are added by editing the array -- no virtual functions, no polymorphism.

The fullscreen quad VAO is created once at pipeline init (cold path) and reused for every pass.

### 3.3 Bloom

Two-pass Gaussian blur on a half-resolution FBO (saves 75% fill rate on LEGACY hardware):

1. **Threshold extract:** Fragment shader reads HDR scene colour; pixels with `luminance > threshold` are written to a half-res FBO. Others are black. Configurable threshold (default 1.0).
2. **Gaussian blur:** Two-pass separable blur (horizontal then vertical). Kernel size 13 taps (6+1+6). Two half-res FBOs ping-pong.
3. **Composite:** Additive blend of blurred bloom texture onto the HDR scene buffer. Configurable intensity (default 0.5).

Half-resolution bloom at 720p means blur FBOs are 640x360. Even a 2012 GPU handles 13-tap Gaussian at that resolution trivially.

### 3.4 Tone Mapping

Two algorithms, selectable at runtime:

- **Reinhard:** `color = color / (color + 1.0)`. Simple, predictable, slightly washed-out highlights.
- **ACES Filmic:** Better contrast and saturation. The standard in film and modern engines. Uses the fitted approximation by Stephen Hill (no matrix math, ~5 instructions).

Default: ACES Filmic. Selectable via Lua.

### 3.5 Gamma Correction

Final pass applies `pow(color, 1.0/2.2)` to convert from linear space to sRGB. This is a single instruction per fragment.

Note: All textures must be loaded in linear space for PBR to be correct. sRGB textures (albedo maps) need `GL_SRGB8_ALPHA8` as the internal format so the GPU linearises them on sample. This is a GL 3.3 core feature. The `TextureDesc` struct gets a `bool srgb = false` field; when true, the RHI uses `GL_SRGB8_ALPHA8` instead of `GL_RGBA8`.

### 3.6 Post-Processing Singleton

```cpp
struct PostProcessConfig {
    bool  bloomEnabled     = false;
    f32   bloomThreshold   = 1.0f;
    f32   bloomIntensity   = 0.5f;
    u8    toneMapper       = 1;         // 0=Reinhard, 1=ACES
    bool  gammaCorrection  = true;
    // Future passes (FXAA, SSAO composite) added here
};
```

Stored in the ECS context, same pattern as `SceneLighting3D`. No heap. No pointers.

### 3.7 Lua Bindings

| Binding | Signature |
|---------|-----------|
| `ffe.enableBloom` | `(threshold: number, intensity: number)` |
| `ffe.disableBloom` | `()` |
| `ffe.setToneMapping` | `(mode: string)` -- `"reinhard"` or `"aces"` or `"none"` |
| `ffe.setGammaCorrection` | `(enabled: boolean)` |

### 3.8 Performance Budget

At 720p on GL 3.3 hardware (~2012 era GPU, e.g. GeForce GT 640):
- HDR FBO resolve: ~0.5 ms
- Bloom (half-res threshold + 2-pass blur + composite): ~1.5 ms
- Tone mapping: ~0.3 ms
- Gamma correction: ~0.2 ms
- **Total post-processing: ~2.5 ms** (well within the 8 ms GPU budget)

---

## 4. Milestone 3: GPU Instancing (LEGACY Tier)

### 4.1 Problem

Rendering N identical meshes currently requires N draw calls, each with its own uniform upload (`model` matrix, material). At 500+ objects this becomes the bottleneck -- not the GPU, but the CPU-side draw call overhead.

### 4.2 Solution: Instanced Arrays

`glDrawArraysInstanced` / `glDrawElementsInstanced` are core in GL 3.3. Per-instance data (model matrix) is uploaded via a vertex attribute buffer with `glVertexAttribDivisor(attr, 1)`.

A `mat4` model matrix requires 4 vertex attribute slots (each `vec4`). GL 3.3 guarantees at least 16 vertex attributes. Current mesh vertex uses 3 slots (position, normal, texcoord) or 5 for skinned (+ joints, weights). Instance attributes use slots 8-11, leaving headroom.

### 4.3 Instance Buffer

```cpp
inline constexpr u32 MAX_INSTANCES_PER_BATCH = 1024;

struct InstanceData {
    glm::mat4 modelMatrix;       // 64 bytes
};
// Total buffer: 1024 * 64 = 64 KB per instance buffer. Fits in VRAM trivially.
```

One `rhi::BufferHandle` with `BufferUsage::STREAM` is allocated per unique `MeshHandle` that has more than one entity. Updated each frame via `rhi::updateBuffer`. The mesh renderer groups entities by `MeshHandle`, fills the instance buffer, and issues one `glDrawElementsInstanced` call per group.

**Threshold:** Instancing kicks in when a `MeshHandle` has 2+ entities. Below that, the regular single-draw path is used (avoids the overhead of instance buffer setup for unique meshes).

### 4.4 Material Instancing

For M3, all instances in a batch share the same material (first entity's `PBRMaterial`). Per-instance material variation (e.g., colour tinting) is deferred -- it would require packing material data into the instance buffer or using SSBOs (GL 4.3+), which breaks LEGACY compatibility.

A warning is logged (once) if entities sharing a `MeshHandle` have different materials, informing the developer that only the first entity's material is used for the batch.

### 4.5 Lua Bindings

No new bindings. Instancing is automatic and transparent. Developers create entities with the same mesh handle; the renderer batches them.

Optional introspection binding for debugging:

| Binding | Signature |
|---------|-----------|
| `ffe.getInstanceCount` | `(meshHandle) -> integer` -- returns how many entities share this mesh |

### 4.6 Use Cases

- Foliage (hundreds of tree/bush meshes)
- Crowds (character mesh with per-instance transforms)
- Debris / environmental clutter
- Particle meshes (3D particle systems using mesh instances instead of quads)

---

## 5. Milestone 4: Skeletal Animation Completion (LEGACY Tier)

### 5.1 Current State

`skeleton.h` defines: `BoneInfo`, `SkeletonData` (64 max bones), `AnimationChannel` (256 max keyframes, TRS), `AnimationClipData`, `MeshAnimations` (16 clips). `animation_system.cpp` implements single-clip playback with linear interpolation. GPU skinning works via `MESH_SKINNED` shader with `u_boneMatrices[64]` uniform array.

### 5.2 What is Missing

1. **Animation blending (crossfade).** Transitioning between clips (e.g., walk to run) pops to the first frame of the new clip.
2. **Animation state machine.** No declarative way to define transitions. Game code must manually call `playAnimation3D`.
3. **Root motion.** The root bone's translation is baked into the animation. No way to extract it and apply to the entity's `Transform3D`.
4. **Interpolation modes.** Current implementation is linear-only. glTF specifies step, linear, and cubic spline.

### 5.3 Animation Blending

Add a blend layer to `AnimationState`:

```cpp
struct AnimationState {
    u32  clipIndex       = 0;
    f32  time            = 0.0f;
    f32  speed           = 1.0f;
    bool looping         = true;
    bool playing         = false;

    // Blend state (crossfade)
    u32  blendFromClip   = 0;       // previous clip index
    f32  blendFromTime   = 0.0f;    // frozen time in previous clip
    f32  blendAlpha      = 1.0f;    // 1.0 = fully in current clip, 0.0 = fully in previous
    f32  blendDuration   = 0.0f;    // total crossfade duration in seconds
    f32  blendElapsed    = 0.0f;    // time since crossfade started
    bool blending        = false;

    u8   _pad[1]         = {};
};
// ~40 bytes. Acceptable per-entity cost.
```

When `blending == true`, `animation_system` samples both `blendFromClip` at `blendFromTime` (frozen or advancing, depending on config) and `clipIndex` at `time`, then lerps bone transforms by `blendAlpha`. When `blendElapsed >= blendDuration`, blending completes and the old clip is discarded.

Bone transform lerp: `position = mix(posA, posB, alpha)`, `rotation = slerp(rotA, rotB, alpha)`, `scale = mix(scaleA, scaleB, alpha)`. This is per-bone, per-frame -- 64 bones * 3 lerps = ~192 lerps. Negligible CPU cost.

### 5.4 Interpolation Modes

Add an `InterpolationMode` enum:

```cpp
enum class InterpolationMode : u8 { STEP, LINEAR, CUBIC_SPLINE };
```

Store per-channel in `AnimationChannel`. Current `AnimationChannel` already has TRS arrays. Add a mode field per track:

```cpp
struct AnimationChannel {
    // ... existing fields ...
    InterpolationMode translationInterp = InterpolationMode::LINEAR;
    InterpolationMode rotationInterp    = InterpolationMode::LINEAR;
    InterpolationMode scaleInterp       = InterpolationMode::LINEAR;
};
```

3 bytes added. `AnimationChannel` is asset-level (cold storage), not per-frame. No performance concern.

Cubic spline interpolation uses the glTF formula: Hermite interpolation with in-tangent and out-tangent. Tangent values are stored interleaved with keyframe values in glTF. Extend the keyframe arrays to store tangents when the mode is `CUBIC_SPLINE`. Since `MAX_KEYFRAMES_PER_CHANNEL` is 256, tangent storage doubles the effective keyframe data but only for cubic channels.

### 5.5 Root Motion

Optional per-clip flag: `bool rootMotion = false` in `AnimationClipData`. When enabled:

1. The animation system extracts the root bone's translation delta each frame.
2. The delta is zeroed out of the bone transform (root bone stays at origin in bone space).
3. The delta is written to a `RootMotionDelta` singleton or a per-entity component.
4. Game code (or a built-in system) reads the delta and applies it to `Transform3D.position`.

This keeps the animation system pure (no side effects on `Transform3D`) while enabling root motion for locomotion.

### 5.6 Lua Bindings

| Binding | Signature | Notes |
|---------|-----------|-------|
| `ffe.crossfadeAnimation3D` | `(entity, clipIndex, duration)` | Starts a crossfade from current clip to `clipIndex` over `duration` seconds |
| `ffe.setRootMotion3D` | `(entity, enabled: boolean)` | Enable/disable root motion extraction |
| `ffe.getRootMotionDelta3D` | `(entity) -> dx, dy, dz` | Returns the root bone translation delta for the last frame |

---

## 6. Milestone 5: Anti-Aliasing (LEGACY Tier)

### 6.1 MSAA

Request multisampled framebuffer via GLFW hint: `glfwWindowHint(GLFW_SAMPLES, N)`. GL handles the rest transparently.

When post-processing is active (M2), the HDR FBO must also be multisampled. This requires a `glBlitFramebuffer` resolve step from the MSAA FBO to a non-MSAA FBO before post-processing reads it (post-process shaders cannot read MSAA textures directly on GL 3.3).

Tier-dependent sample counts:
- LEGACY: 2x (minimal VRAM overhead)
- STANDARD: 4x
- MODERN: 8x

### 6.2 FXAA

FXAA 3.11 (Timothy Lottes, public domain) as a post-processing pass. Runs after tone mapping, before gamma correction. Single fullscreen quad pass reading the LDR image. Lightweight: ~0.3 ms at 720p.

FXAA is preferable to MSAA when the developer wants anti-aliased post-processed edges (MSAA only affects geometry edges, not shader aliasing). The two are not mutually exclusive but most developers will choose one.

### 6.3 Lua Bindings

| Binding | Signature |
|---------|-----------|
| `ffe.setAntiAliasing` | `(mode: string)` -- `"none"`, `"msaa"`, `"fxaa"` |
| `ffe.setMSAASamples` | `(count: integer)` -- 2, 4, or 8. Clamped to tier maximum. |

### 6.4 Design Note: MSAA + Post-Processing Interaction

When both MSAA and post-processing are active, the render path is:

```
Scene -> [MSAA HDR FBO] -> resolve -> [Non-MSAA HDR FBO] -> post-processing -> default FB
```

The resolve step is one `glBlitFramebuffer` call per frame (~0.1 ms). The non-MSAA HDR FBO is already needed for post-processing, so this adds only the MSAA FBO and the blit.

---

## 7. Milestone 6: Screen-Space Ambient Occlusion (STANDARD+ Tier)

### 7.1 Tier Gating

SSAO is **not available on LEGACY**. It requires many texture samples per fragment (typically 32-64 random hemisphere samples) and a depth buffer readback. On GL 3.3 hardware this is too expensive for 60 fps at 720p.

SSAO is enabled on STANDARD (GL 4.5) and MODERN (Vulkan) tiers only. The engine's tier detection (future: runtime query of GL version) gates this at startup.

### 7.2 Algorithm

John Chapman's SSAO (hemisphere sampling in view space):

1. Render scene depth and normals to a G-buffer FBO (or reconstruct from depth buffer + normal map).
2. For each fragment, sample 32 points in a hemisphere oriented along the surface normal.
3. Compare sample depth against the depth buffer. Occluded samples darken the pixel.
4. Blur the raw AO texture (4x4 bilateral blur) to reduce noise.
5. Multiply the AO value into the ambient lighting term in the PBR shader.

**Why not depth-only reconstruction:** Reconstructing normals from depth introduces artefacts on thin geometry. A geometry-pass normal buffer is more robust and costs one extra texture write per fragment.

### 7.3 Parameters

```cpp
struct SSAOConfig {
    bool  enabled    = false;
    f32   radius     = 0.5f;    // sample hemisphere radius in view space
    f32   bias       = 0.025f;  // depth bias to prevent self-occlusion
    u32   samples    = 32;      // sample count (16, 32, 64)
    f32   intensity  = 1.0f;    // AO darkening multiplier
};
```

### 7.4 Lua Bindings

| Binding | Signature |
|---------|-----------|
| `ffe.enableSSAO` | `(radius: number, bias: number, samples: integer)` |
| `ffe.disableSSAO` | `()` |
| `ffe.setSSAOIntensity` | `(intensity: number)` |

Calling `enableSSAO` on a LEGACY-tier GPU logs a warning and does nothing.

---

## 8. Milestone 7: Sprite Batching 2.0

### 8.1 Problem

The current `SpriteBatch` groups by texture and renders up to 2048 sprites per batch. This is adequate for simple 2D games but inefficient for particle-heavy scenes or UIs with many texture atlases, because each texture switch flushes the batch.

### 8.2 Improvement: Texture Array Batching

Use `GL_TEXTURE_2D_ARRAY` (core in GL 3.0) to bind multiple textures in a single draw call. Each sprite's vertex includes a `textureIndex` that selects the layer in the array.

**Constraint:** All textures in an array must share the same dimensions and format. Group sprites by atlas size (common case: all sprites use the same atlas) and only fall back to texture-switch flushing for mismatched sizes.

**Alternative considered:** Bindless textures (`GL_ARB_bindless_texture`). Rejected: not core until GL 4.4, breaks LEGACY compatibility.

### 8.3 Scope

- Extend `SpriteVertex` with a `f32 textureLayer` field (36 bytes, up from 32).
- Allocate a `GL_TEXTURE_2D_ARRAY` per unique (width, height, format) group. Max 256 layers (GL 3.3 minimum).
- Modify `SpriteBatch::flush()` to bind the texture array instead of individual textures.
- The SPRITE shader is updated to use `sampler2DArray` and `gl_Layer` / the interpolated `textureLayer`.

### 8.4 Performance Target

Reduce draw calls for a 10-atlas 2D scene from ~10 flushes to ~1 flush. Measured improvement required before merge.

---

## 9. Milestone 8: Phase Close

Verification session:
- FULL build (Clang-18 + GCC-13)
- All tests pass
- Performance profiling: PBR + post-processing at 720p on GL 3.3 must hold 60 fps
- Instancing benchmark: 1000 instances at 60 fps
- `.context.md` fully updated
- Devlog and project-state updated

---

## 10. Tier Gating Summary

| Feature | RETRO | LEGACY | STANDARD | MODERN |
|---------|-------|--------|----------|--------|
| PBR Materials | No | Yes | Yes | Yes |
| Bloom | No | Yes (half-res) | Yes (full-res) | Yes (full-res) |
| Tone Mapping | No | Yes | Yes | Yes |
| Gamma Correction | No | Yes | Yes | Yes |
| GPU Instancing | No | Yes | Yes | Yes |
| FXAA | No | Yes | Yes | Yes |
| MSAA | No | 2x | 4x | 8x |
| SSAO | No | No | Yes (32 samples) | Yes (64 samples) |
| Animation Blending | No | Yes | Yes | Yes |
| Texture Array Batching | No | Yes | Yes | Yes |

RETRO (GL 2.1) gets none of these features. It retains the existing Blinn-Phong pipeline. This is acceptable -- RETRO is not the default tier and targets hardware that is 20+ years old.

---

## 11. New Shader Registry (Post Phase 7)

| Enum | Value | Name | Tier |
|------|-------|------|------|
| `SOLID` | 0 | Solid colour | LEGACY |
| `TEXTURED` | 1 | Basic textured | LEGACY |
| `SPRITE` | 2 | 2D sprite batch | LEGACY |
| `MESH_BLINN_PHONG` | 3 | 3D Blinn-Phong (deprecated) | LEGACY |
| `SHADOW_DEPTH` | 4 | Shadow depth pass | LEGACY |
| `SKYBOX` | 5 | Cubemap skybox | LEGACY |
| `MESH_SKINNED` | 6 | Blinn-Phong skinned (deprecated) | LEGACY |
| `SHADOW_DEPTH_SKINNED` | 7 | Shadow depth skinned | LEGACY |
| `MESH_PBR` | 8 | PBR metallic-roughness | LEGACY |
| `MESH_PBR_SKINNED` | 9 | PBR + bone skinning | LEGACY |
| `BLOOM_THRESHOLD` | 10 | Bloom bright-pixel extract | LEGACY |
| `BLUR_HORIZONTAL` | 11 | Gaussian blur (H) | LEGACY |
| `BLUR_VERTICAL` | 12 | Gaussian blur (V) | LEGACY |
| `TONE_MAP` | 13 | HDR tone mapping | LEGACY |
| `GAMMA_CORRECT` | 14 | Gamma correction | LEGACY |
| `FXAA` | 15 | FXAA 3.11 | LEGACY |
| `SSAO` | 16 | Screen-space AO | STANDARD |
| `SSAO_BLUR` | 17 | Bilateral AO blur | STANDARD |
| `SPRITE_ARRAY` | 18 | 2D sprite batch (texture array) | LEGACY |

All shaders remain GLSL 330 core except SSAO/SSAO_BLUR which use GLSL 450 core.

---

## 12. Dependencies

No new vcpkg dependencies. All features use existing OpenGL capabilities.

The BRDF LUT can either be:
- Generated at startup via a fragment shader pass (~5 ms, cold path), or
- Shipped as a baked 512x512 PNG in `assets/engine/brdf_lut.png` (~500 KB)

Decision: Generate at startup. Avoids shipping engine-internal assets and ensures the LUT matches the exact BRDF formulation.

---

## 13. Session Estimates

| Milestone | Sessions | Key deliverables |
|-----------|----------|------------------|
| M1: PBR Materials | 2 | PBRMaterial component, Cook-Torrance shader, IBL pipeline, Lua bindings, tests |
| M2: Post-Processing | 2 | FBO chain, bloom, tone mapping, gamma correction, Lua bindings, tests |
| M3: GPU Instancing | 1-2 | Instance buffer, automatic batching, 1000-instance benchmark, tests |
| M4: Skeletal Animation | 2 | Crossfade blending, interpolation modes, root motion, Lua bindings, tests |
| M5: Anti-Aliasing | 1 | MSAA + FXAA, MSAA-post-processing interaction, Lua bindings, tests |
| M6: SSAO | 1 | STANDARD-tier SSAO, bilateral blur, tier gating, Lua bindings, tests |
| M7: Sprite Batching 2.0 | 1 | Texture array batching, SPRITE_ARRAY shader, benchmark, tests |
| M8: Phase Close | 1 | FULL build, profiling, documentation sweep |
| **Total** | **11-12 sessions** | |

---

## 14. What This Prevents Us From Doing Later

Nothing. Every decision here is forward-compatible with Phase 8 (Vulkan):

- PBR materials map directly to Vulkan descriptor sets.
- The post-processing FBO chain maps to Vulkan render passes.
- Instance buffers map to Vulkan vertex input bindings.
- SSAO's hemisphere sampling works identically in Vulkan fragment shaders.
- The shader registry's integer IDs abstract over the backend; adding Vulkan shader variants is additive.

The only debt carried forward is the deprecated `Material3D` / `MESH_BLINN_PHONG`, removed in Phase 8.

---

## 15. Architect's Verdict

**APPROVED.** This phase is the largest visual upgrade FFE has received. Every milestone is self-contained, testable, and shippable independently. The PBR + post-processing combination will bring FFE's visual output from "tech demo" to "competitive with Godot 4 default settings."

The critical design constraint -- everything on GL 3.3 except SSAO -- is achievable because PBR and post-processing are fundamentally fragment-shader work, and GL 3.3 fragment shaders are fully capable. The only feature that truly needs more GPU horsepower is SSAO's 32+ texture samples per fragment, which is correctly tier-gated to STANDARD.

Memory layouts are cache-friendly (POD components, no pointers), no heap allocation in hot paths, no virtual dispatch. The instance buffer is the only per-frame GPU upload, and it is a single contiguous `updateBuffer` call per mesh group.

Implementation should proceed M1 through M8 in order. M1 (PBR) must land before M2 (post-processing) because post-processing needs HDR output, and PBR is what produces HDR values worth tone-mapping. M3-M7 are independent of each other and could theoretically be parallelised across sessions, but the session estimates assume sequential delivery for simplicity.
