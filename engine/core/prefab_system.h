#pragma once

// prefab_system.h — Data-driven entity template system for FFE.
//
// A prefab is a JSON file describing a single entity template: which components
// to attach and what their initial field values are. At runtime, call
// instantiatePrefab() to create a new entity populated from the template, with
// optional per-call field overrides (no heap allocation on the hot path).
//
// Supported components (M1): Transform3D, Mesh, Material3D, PBRMaterial.
// 2D components, nested prefabs, and string-field overrides are deferred to M2.
//
// Pool capacity: 64 prefab slots (slot 0 reserved as null/invalid).
// File size limit: 1 MB per prefab file.
// Override limit: 8 overrides per instantiatePrefab() call (stack-allocated).
//
// Security: loadPrefab() canonicalizes the path and verifies it is within the
// declared asset root before opening any file. See ADR adr-phase10-m1-prefab-system.md.
//
// Tiers: RETRO / LEGACY / STANDARD / MODERN — the prefab system has no GPU
// dependency. Component rendering depends on the renderer tier, not on this system.
//
// File ownership: engine/core/ (engine-dev)

#include "core/types.h"
#include "core/ecs.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ffe {

// ---------------------------------------------------------------------------
// PrefabHandle — opaque 32-bit handle to a loaded prefab template.
//
// id == 0 is the null/invalid value. Valid handles have id in [1, 63].
// Consistent with MeshHandle, TerrainHandle, and all other FFE asset handles.
// ---------------------------------------------------------------------------
struct PrefabHandle {
    uint32_t id = 0;

    explicit operator bool() const { return id != 0; }
};

inline bool isValid(const PrefabHandle h) { return h.id != 0; }
static_assert(sizeof(PrefabHandle) == 4);

// ---------------------------------------------------------------------------
// PrefabOverride — a single field override applied at instantiation time.
//
// Names the target component and field by fixed-size char arrays (no heap).
// The value is a tagged union covering the numeric and boolean field types
// supported in M1. String-field overrides are deferred to M2.
// ---------------------------------------------------------------------------
struct PrefabOverride {
    char component[32];   // Component type name  (e.g., "Transform3D") — null-terminated
    char field[32];       // Field name            (e.g., "x")           — null-terminated

    union {
        float f;
        int   i;
        bool  b;
    } value;

    enum class Type : uint8_t { Float, Int, Bool } type;
};

// ---------------------------------------------------------------------------
// PrefabOverrides — fixed inline array of up to 8 per-instantiation overrides.
//
// Passes by value to instantiatePrefab — entirely stack-allocated (~584 bytes).
// If set() is called when count == MAX, the override is dropped and a warning
// is logged. No heap allocation. No crash.
// ---------------------------------------------------------------------------
struct PrefabOverrides {
    static constexpr int MAX = 8;

    PrefabOverride items[MAX];
    int count = 0;

    // Append a float override. No-op (with warning) if count == MAX.
    void set(const char* component, const char* field, float v);

    // Append an int override. No-op (with warning) if count == MAX.
    void set(const char* component, const char* field, int v);

    // Append a bool override. No-op (with warning) if count == MAX.
    void set(const char* component, const char* field, bool v);
};

// ---------------------------------------------------------------------------
// PrefabSystem — loads prefab templates from JSON, instantiates ECS entities.
//
// Usage:
//   PrefabSystem prefabs;
//   prefabs.setAssetRoot("assets/");
//
//   PrefabHandle tree = prefabs.loadPrefab("assets/prefabs/tree.json");
//   if (!tree) { /* handle error */ }
//
//   EntityId eid = prefabs.instantiatePrefab(world, tree);
//
//   PrefabOverrides ov;
//   ov.set("Transform3D", "x", 10.0f);
//   EntityId eid2 = prefabs.instantiatePrefab(world, tree, ov);
//
//   prefabs.unloadPrefab(tree);
//
// Thread safety: single-threaded. All calls must be made from the main thread.
// ---------------------------------------------------------------------------
class PrefabSystem {
public:
    PrefabSystem();
    ~PrefabSystem();

    // --- Cold path: file I/O, JSON parsing, pool management ---

    // Set the asset root directory. loadPrefab() rejects any path that resolves
    // outside this directory after canonicalization. Must be called before any
    // loadPrefab(). Default: the process working directory.
    void setAssetRoot(std::string_view root);

    // Load a prefab from a JSON file at `path`. Canonicalizes the path, checks
    // it is within the asset root, enforces the 1 MB file-size limit and the
    // 8-level JSON nesting limit, then parses component data into a pool slot.
    // Returns PrefabHandle{0} on any error; logs the reason via FFE_LOG_ERROR.
    PrefabHandle loadPrefab(std::string_view path);

    // Release a prefab slot. The handle becomes invalid immediately.
    // Passing PrefabHandle{0} or an already-unloaded handle is a no-op.
    void unloadPrefab(PrefabHandle handle);

    // Return the number of currently loaded prefabs.
    int getPrefabCount() const;

    // --- Hot path: entity creation + component application (no heap) ---

    // Create a new entity in `world`, apply the prefab's component template,
    // then patch fields listed in `overrides`. Returns the new EntityId.
    // Returns NULL_ENTITY if the handle is invalid or the slot is unoccupied.
    EntityId instantiatePrefab(World& world, PrefabHandle handle,
                               const PrefabOverrides& overrides = {});

private:
    struct PrefabData;   // Forward-declared; defined in prefab_system.cpp.

    static constexpr int MAX_PREFABS = 64;  // Slot 0 reserved (null handle).

    // Pointers to heap-allocated PrefabData (cold data — file paths, flags,
    // component field values). Null when the slot is unoccupied.
    PrefabData* m_pool[MAX_PREFABS];
    bool        m_occupied[MAX_PREFABS];
    int         m_count;
    std::string m_assetRoot;
};

} // namespace ffe
