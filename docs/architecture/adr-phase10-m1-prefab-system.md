# ADR: Phase 10 M1 — Prefab System

**Status:** Accepted
**Author:** architect
**Date:** 2026-03-08
**Tiers:** LEGACY+ (OpenGL 3.3 minimum; the prefab system itself has no GPU dependency)
**Security Review Required:** YES — prefab loading reads external JSON files from disk (path traversal, file size bombs, JSON depth attacks, component injection via Lua override tables all apply).

---

## 1. Context

FFE now has a mature 3D rendering pipeline (Blinn-Phong, PBR, GPU instancing, skeletal animation, terrain, water) and a working ECS (EnTT-backed `World` class). What is missing is a productive workflow for populating scenes with reusable entity templates.

Today, spawning a "tree" entity requires game code to: create an entity, call `world.addComponent<Transform3D>(...)`, call `world.addComponent<Mesh>(...)`, call `world.addComponent<Material3D>(...)`, and repeat identically for every tree. This pattern does not scale to scenes with hundreds of objects. It also means game designers cannot tune entity templates without recompiling C++ or rewriting Lua scripts.

Unity's prefab system and Godot's scene instancing solve this through data-driven entity templates: define a template once as a file, instantiate it many times at runtime with optional per-instance overrides. FFE needs equivalent capability.

Phase 10 M1 delivers the prefab system: a JSON-driven entity template loader and instantiator integrated with the ECS, exposed to Lua, and hardened against untrusted inputs.

### Scope of M1

M1 is deliberately focused on the 3D use case, which is where scene population pain is highest:

- **In scope:** Transform3D, Mesh, Material3D, PBRMaterial component templates
- **Out of scope for M1:** 2D components (Transform, Sprite, SpriteAnimation, Tilemap, ParticleEmitter), nested prefabs, string-type overrides, prefab variants/inheritance

M1 is additive. It does not replace or modify the ECS, SceneSerialiser, or any existing subsystem.

---

## 2. Decision

### 2.1 File Location: `engine/core/`

**Decision:** `engine/core/prefab_system.h` and `engine/core/prefab_system.cpp`.

**Rationale:**

A prefab is an entity template — a description of which components to attach to a newly created entity and what their initial values should be. This is fundamentally an ECS-level concern, not a rendering concern. The prefab system does not render anything, does not touch the RHI, and does not depend on any shader or GPU resource. It creates entities and sets component data.

The renderer owns component *types* (Transform3D, Mesh, Material3D are defined in `render_system.h`) in the same way that a factory owns the parts it uses — but the factory does not live in the parts department. `PrefabSystem` reads those component definitions as a consumer, not as a renderer subsystem.

**Comparison with SceneSerialiser:**

SceneSerialiser lives in `engine/scene/` (not `engine/renderer/` as originally assumed — confirmed from source). It loads multi-entity scenes (entire rooms, levels) and is tightly coupled to scene-level state: it clears the world, resolves entity counts, and handles scene format versioning. Prefabs are single-entity templates with a different scope and lifecycle. They do not belong in `engine/scene/` either, because:

1. Prefabs are instantiated repeatedly at runtime (hot-ish path); scenes are loaded once.
2. Prefabs carry override data at instantiation call sites; scene loading is fire-and-forget.
3. Prefabs are a core ECS pattern independent of any rendering or scene graph concern.

`engine/core/` is where the ECS (`ecs.h`), platform utilities (`platform.h`), types (`types.h`), and the arena allocator live. It is the correct home for a system whose primary job is entity construction.

**Files:**
- `engine/core/prefab_system.h` — public API (PrefabHandle, PrefabOverride, PrefabOverrides, PrefabSystem class)
- `engine/core/prefab_system.cpp` — implementation (JSON parsing, pool management, component application)

**Tests:** `tests/core/test_prefab_system.cpp`

---

### 2.2 Prefab JSON Format

A prefab file is a single JSON object describing one entity template. The file extension is `.json`. Files are stored under the game's asset directory (typically `assets/prefabs/`).

#### Schema

```json
{
  "name": "tree",
  "components": {
    "Transform3D": {
      "x": 0.0, "y": 0.0, "z": 0.0,
      "rotX": 0.0, "rotY": 0.0, "rotZ": 0.0,
      "scaleX": 1.0, "scaleY": 1.0, "scaleZ": 1.0
    },
    "Mesh": {
      "path": "assets/models/tree.glb"
    },
    "Material3D": {
      "r": 1.0, "g": 1.0, "b": 1.0,
      "metallic": 0.0, "roughness": 0.8
    }
  }
}
```

#### Supported Component Types (M1)

| Component key | Fields | Notes |
|---------------|--------|-------|
| `"Transform3D"` | `x, y, z, rotX, rotY, rotZ, scaleX, scaleY, scaleZ` | All float. Defaults: position (0,0,0), rotation (0,0,0), scale (1,1,1). |
| `"Mesh"` | `path` | String. Path to .glb asset file. Required if Mesh component is specified. |
| `"Material3D"` | `r, g, b, metallic, roughness` | All float. Defaults: color (1,1,1), metallic 0.0, roughness 0.5. |
| `"PBRMaterial"` | `albedoR, albedoG, albedoB, metallic, roughness, ao` | All float. Defaults: albedo (1,1,1), metallic 0.0, roughness 0.5, ao 1.0. |

Any key inside `"components"` that is not in the whitelist above is **skipped with a log warning** and does not cause a parse failure. This ensures forward compatibility: a prefab file authored for a future FFE version with additional component types loads without error on an older build.

#### Constraints

| Constraint | Value | Reason |
|------------|-------|--------|
| Maximum file size | 1 MB | Prefab files are single-entity templates; 1 MB is generous. Pre-read guard prevents memory exhaustion from file size bombs. |
| Maximum JSON nesting depth | 8 levels | Prevents stack overflow during recursive JSON parsing. The schema has at most 3 levels in practice; 8 gives headroom for future extension without risk. |
| Maximum prefab name length | 63 characters | Stored inline in PrefabData; avoids heap allocation for metadata. |

#### Top-Level Field Rules

- `"name"` — optional string. If omitted, the slot index is used as an internal identifier. Not exposed to game code.
- `"components"` — required object. If absent or not an object, the file is rejected.
- All other top-level keys are ignored (forward compatibility).

---

### 2.3 PrefabOverrides: Fixed-Size Inline Struct

Instantiation-time overrides are passed as a value-type struct. The common case (≤8 overrides) requires zero heap allocation.

```cpp
namespace ffe {

struct PrefabOverride {
    char component[32];      // Component type name (e.g., "Transform3D")
    char field[32];          // Field name (e.g., "x")
    union {
        float f;
        int   i;
        bool  b;
    } value;
    enum class Type : uint8_t { Float, Int, Bool } type;
};

struct PrefabOverrides {
    static constexpr int MAX = 8;
    PrefabOverride items[MAX];
    int count = 0;

    // Set a float field override.
    void set(const char* component, const char* field, float v);

    // Set an int field override.
    void set(const char* component, const char* field, int v);

    // Set a bool field override.
    void set(const char* component, const char* field, bool v);
};

} // namespace ffe
```

**Size budget (LEGACY tier):**
- `PrefabOverride`: 32 + 32 + 4 + 1 = 69 bytes, padded to 72 bytes by the compiler.
- `PrefabOverrides`: 8 * 72 + 4 = 580 bytes, padded to 584. Fits easily on the stack.

**Overflow policy:** If the caller calls `set()` when `count == MAX`, a warning is logged and the override is silently dropped. The first 8 overrides are applied. No heap allocation, no crash.

**String field limitation (M1):** Only numeric (`float`, `int`) and boolean fields can be overridden via `PrefabOverrides`. String fields (specifically `Mesh::path`) cannot be overridden via this mechanism in M1. The `component[32]` and `field[32]` fields name the target — they are not the value. A string-value override would require either heap allocation (violating the design) or a separate large fixed buffer. This is documented as a known limitation and deferred to M2.

**Rationale for the 8-override limit:** In practice, the most common use of overrides is to set position (3 floats: x, y, z) and possibly scale (3 more) and rotation (3 more) — that is 9 floats for a fully specified Transform3D. Eight covers the common cases (position-only, position+scale) without heap cost. For uncommon cases requiring more overrides, callers should edit the prefab file rather than pass overrides.

---

### 2.4 Public API

```cpp
namespace ffe {

class PrefabSystem {
public:
    // --- Cold path: file I/O, JSON parse, pool storage. Heap allocation OK. ---

    // Load a prefab from a JSON file. Canonicalizes and validates the path,
    // enforces file size and JSON depth limits, parses component data.
    // Returns PrefabHandle{0} on any error; logs the failure reason.
    PrefabHandle loadPrefab(std::string_view path);

    // Unload a previously loaded prefab, freeing its pool slot.
    // Passing PrefabHandle{0} or an already-unloaded handle is a no-op.
    void unloadPrefab(PrefabHandle handle);

    // Number of currently loaded prefabs.
    int getPrefabCount() const;

    // --- Hot path: entity creation + component application. No heap. ---

    // Create a new entity in `world`, apply the prefab's component template,
    // then patch any fields specified in `overrides`. Returns the new EntityId.
    // Returns NULL_ENTITY if the handle is invalid or the pool slot is free.
    EntityId instantiatePrefab(World& world, PrefabHandle handle,
                               const PrefabOverrides& overrides = {});

    // Set the asset root directory. loadPrefab() rejects paths that resolve
    // outside this directory. Must be called before any loadPrefab().
    // Default: the process working directory.
    void setAssetRoot(std::string_view root);
};

} // namespace ffe
```

#### PrefabHandle

```cpp
namespace ffe {

struct PrefabHandle {
    uint32_t id = 0;
};

inline bool isValid(const PrefabHandle h) { return h.id != 0; }
static_assert(sizeof(PrefabHandle) == 4);

} // namespace ffe
```

`PrefabHandle{0}` is the null/invalid value. Valid handles have `id` in the range [1, MAX_PREFABS - 1]. This is consistent with `MeshHandle`, `TerrainHandle`, and all other FFE opaque handle types.

#### Error Reporting

`PrefabSystem` never throws exceptions (engine core policy). Errors are reported via `FFE_LOG_ERROR` and a null return value (`PrefabHandle{0}` or `NULL_ENTITY`). The caller is responsible for checking.

---

### 2.5 Relation to SceneSerialiser

SceneSerialiser (`engine/scene/scene_serialiser.h`) and PrefabSystem are independent systems that share utilities but share no code.

| Dimension | SceneSerialiser | PrefabSystem |
|-----------|----------------|--------------|
| Scope | Multi-entity scenes (whole levels, rooms) | Single-entity templates |
| Load result | Populates `World` in place | Returns one `EntityId` |
| File format | Scene graph JSON with entity array | Single-entity component object |
| File size limit | 64 MB | 1 MB |
| Instantiation call | `loadScene(world, path)` — one path, fires once | `instantiatePrefab(world, handle, overrides)` — one handle, fires many times |
| Override mechanism | None (deterministic replay) | `PrefabOverrides` struct |
| JSON library | `nlohmann/json` | `nlohmann/json` (same — no new dependency) |

**Shared utilities (by convention, not inheritance):**

1. **Path canonicalization:** Both call `ffe::canonicalizePath()` from `engine/core/platform.h` before opening any file.
2. **Asset root validation:** Both verify the canonical path is within the declared asset root before opening.
3. **JSON library:** Both use `nlohmann/json`. No new vcpkg dependency.

There is no shared base class, no inheritance hierarchy, and no function-pointer dispatch between the two systems. They share the above utilities by calling the same free functions. This keeps both systems simple and independently testable.

---

### 2.6 Internal Storage: PrefabPool

```cpp
namespace ffe {

// Internal — not in the public header.
struct PrefabData {
    bool occupied = false;
    char name[64] = {};              // Display name (from JSON "name" field or slot index)

    // --- Transform3D template (optional) ---
    bool hasTransform3D = false;
    float tx = 0.f, ty = 0.f, tz = 0.f;       // position
    float rx = 0.f, ry = 0.f, rz = 0.f;       // rotation (Euler, degrees)
    float sx = 1.f, sy = 1.f, sz = 1.f;       // scale

    // --- Mesh template (optional) ---
    bool hasMesh = false;
    std::string meshPath;            // Heap OK: cold data, loaded once

    // --- Material3D template (optional) ---
    bool hasMaterial3D = false;
    float matR = 1.f, matG = 1.f, matB = 1.f;
    float matMetallic = 0.f, matRoughness = 0.5f;

    // --- PBRMaterial template (optional) ---
    bool hasPBRMaterial = false;
    float pbrAlbedoR = 1.f, pbrAlbedoG = 1.f, pbrAlbedoB = 1.f;
    float pbrMetallic = 0.f, pbrRoughness = 0.5f, pbrAo = 1.0f;
};

// Pool capacity. Slot 0 is reserved (invalid handle). Slots 1–63 are usable.
inline constexpr int MAX_PREFABS = 64;

} // namespace ffe
```

**Implementation layout inside PrefabSystem:**

```cpp
PrefabData m_pool[MAX_PREFABS]; // Slot 0 always occupied=false, never used
char m_assetRoot[512] = {};
```

`MAX_PREFABS = 64` because:
- Prefabs are templates, not instances. 64 distinct entity types is enough for large games. If more are needed in a future milestone, the constant can be raised — the pool is static.
- On LEGACY tier, `PrefabData` is approximately 250 bytes (dominated by `std::string meshPath` = 32 bytes on common STL implementations, plus the boolean flags and floats). The full pool is ~16 KB — well within the LEGACY 2 MB arena.

**Slot allocation:** Linear scan from slot 1 on `loadPrefab`. First free slot wins. On `unloadPrefab`, set `occupied = false` and reset all fields. No fragmentation concern at 64 slots.

**Handle-to-slot mapping:** `handle.id` is the direct slot index. `pool[handle.id]` is the `PrefabData`. Bounds-checked in `instantiatePrefab`: reject if `handle.id == 0 || handle.id >= MAX_PREFABS || !pool[handle.id].occupied`.

---

### 2.7 Instantiation Algorithm

`instantiatePrefab` is the hot path. It must not heap-allocate. The algorithm:

1. Validate handle (bounds check + `occupied` flag). Return `NULL_ENTITY` on failure.
2. `EntityId eid = world.createEntity()` — EnTT create is O(1), no heap.
3. If `data.hasTransform3D`:
   - `auto& t = world.addComponent<Transform3D>(eid)` with default values.
   - Set `t.x = data.tx`, etc. (9 float assignments).
   - Apply any `PrefabOverrides` entries targeting `"Transform3D"`.
4. If `data.hasMesh`:
   - `auto& m = world.addComponent<Mesh>(eid)`.
   - Look up `MeshHandle` from `data.meshPath` via the mesh loader's handle registry.
   - Set `m.handle = resolvedHandle`.
   - Note: the mesh itself was GPU-uploaded at `loadPrefab` time or by prior game code. The prefab system does not load GPU resources.
5. If `data.hasMaterial3D`:
   - `auto& mat = world.addComponent<Material3D>(eid)`.
   - Set fields from `data.*`. Apply overrides targeting `"Material3D"`.
6. If `data.hasPBRMaterial`:
   - `auto& pbr = world.addComponent<PBRMaterial>(eid)`.
   - Set fields from `data.*`. Apply overrides targeting `"PBRMaterial"`.
7. Return `eid`.

**Override application (step 3/5/6):** For each `PrefabOverride` in `overrides.items[0..count-1]`:
- `strcmp(override.component, "Transform3D")` — if match, `strcmp(override.field, "x")` etc. and assign `override.value.f` to the corresponding component field.
- This is a flat if/else chain over the known fields. No heap, no virtual dispatch, no `std::function`. O(overrides.count * fields_per_component) = O(72) worst case on LEGACY.

**Mesh handle resolution:** The Mesh component stores a `MeshHandle`. The prefab stores a path string. At instantiation time, the prefab system must resolve the path to a handle. The resolution strategy: call the mesh loader's `findHandleByPath(std::string_view path)` function (a linear scan over the mesh pool, O(MAX_MESH_ASSETS) = O(100)). If not found, log a warning and leave the Mesh component with an invalid handle. The game developer is responsible for loading mesh assets before instantiating prefabs that reference them. This keeps the prefab system free of GPU resource management responsibilities.

---

### 2.8 Lua API

The Lua bindings expose `loadPrefab`, `instantiatePrefab`, and `unloadPrefab`. The override table format mirrors the JSON schema: keys are component names, values are tables of field-value pairs.

```lua
-- Load a prefab from disk (cold path — call at startup or scene load)
local handle = ffe.loadPrefab("assets/prefabs/tree.json")

-- Instantiate with no overrides (all fields from template)
local eid = ffe.instantiatePrefab(handle)

-- Instantiate with position override
local eid2 = ffe.instantiatePrefab(handle, {
    Transform3D = { x = 10.0, y = 0.0, z = 5.0 }
})

-- Instantiate with multiple component overrides
local eid3 = ffe.instantiatePrefab(handle, {
    Transform3D = { x = -5.0, y = 2.0, z = 0.0, scaleX = 2.0, scaleY = 2.0, scaleZ = 2.0 },
    Material3D  = { r = 0.8, g = 0.6, b = 0.4 }
})

-- Unload when no longer needed
ffe.unloadPrefab(handle)
```

**Lua binding implementation notes:**

1. `ffe.loadPrefab(path)` — string argument. Returns integer handle id (0 on failure).
2. `ffe.instantiatePrefab(handle [, overrides])` — integer handle, optional table. Returns integer entity id (or `ffe.NULL_ENTITY` on failure).
3. `ffe.unloadPrefab(handle)` — integer handle. No return value.

**Override table binding algorithm:**

The binding iterates the Lua table (second argument) using `lua_next`. For each key (component name string), it iterates the nested table using `lua_next` again (field name + value). For each field:
- If value is a number (`lua_isnumber`): call `overrides.set(component, field, (float)lua_tonumber(...))`.
- If value is a boolean (`lua_isboolean`): call `overrides.set(component, field, (bool)lua_toboolean(...))`.
- If value is any other type: log a warning and skip. Do not error — unexpected types are ignored for forward compatibility.

The binding must check `overrides.count < PrefabOverrides::MAX` before calling `set()` (the `set()` method already guards, but explicit checking in the binding allows a single descriptive warning rather than MAX silent drops).

**Type safety:** The Lua binding does not perform component-field validation beyond type checking (number vs bool vs other). Unknown component names or field names are passed through to `PrefabOverrides` as-is; the C++ instantiation code ignores unrecognised component/field pairs when applying overrides, logging a warning.

**String truncation:** Component name and field name strings from Lua are truncated to 31 characters with explicit null-termination (`strncpy(dest, src, 31); dest[31] = '\0'`) before being stored in the `PrefabOverride` struct. This prevents a long Lua string from overflowing the fixed `char[32]` buffers in `PrefabOverride::component` and `PrefabOverride::field`.

---

### 2.9 Security Surface

This section is written for `critic`'s shift-left review.

#### Threat 1: Path Traversal via `loadPrefab`

**Attack:** `ffe.loadPrefab("../../etc/passwd")` or `ffe.loadPrefab("/etc/shadow")`.

**Mitigation:**
1. `loadPrefab` calls `ffe::canonicalizePath(path, buf, sizeof(buf))` (from `engine/core/platform.h`). On POSIX this uses `realpath(path, nullptr)` which resolves all symlinks and `..` components. On Windows this uses `_fullpath`.
2. After canonicalization, verify that the canonical path starts with the declared asset root (`m_assetRoot`). If not, log an error and return `PrefabHandle{0}`. No file is opened.
3. The asset root itself is canonicalized at `setAssetRoot()` time. `m_assetRoot` is stored as the result of `canonicalizePath` applied to the caller-supplied root, so trailing-slash inconsistencies cannot cause false negatives in the prefix check.

**Asset root prefix check precision:** The prefix check must require that the canonical path starts with `m_assetRoot` followed by a path separator (`/` on POSIX, `\` on Windows) or equals `m_assetRoot` exactly — not merely that it starts with the string. This prevents a path such as `/game/assets-evil/tree.json` from passing a check against a root of `/game/assets`.

**UNC path gap (Windows):** `ffe::canonicalizePath` calls `_fullpath` directly without a pre-check for UNC paths (`\\server\share\..` patterns). The implementation must add a pre-canonicalization check in `loadPrefab` that rejects any path beginning with `\\` on Windows before calling `canonicalizePath`, returning `PrefabHandle{0}` immediately. On POSIX, `realpath` does not process UNC paths and is not affected.

**Failure modes:** If `canonicalizePath` returns false (file does not exist, or buffer too small), the load fails immediately — no file is opened.

#### Threat 2: File Size Bomb

**Attack:** A crafted prefab file that is gigabytes in size, exhausting memory when read into a `std::string` or `nlohmann::json` parse buffer.

**Mitigation:**
1. Before reading the file, `stat()` (POSIX) or `GetFileAttributesEx()` (Windows) the file to obtain its size.
2. If size > `MAX_PREFAB_FILE_SIZE` (1 MB), log an error and return `PrefabHandle{0}`. The file is not opened for reading.
3. The 1 MB limit is generous for any single-entity template. A legitimate prefab file is expected to be well under 1 KB.

**Pipeline ordering:** `stat` is called on the canonical, asset-root-validated path — not on the raw user-supplied path. The correct pipeline order is: (1) canonicalize, (2) asset-root check, (3) stat + size check, (4) open and read. This prevents leaking file-existence information about paths outside the asset directory via `stat` timing or error codes.

#### Threat 3: JSON Nesting Depth Attack

**Attack:** A JSON file with thousands of levels of nesting, causing stack overflow in the parser's recursive descent.

**Mitigation:**
- `nlohmann::json` supports a configurable `max_depth` parameter on `json::parse()`. Pass `max_depth = 8` (the `json::parse` overload that accepts `allow_exceptions=false, cbor=false, max_depth=8` or equivalent). If the depth limit is exceeded, the parse returns a discarded value.
- Verified against nlohmann/json 3.x API: `json::parse(input, nullptr, false, false)` uses the default depth limit; the explicit depth-limited overload is `json::parse(input, nullptr, false, false, 8)` (the fifth parameter is `max_depth` as of nlohmann v3.11+). Confirm the exact API against the vcpkg-vendored version during implementation.
- If the depth limit is not available in the vendored version, implement a manual depth counter using a recursive descent over the parsed object (post-parse check): count maximum nesting depth and reject if > 8.

#### Threat 4: Component Whitelist Bypass

**Attack:** A crafted prefab JSON with a component key that causes unexpected behaviour — e.g., exploiting future component types not yet implemented, or injecting a component name that confuses the binding.

**Mitigation:**
- The component name whitelist is explicitly checked against: `"Transform3D"`, `"Mesh"`, `"Material3D"`, `"PBRMaterial"`.
- Any key not in this list is skipped with `FFE_LOG_WARN`. The skip is unconditional — there is no fallthrough, no dynamic dispatch on the component name.
- The check is a series of `strcmp` calls, not a hash or dynamic lookup, so there is no hash-collision attack surface.

#### Threat 5: Override Injection via Lua

**Attack:** Lua game code (possibly from an untrusted Lua script loaded from disk) passes override values of unexpected types — e.g., a table where a float is expected, or a function object.

**Mitigation:**
- The Lua binding checks `lua_isnumber` or `lua_isboolean` before calling `overrides.set()`. Any other type (`LUA_TTABLE`, `LUA_TFUNCTION`, `LUA_TSTRING`, etc.) is skipped with a log warning.
- `overrides.set()` writes into a fixed-size union with typed discriminator (`PrefabOverride::Type`). There is no type confusion in the C++ side — the type is set by the binding, not by the JSON data.
- Integer values from Lua (`lua_isinteger`) are treated as float (converted via `(float)lua_tonumber`) unless the override type can be unambiguously determined from the target field. In M1, all numeric fields are `float`; there are no `int` fields in the supported components. The `int` variant of `PrefabOverride` is reserved for future components.

#### Threat 7: `meshPath` Secondary Use at Instantiation Time

**Attack:** `meshPath` stored in `PrefabData` is a string that was validated against the asset root at `loadPrefab` time. If `instantiatePrefab` were to re-open that path from disk, a time-of-check/time-of-use (TOCTOU) or secondary path traversal risk would exist.

**Mitigation:** At `instantiatePrefab` time, `findHandleByPath(data.meshPath)` performs an in-memory lookup only — no disk I/O — so the stored string cannot be used for a secondary path traversal attack. If the mesh loader ever evolves to perform disk I/O inside `findHandleByPath`, it must apply the same canonicalization and asset-root validation as `loadPrefab` before opening any file.

#### Threat 6: Handle Validation

**Attack:** Pass a crafted `PrefabHandle` with an out-of-bounds `id` to `instantiatePrefab` (C++ or Lua).

**Mitigation:**
- `instantiatePrefab` performs: `if (handle.id == 0 || handle.id >= MAX_PREFABS || !m_pool[handle.id].occupied)` before any access. Returns `NULL_ENTITY`.
- The Lua binding converts the Lua integer to `uint32_t` with explicit bounds checking before constructing the handle.

---

### 2.10 Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO (OpenGL 2.1) | Yes | PrefabSystem has no GPU dependency. Components it applies have their own tier requirements. |
| LEGACY (OpenGL 3.3) | Yes — primary target | Default development tier. |
| STANDARD (OpenGL 4.5 / Vulkan) | Yes | No changes needed. |
| MODERN (Vulkan) | Yes | No changes needed. |

The prefab system is tier-agnostic. It creates entities and writes component data. Whether those components are rendered depends on the renderer tier — PrefabSystem does not need to know.

**VRAM budget:** `PrefabSystem` holds no GPU resources. The `m_pool[64]` is ~16 KB of CPU RAM. Negligible on all tiers.

---

### 2.11 Test Plan

`tests/core/test_prefab_system.cpp` must cover all of the following. This list is the minimum; additional edge cases are encouraged.

| # | Category | Test Description |
|---|----------|-----------------|
| 1 | Happy path | Load a valid Transform3D-only prefab; verify entity has correct Transform3D values. |
| 2 | Happy path | Load a prefab with all four supported components; verify all four are applied correctly. |
| 3 | Happy path | Instantiate the same prefab handle twice; verify two distinct entity IDs are returned. |
| 4 | Happy path | Float override on Transform3D.x; verify entity transform reflects override value. |
| 5 | Happy path | Multiple overrides across two components in one call; verify all applied. |
| 6 | Happy path | `instantiatePrefab` with empty `PrefabOverrides{}` (default arg); verify template values used. |
| 7 | Happy path | `unloadPrefab` frees the slot; `getPrefabCount()` decrements; handle becomes invalid. |
| 8 | Happy path | Load 63 prefabs (fill all valid slots); verify `getPrefabCount() == 63`. |
| 9 | Limits | Overflow `PrefabOverrides::MAX` (9 set() calls); verify first 8 applied, 9th dropped (no crash). |
| 10 | Limits | Load a 64th prefab when pool is full (all 63 slots occupied); verify `PrefabHandle{0}` returned. |
| 11 | Limits | Unload one prefab from a full pool; verify a new `loadPrefab` succeeds in the freed slot. |
| 12 | Invalid input | `loadPrefab` with a path to a non-existent file; verify `PrefabHandle{0}`, no crash. |
| 13 | Invalid input | `loadPrefab` with a file exceeding 1 MB; verify rejected before parsing. |
| 14 | Invalid input | `loadPrefab` with JSON missing the `"components"` key; verify `PrefabHandle{0}`. |
| 15 | Invalid input | `loadPrefab` with JSON nesting deeper than 8 levels; verify `PrefabHandle{0}`. |
| 16 | Invalid input | `instantiatePrefab` with `PrefabHandle{0}`; verify `NULL_ENTITY` returned, no crash. |
| 17 | Invalid input | `instantiatePrefab` with an out-of-bounds handle id (e.g., id=255); verify `NULL_ENTITY` returned. |
| 18 | Security | `loadPrefab("../../etc/passwd")`; verify path traversal rejected (PrefabHandle{0}), file not opened. |
| 19 | Security | `loadPrefab` with a path outside the asset root after canonicalization; verify rejected. |
| 20 | Forward compat | Prefab JSON with an unknown component key (`"FutureComponent": {}`); verify load succeeds, unknown key is ignored, known components applied. |
| 21 | Forward compat | Prefab JSON with an unknown field inside a known component; verify load succeeds, unknown field ignored, known fields applied. |

---

## 3. Consequences

### Positive

- **Eliminates scene population boilerplate.** Spawning 200 trees is now `for (int i = 0; i < 200; i++) ffe.instantiatePrefab(treeHandle, { Transform3D = { x = i * 5.0 } })`.
- **Data-driven entity design.** Game designers can tune entity templates in JSON without recompiling or editing Lua scripts.
- **Zero heap allocation on the hot path.** `instantiatePrefab` creates an entity and applies components without touching the heap. EnTT's `emplace` may allocate internally on first component type registration, but per-instance instantiation after warmup is allocation-free.
- **Consistent handle pattern.** `PrefabHandle` follows the same opaque `uint32_t` pattern as `MeshHandle`, `TerrainHandle`, and all other FFE asset handles. Lua developers will find the API immediately familiar.
- **No new vcpkg dependency.** Uses nlohmann/json (already vendored by SceneSerialiser) and platform.h (already in core).
- **Hardened against the most common attack vectors.** Path traversal, file size bombs, JSON depth attacks, and component injection are all explicitly guarded.

### Tradeoffs

- **String overrides deferred.** Overriding a mesh path at instantiation time (e.g., "use this tree prefab but load a different .glb") requires passing a different prefab file. This is deliberate: string overrides require either heap allocation or a large fixed buffer, both of which compromise the zero-heap-on-hot-path goal.
- **Linear scan for mesh handle resolution.** `findHandleByPath` is O(MAX_MESH_ASSETS) = O(100). For games spawning thousands of entities per second, this could be a bottleneck. Mitigation: game code should pre-load all mesh assets at startup, and the mesh loader can cache a path-to-handle map if profiling shows it is needed. This is not a Phase 10 M1 concern.
- **64 prefab limit.** Sufficient for all current FFE example games. Raisable in a later milestone by increasing `MAX_PREFABS` — the only cost is a larger static pool.
- **M1 supports 3D components only.** 2D prefabs (Sprite, SpriteAnimation, Tilemap) require separate M2 work. Games using both 2D and 3D entities cannot template 2D entities via prefabs in M1.
- **SceneSerialiser not unified.** Prefabs and scenes are separate systems. If a developer wants to place a prefab-defined entity in a saved scene, they must instantiate the prefab before saving. There is no "prefab reference" in the scene file format in M1.

---

## 4. Deferred Decisions

### 4.1 Nested Prefabs

A prefab that references another prefab (e.g., a "camp" prefab that contains "tree" prefabs and "rock" prefabs). Deferred to Phase 10 M2 or later. Key design questions for that milestone:

- How to represent the reference in JSON (by path? by name?).
- Cycle detection (prefab A contains prefab B which contains prefab A).
- Whether the depth limit (8 levels) also applies to prefab nesting.
- How overrides propagate through nesting levels.

### 4.2 String Field Overrides

Overriding `Mesh::path` (or other string fields) at instantiation time. Requires either:
- A separate `std::string` override list (heap allocation — violates the no-heap-on-hot-path rule), or
- A fixed-size string buffer per override (wastes stack space if paths are long), or
- A string interning table (adds complexity and a new system).

Deferred until the performance tradeoff is profiled. The common case is to have separate prefab files for each mesh variant.

### 4.3 2D Component Support

Extending the prefab system to support Transform, Sprite, SpriteAnimation, Tilemap, and ParticleEmitter. The design is identical — add `has*` flags and corresponding data fields to `PrefabData`, add the component names to the JSON whitelist, add application logic in `instantiatePrefab`. Deferred to keep M1 focused and reviewable.

### 4.4 Prefab Variants and Inheritance

A "tall tree" variant that inherits from "tree" and overrides only scale. Unity-style prefab variants. This requires a parent-child relationship between PrefabData slots and a merge algorithm at load time. Deferred to a future phase — the common alternative (separate prefab files) works fine for M1.

### 4.5 Prefab Instance Tracking

Tracking which entities were spawned from a given prefab (for "apply-to-all-instances" workflows in the editor). Requires a per-entity component (`PrefabInstanceTag { PrefabHandle source; }`) or an inverse mapping in PrefabSystem. Deferred to the editor integration milestone when it becomes necessary.

### 4.6 Scene Serialiser Integration

Storing a prefab reference in a scene file rather than inlining all component data. Requires a `"prefab"` key in the scene format and logic in SceneSerialiser to instantiate prefabs on load. Deferred — the systems are intentionally independent in M1.
