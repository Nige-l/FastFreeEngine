# Design Note: Perspective Camera Convenience Modes and Diffuse Texture on 3D Meshes

**Status:** APPROVED — ready for implementation
**Author:** architect
**Date:** 2026-03-07
**Session:** 42
**Tiers:** LEGACY (all work targets LEGACY unless stated)
**Security Review Required:** NO — Feature A adds pure math in the Lua binding layer with no new file I/O or external input. Feature B activates an already-present code path (Material3D.diffuseTexture already exists; mesh_renderer.cpp already calls rhi::bindTexture with it). Neither feature introduces new attack surface beyond what ADR-007 already covered.

---

## Codebase State as Found

Before reading these source files the design task was framed against a hypothetical state. The actual codebase is more advanced than the brief assumed. The key discrepancies are documented below so engine-dev works from the true state, not the brief.

### Shader — already texture-complete

`shader_library.cpp` MESH_BLINN_PHONG vertex shader already has:

```glsl
layout(location = 2) in vec2 a_texcoord;
out vec2 v_texcoord;
// ...
v_texcoord = a_texcoord;
```

Fragment shader already has:

```glsl
in vec2 v_texcoord;
uniform sampler2D u_diffuseTexture;
// ...
vec4 texSample = texture(u_diffuseTexture, v_texcoord);
fragColor = vec4(lighting, 1.0) * texSample * u_diffuseColor;
```

There is NO `u_hasTexture` int flag and none is needed. The shader always samples `u_diffuseTexture`. When no user texture is set the renderer falls back to `defaultWhite` (a 1×1 white pixel stored in ECS context as `rhi::TextureHandle`). Multiplying by white is a no-op — existing untextured behaviour is preserved.

**Implication for Feature B:** No shader changes are required. The shader is already correct. engine-dev must NOT add `u_hasTexture` — it is unnecessary complexity and contradicts the existing design.

### Material3D — already has diffuseTexture field

`render_system.h` Material3D struct:

```cpp
struct Material3D {
    glm::vec4          diffuseColor   = {1.0f, 1.0f, 1.0f, 1.0f}; // 16 bytes
    rhi::TextureHandle diffuseTexture;  // 4 bytes (0 = use default white texture)
    rhi::ShaderHandle  shaderOverride; // 4 bytes (0 = use builtin MESH_BLINN_PHONG)
};
static_assert(sizeof(Material3D) == 24);
```

The field is named `diffuseTexture`, NOT `textureHandle` as the brief stated. The `static_assert` is already in place at 24 bytes.

### mesh_renderer.cpp — already handles diffuseTexture

The per-entity draw loop in `mesh_renderer.cpp` already does:

```cpp
rhi::TextureHandle diffuseTex = defaultWhite;
const Material3D* mat = world.registry().try_get<Material3D>(entity);
if (mat != nullptr) {
    diffuseColor = mat->diffuseColor;
    if (rhi::isValid(mat->diffuseTexture)) {
        diffuseTex = mat->diffuseTexture;
    }
}
rhi::setUniformVec4(meshShader, "u_diffuseColor", diffuseColor);
rhi::bindTexture(diffuseTex, 0);
rhi::setUniformInt(meshShader, "u_diffuseTexture", 0);
```

`rhi::bindTexture(handle, unitIndex)` is the established resolution path — it looks up the OpenGL texture ID internally. engine-dev does NOT need to resolve TextureHandle to a raw GL ID manually.

**Implication for Feature B:** mesh_renderer.cpp requires NO changes. The only work needed is: (1) a new Lua binding `ffe.setMeshTexture` that writes to `mat->diffuseTexture`, and (2) confirming that TEXCOORD_0 is loaded from the glTF and uploaded to attribute location 2 in mesh_loader.cpp.

### set3DCamera binding pattern

The existing `ffe.set3DCamera` binding uses `auto ffe_set3DCamera = [](lua_State* state) -> int { ... };` followed by `lua_pushcfunction(L, ffe_set3DCamera)`. This works because the lambda has no capture — a captureless lambda is convertible to a plain function pointer, which is what `lua_pushcfunction` requires.

**Rule for new bindings:** New camera bindings MUST follow the same captureless-lambda pattern. Do NOT add captures. The lambda must be declared as `auto ffe_set3DCameraFPS = [](lua_State* state) -> int { ... };` with no `[&]` or `[=]`. If captures are needed for any reason, use named static functions instead (as the fix from Session 38 demonstrated).

---

## Feature A: Perspective Camera Convenience Modes

### Scope

Two new Lua bindings in `script_engine.cpp`, implemented as captureless lambdas or named static functions. No new C++ headers, no new structs, no changes to `Application`, no changes to the `Camera` struct.

Both bindings are thin math wrappers that call the existing `ffe.set3DCamera` implementation inline (i.e., they write directly to `(*cam)->position` and `(*cam)->target` rather than calling the Lua function — calling back into Lua from C is unnecessary complexity).

### A.1 — ffe.set3DCameraFPS

**Signature:**

```lua
ffe.set3DCameraFPS(x, y, z, yaw_deg, pitch_deg)
```

**Semantics:**
- Position: (x, y, z) in world space
- Yaw: rotation around world Y axis. Convention: 0° looks down -Z (same as set3DCamera default target of (0,0,0) from position (0,0,5))
- Pitch: rotation around local X axis. Positive pitch = looking upward
- Pitch is clamped to [-89, 89] degrees before trig — prevents gimbal lock at ±90°

**Forward vector computation (exact, in this order):**

```cpp
const float yaw_rad   = yaw_deg   * (M_PI_F / 180.0f);
const float pitch_rad = pitch_deg * (M_PI_F / 180.0f);

// Clamp pitch before computing trig (use clamped value, not raw)
const float pitch_clamped = std::max(-89.0f, std::min(89.0f, pitch_deg));
const float pc_rad = pitch_clamped * (M_PI_F / 180.0f);

const float fx = std::cos(pc_rad) * std::sin(yaw_rad);
const float fy = std::sin(pc_rad);
const float fz = std::cos(pc_rad) * std::cos(yaw_rad);
// forward = (fx, fy, fz) — already unit length (cos²+sin² = 1 by construction)

// target = position + forward (1 unit ahead)
const float tx = x + fx;
const float ty = y + fy;
const float tz = z + fz;
```

Note: `M_PI_F` is `static_cast<float>(M_PI)`. Use `#include <cmath>` and `std::cos` / `std::sin` / `std::max` / `std::min` — no raw C math functions.

**C++ binding implementation:**

```cpp
auto ffe_set3DCameraFPS = [](lua_State* state) -> int {
    lua_pushlightuserdata(state, &s_worldRegistryKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
    auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    auto* cam = world->registry().ctx().find<ffe::renderer::Camera*>();
    if (cam == nullptr || *cam == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "ffe.set3DCameraFPS: 3D camera not in ECS context");
        return 0;
    }

    const ffe::f32 x         = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.0));
    const ffe::f32 y         = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
    const ffe::f32 z         = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
    const ffe::f32 yaw_deg   = static_cast<ffe::f32>(luaL_optnumber(state, 4, 0.0));
    const ffe::f32 pitch_deg = static_cast<ffe::f32>(luaL_optnumber(state, 5, 0.0));

    static constexpr ffe::f32 DEG_TO_RAD = static_cast<ffe::f32>(M_PI) / 180.0f;

    const ffe::f32 pitch_clamped = std::max(-89.0f, std::min(89.0f, pitch_deg));
    const ffe::f32 yaw_rad       = yaw_deg   * DEG_TO_RAD;
    const ffe::f32 pitch_rad     = pitch_clamped * DEG_TO_RAD;

    const ffe::f32 fx = std::cos(pitch_rad) * std::sin(yaw_rad);
    const ffe::f32 fy = std::sin(pitch_rad);
    const ffe::f32 fz = std::cos(pitch_rad) * std::cos(yaw_rad);

    (*cam)->position = {x, y, z};
    (*cam)->target   = {x + fx, y + fy, z + fz};
    // up vector remains {0,1,0} — not modified

    return 0;
};
lua_pushcfunction(L, ffe_set3DCameraFPS);
lua_setfield(L, -2, "set3DCameraFPS");
```

**Registration:** Add immediately after the `ffe.set3DCamera` registration block (around line 4015 in script_engine.cpp).

### A.2 — ffe.set3DCameraOrbit

**Signature:**

```lua
ffe.set3DCameraOrbit(target_x, target_y, target_z, radius, yaw_deg, pitch_deg)
```

**Semantics:**
- Camera orbits around (target_x, target_y, target_z)
- radius: distance from target to camera. Must be > 0; if <= 0, no-op with log warning
- Yaw: horizontal rotation around world Y axis
- Pitch: elevation angle. Clamped to [-85, 85] degrees (tighter than FPS — prevents camera going directly overhead, which would cause up-vector ambiguity)
- NaN/Inf guard: if any input is non-finite (`!std::isfinite`), no-op with log warning

**Camera position computation (exact):**

```cpp
const float pitch_clamped = std::max(-85.0f, std::min(85.0f, pitch_deg));
const float yaw_rad   = yaw_deg       * DEG_TO_RAD;
const float pitch_rad = pitch_clamped * DEG_TO_RAD;

const float cx = target_x + radius * std::cos(pitch_rad) * std::sin(yaw_rad);
const float cy = target_y + radius * std::sin(pitch_rad);
const float cz = target_z + radius * std::cos(pitch_rad) * std::cos(yaw_rad);

// position = (cx, cy, cz), target = (target_x, target_y, target_z)
```

**C++ binding implementation:**

```cpp
auto ffe_set3DCameraOrbit = [](lua_State* state) -> int {
    lua_pushlightuserdata(state, &s_worldRegistryKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
    auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    auto* cam = world->registry().ctx().find<ffe::renderer::Camera*>();
    if (cam == nullptr || *cam == nullptr) {
        FFE_LOG_ERROR("ScriptEngine", "ffe.set3DCameraOrbit: 3D camera not in ECS context");
        return 0;
    }

    const ffe::f32 target_x  = static_cast<ffe::f32>(luaL_optnumber(state, 1, 0.0));
    const ffe::f32 target_y  = static_cast<ffe::f32>(luaL_optnumber(state, 2, 0.0));
    const ffe::f32 target_z  = static_cast<ffe::f32>(luaL_optnumber(state, 3, 0.0));
    const ffe::f32 radius    = static_cast<ffe::f32>(luaL_optnumber(state, 4, 5.0));
    const ffe::f32 yaw_deg   = static_cast<ffe::f32>(luaL_optnumber(state, 5, 0.0));
    const ffe::f32 pitch_deg = static_cast<ffe::f32>(luaL_optnumber(state, 6, 0.0));

    // Guard: NaN/Inf inputs
    if (!std::isfinite(target_x) || !std::isfinite(target_y) || !std::isfinite(target_z) ||
        !std::isfinite(radius)   || !std::isfinite(yaw_deg)   || !std::isfinite(pitch_deg)) {
        FFE_LOG_WARN("ScriptEngine",
                     "ffe.set3DCameraOrbit: non-finite input detected — call ignored");
        return 0;
    }

    // Guard: radius must be positive
    if (radius <= 0.0f) {
        FFE_LOG_WARN("ScriptEngine",
                     "ffe.set3DCameraOrbit: radius must be > 0 — call ignored");
        return 0;
    }

    static constexpr ffe::f32 DEG_TO_RAD = static_cast<ffe::f32>(M_PI) / 180.0f;

    const ffe::f32 pitch_clamped = std::max(-85.0f, std::min(85.0f, pitch_deg));
    const ffe::f32 yaw_rad       = yaw_deg       * DEG_TO_RAD;
    const ffe::f32 pitch_rad     = pitch_clamped * DEG_TO_RAD;

    const ffe::f32 cx = target_x + radius * std::cos(pitch_rad) * std::sin(yaw_rad);
    const ffe::f32 cy = target_y + radius * std::sin(pitch_rad);
    const ffe::f32 cz = target_z + radius * std::cos(pitch_rad) * std::cos(yaw_rad);

    (*cam)->position = {cx, cy, cz};
    (*cam)->target   = {target_x, target_y, target_z};
    // up vector remains {0,1,0} — not modified

    return 0;
};
lua_pushcfunction(L, ffe_set3DCameraOrbit);
lua_setfield(L, -2, "set3DCameraOrbit");
```

**Registration:** Add immediately after the `ffe.set3DCameraFPS` registration block.

### A.3 — Files modified for Feature A

| File | Change |
|------|--------|
| `engine/scripting/script_engine.cpp` | Add two binding blocks after line 4015 |

No other files change for Feature A.

---

## Feature B: Diffuse Texture on 3D Meshes

### Scope

One new Lua binding (`ffe.setMeshTexture`) in `script_engine.cpp`. No shader changes. No mesh_renderer.cpp changes. The texture infrastructure is complete as shipped.

The one open question that engine-dev must verify before implementing: **is TEXCOORD_0 loaded from the glTF and uploaded to attribute location 2 in mesh_loader.cpp?** The shader already reads `layout(location = 2) in vec2 a_texcoord`, but if the VBO does not include UV data at location 2, meshes will render with garbage UVs. The investigation and fix (if needed) is part of Feature B's implementation work.

### B.1 — Verifying UV data in mesh_loader.cpp

engine-dev must read `mesh_loader.cpp` and confirm that:

1. `TEXCOORD_0` is extracted from the cgltf primitive's attributes
2. UV data is interleaved into the VBO or bound as a separate attrib pointer at location 2
3. `glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, offset)` and `glEnableVertexAttribArray(2)` are called inside the VAO setup block

If UV data is absent from the loader, engine-dev must add it before the `ffe.setMeshTexture` binding will be useful. The addition must:
- Extract `cgltf_attribute_type_texcoord` with index 0 from the primitive
- Handle the case where TEXCOORD_0 is absent in the .glb (emit `vec2(0,0)` for all vertices — silent fallback, no error)
- Upload UV data at stride consistent with the existing vertex layout

The existing vertex layout in the VBO (as implied by the shader) is:
- Location 0: `vec3 a_position` (12 bytes)
- Location 1: `vec3 a_normal`   (12 bytes)
- Location 2: `vec2 a_texcoord` (8 bytes)

If the loader stores these interleaved, stride is 32 bytes per vertex. If stored as separate VBOs (one per attribute), each attrib uses its own VBO. engine-dev must determine which layout is in use by reading `mesh_loader.cpp` and maintain consistency with whatever is there.

### B.2 — ffe.setMeshTexture Lua binding

**Signature:**

```lua
ffe.setMeshTexture(entityId, textureHandle)
```

**Semantics:**
- Sets `Material3D.diffuseTexture` on the entity to the given `rhi::TextureHandle`
- `textureHandle` is the integer handle returned by `ffe.loadTexture`
- Pass 0 to clear (reverts to white 1×1 fallback — untextured behaviour)
- Creates `Material3D` on the entity if not already present (same pattern as `ffe.setMeshColor`)
- Guard: entity must be valid (`world->isValid(entityId)`); otherwise no-op with FFE_LOG_ERROR
- Guard: textureHandle must be 0 (clear) or a value > 0 (any non-zero value is accepted as a handle — the RHI's `bindTexture` already handles invalid handles gracefully by binding a black texture, so we do not need a separate range check here beyond rejecting negative values)

**TextureHandle type note:** `rhi::TextureHandle` is `struct TextureHandle { u32 id = 0; }`. Lua passes integer handles. The binding reads the integer from Lua and writes `rhi::TextureHandle{static_cast<u32>(rawHandle)}` to the component.

**C++ binding implementation:**

```cpp
auto ffe_setMeshTexture = [](lua_State* state) -> int {
    if (lua_type(state, 1) != LUA_TNUMBER) {
        FFE_LOG_ERROR("ScriptEngine",
                      "ffe.setMeshTexture: argument 1 (entityId) must be a number");
        return 0;
    }

    lua_pushlightuserdata(state, &s_worldRegistryKey);
    lua_gettable(state, LUA_REGISTRYINDEX);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); return 0; }
    auto* world = static_cast<ffe::World*>(lua_touserdata(state, -1));
    lua_pop(state, 1);

    const lua_Integer rawId = lua_tointeger(state, 1);
    if (rawId < 0 || rawId > MAX_ENTITY_ID) { return 0; }
    const ffe::EntityId entityId = static_cast<ffe::EntityId>(rawId);
    if (!world->isValid(entityId)) {
        FFE_LOG_ERROR("ScriptEngine",
                      "ffe.setMeshTexture: invalid entity ID %" PRId64,
                      static_cast<long long>(rawId));
        return 0;
    }

    // textureHandle: 0 = clear, >0 = use this texture
    const lua_Integer rawTex = luaL_optinteger(state, 2, 0);
    if (rawTex < 0) {
        FFE_LOG_WARN("ScriptEngine",
                     "ffe.setMeshTexture: negative textureHandle ignored");
        return 0;
    }

    ffe::Material3D& mat = world->registry().get_or_emplace<ffe::Material3D>(
        static_cast<entt::entity>(entityId));
    mat.diffuseTexture = ffe::rhi::TextureHandle{static_cast<ffe::u32>(rawTex)};

    return 0;
};
lua_pushcfunction(L, ffe_setMeshTexture);
lua_setfield(L, -2, "setMeshTexture");
```

**Registration:** Add after the `ffe.setMeshColor` registration block (around line 4052 in script_engine.cpp).

### B.3 — Files modified for Feature B

| File | Change |
|------|--------|
| `engine/scripting/script_engine.cpp` | Add `ffe.setMeshTexture` binding block after `ffe.setMeshColor` |
| `engine/renderer/mesh_loader.cpp` | Add TEXCOORD_0 load + upload if absent (verify first) |

No changes to `shader_library.cpp`, `mesh_renderer.cpp`, `render_system.h`, or `mesh_loader.h`.

---

## Tests

engine-dev must add tests in `tests/` covering:

### Feature A tests

| Test | What to verify |
|------|----------------|
| FPS camera: yaw=0, pitch=0 | Camera at (0,0,5), forward = (0,0,1), target = (0,0,6) |
| FPS camera: yaw=90°, pitch=0 | forward.x ≈ 1, forward.z ≈ 0, forward.y = 0 |
| FPS camera: pitch=45° | forward.y ≈ 0.707, forward.xz magnitude ≈ 0.707 |
| FPS camera: pitch clamped at 89° | pitch=91° behaves identically to pitch=89° |
| FPS camera: pitch clamped at -89° | pitch=-91° behaves identically to pitch=-89° |
| Orbit camera: basic | radius=5, yaw=0, pitch=0 → camera at (target_x, target_y, target_z+5) |
| Orbit camera: radius=0 | No-op (camera unchanged) |
| Orbit camera: radius negative | No-op (camera unchanged) |
| Orbit camera: NaN input | No-op (camera unchanged) |
| Orbit camera: Inf input | No-op (camera unchanged) |
| Orbit camera: pitch clamped at 85° | pitch=90° behaves identically to pitch=85° |

Tests that require a Camera in ECS context: these are unit tests that construct a minimal `ffe::World`, emplace a `Camera` and `Camera*` in the context, then call the binding via `lua_pcall`. Follow the pattern of existing scripting tests in `tests/`.

### Feature B tests

| Test | What to verify |
|------|----------------|
| setMeshTexture on valid entity | Material3D.diffuseTexture.id == expected value |
| setMeshTexture creates Material3D | Component created if not present |
| setMeshTexture with handle 0 | Material3D.diffuseTexture.id == 0 (clear) |
| setMeshTexture with invalid entity | No-op, no crash |
| setMeshTexture with negative handle | No-op, no crash |
| UV data present in VAO | If TEXCOORD_0 was absent and is now added: test that getMeshGpuRecord returns non-null after loading a known .glb |

---

## Implementation Order

1. Verify UV data in mesh_loader.cpp (read the file; add TEXCOORD_0 if absent)
2. Implement Feature A bindings (set3DCameraFPS, set3DCameraOrbit)
3. Implement Feature B binding (setMeshTexture)
4. Write all tests
5. Do NOT build — hand off to build-engineer in Phase 5

---

## Flags and Open Questions for engine-dev

**FLAG 1 (MUST RESOLVE BEFORE IMPLEMENTING Feature B):**
Read `engine/renderer/mesh_loader.cpp` and confirm whether TEXCOORD_0 is loaded and uploaded to VAO attribute location 2. The shader at location 2 (`a_texcoord`) already exists. If the VBO does not supply UVs at location 2, any textured mesh will sample garbage. The loader fix is required before `ffe.setMeshTexture` is useful. If TEXCOORD_0 is absent in a given .glb, emit `vec2(0.0, 0.0)` silently (no error, no warning — missing UVs is a common glTF authoring state and must not crash the engine).

**FLAG 2 (CONFIRMED — no action needed):**
The brief said Material3D uses a field named `textureHandle`. The actual field name is `diffuseTexture` (type `rhi::TextureHandle`). The Lua binding must write to `mat.diffuseTexture`, not `mat.textureHandle`.

**FLAG 3 (CONFIRMED — no action needed):**
The brief said mesh_renderer.cpp needs changes to bind the texture and cache uniform locations. In fact mesh_renderer.cpp already binds `diffuseTex` via `rhi::bindTexture(diffuseTex, 0)` and sets `u_diffuseTexture` to 0 on every draw call. The uniform string lookup per-draw-call is a MINOR performance concern (noted by performance-critic in the past), but it is existing behaviour — do not change it as part of this session. If performance-critic flags it in Phase 3, engine-dev can address it in Phase 4.

**FLAG 4 (CONFIRMED — no action needed):**
The brief said the shader needs `u_hasTexture` and conditional sampling. The actual shader always samples `u_diffuseTexture`. The fallback to untextured rendering works via the `defaultWhite` 1×1 texture stored in ECS context. No shader changes are required. Do not add `u_hasTexture`.

**FLAG 5 (NOTE for Feature A):**
`M_PI` may not be defined under strict C++20 on all toolchains without `#define _USE_MATH_DEFINES` (MSVC) or `_GNU_SOURCE` (GCC). Use `static constexpr float PI = 3.14159265358979323846f;` as a local constant inside the binding to avoid portability risk. Do not rely on `M_PI` being defined.
