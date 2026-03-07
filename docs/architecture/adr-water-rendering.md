# ADR: Water Rendering System

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (OpenGL 3.3, 1 GB VRAM)
**Security Review Required:** NO — no external input (procedural surface, no asset loading)

---

## 1. Context

FFE has terrain, skybox, shadow mapping, fog, and Blinn-Phong/PBR lighting — but no water. Water is a visual staple of outdoor 3D scenes. Without it, any terrain with a lake, river, or ocean requires developers to fake it with a textured quad, which looks poor.

This ADR defines a planar water system that renders a single reflective, animated water surface per scene. The design targets LEGACY tier (OpenGL 3.3) — no tessellation, no compute shaders, no refraction FBO. The reflection pass uses the standard camera-flip technique with a half-resolution FBO, and the surface animation is entirely procedural in the fragment shader.

---

## 2. Decision

### 2.1 One Water Plane Per Scene

Only one water plane is active at a time. This simplifies clip plane management (one `glEnable(GL_CLIP_DISTANCE0)` per reflection pass) and avoids multiple reflection FBOs. The water plane is axis-aligned at a configurable Y height, extending to the edges of the visible world.

Rationale: multiple water planes at different heights would each need a separate reflection pass, doubling or tripling the scene render cost. One plane keeps the reflection budget fixed at one extra scene render.

### 2.2 WaterConfig Struct

Stored as an ECS singleton in the registry context (same pattern as `PostProcessConfig`, `ShadowConfig`).

```cpp
struct WaterConfig {
    bool  enabled        = false;       // Master switch
    f32   waterLevel     = 0.0f;        // World-space Y height of the water plane
    glm::vec4 shallowColor = {0.1f, 0.4f, 0.6f, 0.6f}; // RGBA at shallow edges
    glm::vec4 deepColor    = {0.0f, 0.1f, 0.3f, 0.9f}; // RGBA at full depth
    f32   maxDepth       = 10.0f;       // Depth (below waterLevel) at which deepColor is fully reached
    f32   waveSpeed      = 0.03f;       // Scroll speed for animated distortion
    f32   waveScale      = 0.02f;       // Distortion amplitude in UV space
    f32   fresnelPower   = 2.0f;        // Exponent for Schlick's approximation
    f32   fresnelBias    = 0.1f;        // Minimum reflectivity at normal incidence
    f32   reflectionDistortion = 0.02f; // How much wave animation distorts reflection UVs
};
```

| Field | Purpose | Range |
|-------|---------|-------|
| `waterLevel` | Y position of the plane | any float |
| `shallowColor` / `deepColor` | Depth-based color gradient | RGBA [0,1] |
| `maxDepth` | Distance below surface for full deep color | > 0 |
| `waveSpeed` | Animation rate | 0 = frozen, 0.03 = gentle |
| `waveScale` | Distortion strength | 0 = flat mirror, 0.05 = choppy |
| `fresnelPower` | View-angle reflection ramp | 1-5 typical |
| `fresnelBias` | Minimum reflection at head-on view | 0-1 |
| `reflectionDistortion` | Wave distortion applied to reflection sampling | 0-0.1 |

### 2.3 Water ECS Component

Per-entity component for the water plane entity. Lightweight — appearance is controlled by the `WaterConfig` singleton, not per-entity data.

```cpp
struct Water {
    u32 _tag = 1;  // Tag component — presence triggers water rendering
};
static_assert(sizeof(Water) == 4, "Water must be 4 bytes");
```

The water entity must also have a `Transform3D` component. The `Transform3D.position.y` is ignored in favor of `WaterConfig::waterLevel` (the singleton is the source of truth for the Y height). The XZ position of `Transform3D` defines the center of the water quad. Scale controls the XZ extent (default 1000x1000 world units).

Only the first entity with a `Water` component is rendered. Additional Water entities are ignored (one-plane constraint).

### 2.4 Reflection FBO

**Resolution:** Half of the main framebuffer width and height (e.g., 960x540 for a 1920x1080 window). This is the single most important performance lever — half-res reflection costs ~25% of a full scene render instead of ~100%.

**Technique: Camera Flip**

1. Save the current camera state.
2. Reflect the camera position across the water plane: `cameraPos.y = 2 * waterLevel - cameraPos.y`. Invert the pitch.
3. Compute a reflected view matrix.
4. Set `GL_CLIP_DISTANCE0` in all scene shaders via a `u_clipPlane` uniform (`vec4(0, 1, 0, -waterLevel)`). Fragments below the water plane are clipped so they don't appear in the reflection.
5. Render the scene (terrain, meshes, skybox — NOT the water itself, NOT shadow pass) into the reflection FBO.
6. Restore the original camera state.

**Clip plane delivery:** Add a `uniform vec4 u_clipPlane` to `MESH_BLINN_PHONG`, `MESH_SKINNED`, `MESH_PBR`, `TERRAIN`, and `SKYBOX` shaders. In the vertex shader: `gl_ClipDistance[0] = dot(worldPos, u_clipPlane)`. When no water is active, set `u_clipPlane = vec4(0,0,0,0)` so nothing is clipped (dot product is always 0, GL_CLIP_DISTANCE0 disabled).

**FBO setup:**

```
Reflection FBO:
  - Color attachment: GL_RGBA8, half-res
  - Depth attachment: GL_DEPTH_COMPONENT16, half-res (needed for correct z-culling during reflection pass)
  - No stencil
```

GPU resources created at init, recreated on resize. Same lifecycle pattern as `post_process.h`.

### 2.5 Water Shader (`WATER`)

New enum value in `BuiltinShader`:

```cpp
WATER = 20,  // Planar water with reflection, fresnel, animated distortion
// COUNT becomes 21
```

**Vertex shader inputs:**
- Water quad geometry (4 vertices, 6 indices — a single full-screen-ish quad at `waterLevel`)
- `u_model`, `u_viewProjection` matrices
- `u_waterLevel` (float)

**Vertex shader outputs:**
- `v_worldPos` (vec3) — for depth calculation and fresnel
- `v_clipSpace` (vec4) — for projective texture mapping of reflection
- `v_texCoord` (vec2) — for wave animation

**Fragment shader uniforms:**

| Uniform | Type | Source |
|---------|------|--------|
| `u_reflectionTex` | sampler2D | Reflection FBO color texture |
| `u_depthTex` | sampler2D | Main scene depth texture (from post_process) |
| `u_time` | float | Accumulated time for animation |
| `u_cameraPos` | vec3 | Camera world position |
| `u_shallowColor` | vec4 | From WaterConfig |
| `u_deepColor` | vec4 | From WaterConfig |
| `u_maxDepth` | float | From WaterConfig |
| `u_waveSpeed` | float | From WaterConfig |
| `u_waveScale` | float | From WaterConfig |
| `u_fresnelPower` | float | From WaterConfig |
| `u_fresnelBias` | float | From WaterConfig |
| `u_reflDistortion` | float | From WaterConfig |

**Fragment shader algorithm:**

```glsl
// 1. Procedural wave normal (no texture file needed)
vec2 uv1 = v_texCoord * 8.0 + vec2(u_time * u_waveSpeed, u_time * u_waveSpeed * 0.7);
vec2 uv2 = v_texCoord * 6.0 - vec2(u_time * u_waveSpeed * 0.5, u_time * u_waveSpeed * 1.1);
// Hash-based pseudo-noise producing a normal perturbation
vec3 waveNormal = normalize(vec3(
    sin(uv1.x * 6.28) * cos(uv2.y * 6.28) * u_waveScale,
    1.0,
    cos(uv1.y * 6.28) * sin(uv2.x * 6.28) * u_waveScale
));

// 2. Projective reflection UV
vec2 reflUV = (v_clipSpace.xy / v_clipSpace.w) * 0.5 + 0.5;
reflUV.y = 1.0 - reflUV.y;  // flip vertically for reflection
reflUV += waveNormal.xz * u_reflDistortion;  // distort by wave
vec3 reflColor = texture(u_reflectionTex, reflUV).rgb;

// 3. Depth-based transparency (edge fade)
float sceneDepth = linearizeDepth(texture(u_depthTex, screenUV).r);
float waterDepth = linearizeDepth(gl_FragCoord.z);
float depthDiff = sceneDepth - waterDepth;
float depthFactor = clamp(depthDiff / u_maxDepth, 0.0, 1.0);
vec4 waterColor = mix(u_shallowColor, u_deepColor, depthFactor);

// 4. Fresnel (Schlick approximation)
vec3 viewDir = normalize(u_cameraPos - v_worldPos);
float fresnel = u_fresnelBias + (1.0 - u_fresnelBias) * pow(1.0 - max(dot(viewDir, waveNormal), 0.0), u_fresnelPower);

// 5. Final blend
vec3 finalColor = mix(waterColor.rgb, reflColor, fresnel);
float finalAlpha = mix(waterColor.a, 1.0, fresnel);  // more opaque at glancing angles
FragColor = vec4(finalColor, finalAlpha);
```

The procedural wave uses layered sinusoidal functions rather than a noise texture. This avoids an external asset dependency and runs efficiently on LEGACY hardware. Two overlapping UV scrolls at different speeds create convincing surface movement.

### 2.6 Render Order

Water must be rendered **after** all opaque geometry and **before** any transparent 2D overlays:

```
1. Shadow depth pass (existing)
2. [NEW] Reflection pass — render scene into reflection FBO (only if water enabled)
3. Main scene pass — opaque meshes, terrain, skybox (existing)
4. [NEW] Water pass — render water quad with alpha blending
5. Post-processing (existing — bloom, tone map, FXAA operate on final image including water)
6. 2D overlay / HUD (existing)
```

Water renders with:
- `glEnable(GL_BLEND)`, `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`
- `glDepthMask(GL_FALSE)` (write to color but not depth — water is transparent)
- `glEnable(GL_DEPTH_TEST)` (read depth for correct occlusion)

### 2.7 Water Quad Geometry

A single quad (2 triangles) generated at init time. The quad is an XZ plane at Y=0 with extent [-0.5, 0.5] in both X and Z. The model matrix scales it to the desired world size and translates Y to `waterLevel`.

GPU resources: 1 VAO, 1 VBO (4 vertices x 20 bytes = 80 bytes), 1 IBO (6 indices = 12 bytes). Negligible VRAM.

Vertex layout: `vec3 position + vec2 texCoord` (20 bytes per vertex). This is simpler than `MeshVertex` (32 bytes) — water does not need normals or tangents in the vertex data (the shader computes procedural normals).

### 2.8 Integration with Existing Systems

**Terrain:** Water renders independently. `getTerrainHeight()` is unaffected. The terrain is rendered into the reflection FBO like any other mesh.

**Shadows:** The shadow depth pass does NOT render the water quad (water doesn't cast shadows). The reflection pass does NOT include shadow mapping (too expensive for LEGACY — shadows are omitted from reflections).

**Fog:** The water fragment shader applies the same fog calculation as `MESH_BLINN_PHONG` (linear fog based on fragment distance). Fog uniforms (`u_fogColor`, `u_fogStart`, `u_fogEnd`, `u_fogEnabled`) are passed to the water shader.

**Skybox:** The skybox is rendered into the reflection FBO. This gives water reflections of the sky. The skybox shader needs the `u_clipPlane` uniform added (set to `vec4(0,0,0,0)` — skybox is always above water, no clipping needed, but the uniform must exist to avoid shader errors).

**Post-processing:** Water is rendered into the HDR scene FBO (if post-processing is active). Bloom, tone mapping, and FXAA apply to the water surface naturally.

### 2.9 Lua API

```lua
-- Create water (adds Water + Transform3D to a new entity)
local waterEntity = ffe.createWater(waterLevel)  -- Y height

-- Configure appearance
ffe.setWaterLevel(waterLevel)           -- change Y height at runtime
ffe.setWaterColor(r, g, b, a)          -- shallow color (deep color = shallow * 0.3 darkened)
ffe.setWaterOpacity(opacity)            -- 0.0 = invisible, 1.0 = opaque (sets shallowColor.a)
ffe.setWaterWaveSpeed(speed)            -- animation speed (default 0.03)
ffe.setWaterWaveScale(scale)            -- distortion strength (default 0.02)

-- Remove water
ffe.removeWater()                       -- destroys the water entity, frees reflection FBO
```

Six bindings total. The `setWaterColor` binding sets `shallowColor` directly and auto-derives `deepColor` as a darkened variant (multiply RGB by 0.3, alpha by 1.5 clamped to 1.0). Advanced users who want full control can use future bindings or C++ directly.

### 2.10 Performance Budget

**Target: 60 fps on LEGACY tier (GTX 650-class, OpenGL 3.3, 1 GB VRAM)**

| Cost | Estimate | Mitigation |
|------|----------|------------|
| Reflection FBO render | ~40% of main scene cost | Half-resolution (quarter pixel count) |
| Reflection scene complexity | Same draw calls as main pass | Skip shadow pass in reflection; skip particles, 2D |
| Water quad draw | 2 triangles, 1 draw call | Negligible |
| Water fragment shader | ~20 ALU ops per fragment | No texture fetches except reflection sampler + depth |
| VRAM for reflection FBO | ~4 MB at 960x540 RGBA8+D16 | Fixed cost, scales with window size |

**Total overhead when water is enabled:** approximately 40-50% of the base 3D scene render time. On a scene that renders at 120 fps without water, water brings it to ~70-80 fps — still above the 60 fps floor.

**If over budget on specific hardware:**
1. Reduce reflection FBO to quarter resolution (set via a future `setWaterReflectionQuality` binding)
2. Skip terrain from reflection pass (render only skybox + nearby meshes)
3. Disable reflection entirely (water becomes a flat colored transparent plane)

---

## 3. Files to Create / Modify

### New Files

| File | Contents |
|------|----------|
| `engine/renderer/water.h` | `WaterConfig` struct, `Water` component, `initWater()`, `shutdownWater()`, `resizeWaterFBOs()`, `renderWaterReflection()`, `renderWater()`, `setWaterShader()` |
| `engine/renderer/water.cpp` | Reflection FBO management, water quad VAO, reflection pass orchestration, water draw |

### Modified Files

| File | Change |
|------|--------|
| `engine/renderer/shader_library.h` | Add `WATER = 20` to `BuiltinShader` enum |
| `engine/renderer/shader_library.cpp` | Add WATER vertex + fragment shader source, compile in `initShaderLibrary()` |
| `engine/renderer/render_system.h` | Add `Water` component struct |
| `engine/core/application.cpp` | Call `initWater()` at init, `renderWaterReflection()` before main pass, `renderWater()` after opaque pass, `shutdownWater()` at shutdown, `resizeWaterFBOs()` on resize |
| `engine/scripting/script_engine.cpp` | Add 6 Lua bindings (`createWater`, `setWaterLevel`, `setWaterColor`, `setWaterOpacity`, `setWaterWaveSpeed`, `setWaterWaveScale`, `removeWater`) |
| `engine/renderer/.context.md` | Document water API |
| Existing 3D shaders (MESH_BLINN_PHONG, MESH_SKINNED, MESH_PBR, TERRAIN, SKYBOX) | Add `uniform vec4 u_clipPlane` + `gl_ClipDistance[0]` in vertex shaders |

### Test Files

| File | Coverage |
|------|----------|
| `tests/renderer/test_water.cpp` | WaterConfig defaults, fresnel math, depth fade math, reflection UV flip |
| `tests/scripting/test_water_bindings.cpp` | Lua binding smoke tests |

---

## 4. Rejected Alternatives

**Refraction FBO:** Rendering the scene a third time (from below) for refraction is too expensive on LEGACY. The depth-based color blend (shallow/deep) approximates refraction visually at zero extra render cost.

**Tessellated wave mesh:** Requires OpenGL 4.0+ tessellation shaders. Violates LEGACY tier constraint. Procedural fragment-shader waves achieve convincing results without geometry modification.

**Normal map texture:** Would require shipping or generating a water normal map asset. Procedural sinusoidal waves in the shader avoid external dependencies and are tunable at runtime.

**Multiple water planes:** Each additional plane needs its own reflection pass (~40% scene cost each). The one-plane limit keeps the performance ceiling predictable.

**Full-resolution reflection:** Doubles the reflection cost for marginal visual improvement. Half-resolution reflections are industry standard (used in Source Engine, Morrowind, many mobile engines).

---

## 5. Open Questions

1. **Should the reflection pass include point lights?** Currently planned: yes (reuse the same lighting setup). If too expensive on LEGACY, we can render reflections with ambient-only lighting.
2. **Underwater camera:** When the camera goes below `waterLevel`, the scene should tint blue and distort. This is a separate feature (post-process underwater effect) and is out of scope for this ADR. Flag for a future milestone.
