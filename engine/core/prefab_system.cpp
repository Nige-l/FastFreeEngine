// prefab_system.cpp — Data-driven entity template system for FFE.
//
// See prefab_system.h for the public API and usage documentation.
// See docs/architecture/adr-phase10-m1-prefab-system.md for the design rationale
// and the full security threat model.
//
// File ownership: engine/core/ (engine-dev)

#include "core/prefab_system.h"
#include "core/logging.h"
#include "core/platform.h"
#include "renderer/render_system.h"   // Transform3D, Mesh, Material3D
#include "renderer/pbr_material.h"    // PBRMaterial
#include "renderer/mesh_loader.h"     // MeshHandle, MAX_MESH_ASSETS

#include <nlohmann/json.hpp>

#include <climits>     // PATH_MAX
#include <cmath>       // cosf, sinf
#include <cstring>     // strncpy, strncmp, memcpy
#include <cstdio>      // snprintf
#include <fstream>     // std::ifstream
#include <memory>      // std::unique_ptr, std::make_unique
#include <string>      // std::string
#include <vector>      // std::vector (depth-check stack, cold path only)
#include <sys/stat.h>  // stat()

using json = nlohmann::json;

namespace ffe {

// ---------------------------------------------------------------------------
// PrefabData — internal storage for one loaded prefab template.
// Heap-allocated per slot; the pool holds raw owning pointers.
// Cold data: loaded once, read many times at instantiation.
// ---------------------------------------------------------------------------

struct PrefabSystem::PrefabData {
    char name[64] = {};                              // Display name (optional)

    // --- Transform3D template (optional) ---
    bool  hasTransform3D = false;
    float tx = 0.f, ty = 0.f, tz = 0.f;             // position
    float rx = 0.f, ry = 0.f, rz = 0.f;             // rotation (Euler degrees)
    float sx = 1.f, sy = 1.f, sz = 1.f;             // scale

    // --- Mesh template (optional) ---
    bool        hasMesh = false;
    std::string meshPath;                            // heap OK: cold data, loaded once

    // --- Material3D template (optional) ---
    bool  hasMaterial3D = false;
    float matR = 1.f, matG = 1.f, matB = 1.f;
    float matMetallic = 0.f, matRoughness = 0.5f;

    // --- PBRMaterial template (optional) ---
    bool  hasPBRMaterial = false;
    float pbrAlbedoR = 1.f, pbrAlbedoG = 1.f, pbrAlbedoB = 1.f;
    float pbrMetallic = 0.f, pbrRoughness = 0.5f, pbrAo = 1.0f;
};

// ---------------------------------------------------------------------------
// PrefabOverrides::set() — append an override entry to the fixed inline array.
// All three overloads follow the same pattern: bounds-check, strncpy, set type.
// ---------------------------------------------------------------------------

void PrefabOverrides::set(const char* component, const char* field, float v) {
    if (count >= MAX) {
        FFE_LOG_WARN("PrefabSystem", "PrefabOverrides::set(float): overflow — max %d overrides; dropping '%s.%s'",
                     MAX, component, field);
        return;
    }
    PrefabOverride& o = items[count];
    strncpy(o.component, component, 31);
    o.component[31] = '\0';
    strncpy(o.field, field, 31);
    o.field[31] = '\0';
    o.value.f = v;
    o.type = PrefabOverride::Type::Float;
    ++count;
}

void PrefabOverrides::set(const char* component, const char* field, int v) {
    if (count >= MAX) {
        FFE_LOG_WARN("PrefabSystem", "PrefabOverrides::set(int): overflow — max %d overrides; dropping '%s.%s'",
                     MAX, component, field);
        return;
    }
    PrefabOverride& o = items[count];
    strncpy(o.component, component, 31);
    o.component[31] = '\0';
    strncpy(o.field, field, 31);
    o.field[31] = '\0';
    o.value.i = v;
    o.type = PrefabOverride::Type::Int;
    ++count;
}

void PrefabOverrides::set(const char* component, const char* field, bool v) {
    if (count >= MAX) {
        FFE_LOG_WARN("PrefabSystem", "PrefabOverrides::set(bool): overflow — max %d overrides; dropping '%s.%s'",
                     MAX, component, field);
        return;
    }
    PrefabOverride& o = items[count];
    strncpy(o.component, component, 31);
    o.component[31] = '\0';
    strncpy(o.field, field, 31);
    o.field[31] = '\0';
    o.value.b = v;
    o.type = PrefabOverride::Type::Bool;
    ++count;
}

// ---------------------------------------------------------------------------
// PrefabSystem — constructor / destructor
// ---------------------------------------------------------------------------

PrefabSystem::PrefabSystem()
    : m_pool{}
    , m_occupied{}
    , m_count(0)
{
    // Slot 0 is permanently reserved as the null/invalid handle.
    // m_pool[0] and m_occupied[0] remain nullptr/false for the lifetime of this object.
}

PrefabSystem::~PrefabSystem() {
    // Free any remaining heap-allocated PrefabData objects.
    for (int i = 1; i < MAX_PREFABS; ++i) {
        if (m_occupied[i]) {
            delete m_pool[i];
            m_pool[i] = nullptr;
            m_occupied[i] = false;
        }
    }
}

// ---------------------------------------------------------------------------
// setAssetRoot — canonicalize and store the asset root directory.
// loadPrefab() rejects any path that does not start with this root.
// ---------------------------------------------------------------------------

void PrefabSystem::setAssetRoot(std::string_view root) {
    if (root.empty()) {
        m_assetRoot.clear();
        return;
    }

    // Build a null-terminated copy for canonicalizePath.
    char pathBuf[PATH_MAX + 1];
    const std::size_t copyLen = (root.size() < PATH_MAX) ? root.size() : PATH_MAX;
    memcpy(pathBuf, root.data(), copyLen);
    pathBuf[copyLen] = '\0';

    char canon[PATH_MAX + 1];
    if (!canonicalizePath(pathBuf, canon, sizeof(canon))) {
        // Root may not exist yet (e.g. during testing). Store the raw value stripped of
        // trailing slash so that the prefix check is consistent.
        m_assetRoot = std::string(root);
    } else {
        m_assetRoot = std::string(canon);
    }

    // Strip trailing slash for consistent prefix comparison.
    while (!m_assetRoot.empty() && m_assetRoot.back() == '/') {
        m_assetRoot.pop_back();
    }
#ifdef _WIN32
    while (!m_assetRoot.empty() && m_assetRoot.back() == '\\') {
        m_assetRoot.pop_back();
    }
#endif

    FFE_LOG_INFO("PrefabSystem", "asset root set to: %s", m_assetRoot.c_str());
}

// ---------------------------------------------------------------------------
// loadPrefab — security-hardened JSON prefab loader.
//
// Pipeline order (from ADR section 2.9):
//   1. UNC path pre-check (Windows only)
//   2. canonicalizePath()
//   3. asset root prefix check
//   4. stat() file size check (must be <= 1 MB)
//   5. open + read to string
//   6. nlohmann::json parse (max depth 8)
//   7. find free pool slot
//   8. parse "components" with whitelist
//   9. store PrefabData, return handle
// ---------------------------------------------------------------------------

static constexpr std::size_t MAX_PREFAB_FILE_SIZE = 1024u * 1024u;  // 1 MB

PrefabHandle PrefabSystem::loadPrefab(std::string_view path) {
    if (path.empty()) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: path is empty");
        return PrefabHandle{0};
    }

    // Build a null-terminated path string for C APIs.
    char pathBuf[PATH_MAX + 1];
    const std::size_t copyLen = (path.size() < PATH_MAX) ? path.size() : PATH_MAX;
    memcpy(pathBuf, path.data(), copyLen);
    pathBuf[copyLen] = '\0';

    // --- Threat 1a: UNC path pre-check (Windows only) ---
    // _fullpath does not correctly canonicalize UNC paths; reject them first.
#ifdef _WIN32
    if (pathBuf[0] == '\\' && pathBuf[1] == '\\') {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: UNC paths are not allowed: %s", pathBuf);
        return PrefabHandle{0};
    }
#endif

    // --- Threat 1b: canonicalizePath resolves symlinks and '..' components ---
    char canonBuf[PATH_MAX + 1];
    if (!canonicalizePath(pathBuf, canonBuf, sizeof(canonBuf))) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: canonicalizePath failed for: %s", pathBuf);
        return PrefabHandle{0};
    }
    const std::string canonPath(canonBuf);

    // --- Threat 1c: asset root prefix check ---
    // If m_assetRoot is set, canonPath must start with assetRoot + '/'.
    // This prevents /game/assets-evil/foo.json passing a check for /game/assets.
    if (!m_assetRoot.empty()) {
        bool withinRoot = false;
        if (canonPath == m_assetRoot) {
            // Exact match is permitted (loading a file that IS the root directory would
            // fail the stat/parse steps anyway, but we accept it at this stage).
            withinRoot = true;
        } else if (canonPath.size() > m_assetRoot.size()) {
            const char sep = canonPath[m_assetRoot.size()];
            withinRoot = (canonPath.compare(0, m_assetRoot.size(), m_assetRoot) == 0)
                      && (sep == '/'
#ifdef _WIN32
                          || sep == '\\'
#endif
                         );
        }
        if (!withinRoot) {
            FFE_LOG_ERROR("PrefabSystem",
                          "loadPrefab: path traversal rejected — '%s' is outside asset root '%s'",
                          canonBuf, m_assetRoot.c_str());
            return PrefabHandle{0};
        }
    }

    // --- Threat 2: stat file size check — BEFORE opening the file ---
    struct stat st{};
    if (::stat(canonBuf, &st) != 0) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: file does not exist or stat() failed: %s", canonBuf);
        return PrefabHandle{0};
    }
    if (static_cast<std::size_t>(st.st_size) > MAX_PREFAB_FILE_SIZE) {
        FFE_LOG_ERROR("PrefabSystem",
                      "loadPrefab: file too large (%lld bytes, limit %zu): %s",
                      static_cast<long long>(st.st_size), MAX_PREFAB_FILE_SIZE, canonBuf);
        return PrefabHandle{0};
    }

    // --- Step 5: open and read file to string ---
    std::ifstream ifs(canonBuf, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: failed to open file: %s", canonBuf);
        return PrefabHandle{0};
    }
    std::string text;
    text.reserve(static_cast<std::size_t>(st.st_size) + 1u);
    text.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    ifs.close();

    // --- Threat 3: JSON parse with depth limit ---
    json doc = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/false);
    // max_depth not available in this nlohmann version; post-parse depth check below compensates
    if (doc.is_discarded()) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: JSON parse failed: %s", canonBuf);
        return PrefabHandle{0};
    }

    // Manual post-parse nesting depth check (max 8 levels, per ADR section 2.9).
    // This catches deeply nested documents even if the parser did not reject them.
    static constexpr int MAX_JSON_DEPTH = 8;

    // Iterative max-depth calculation using a stack (no heap in hot path, but
    // this is a cold load path so a small heap-allocated stack is acceptable).
    {
        struct Frame { const json* node; int depth; };
        // Pre-reserve enough space — 64 entries covers a 64-leaf tree at depth 8.
        std::vector<Frame> stack;
        stack.reserve(64);
        stack.push_back({&doc, 1});
        bool tooDeep = false;
        while (!stack.empty() && !tooDeep) {
            Frame f = stack.back();
            stack.pop_back();
            if (f.depth > MAX_JSON_DEPTH) {
                tooDeep = true;
                break;
            }
            if (f.node->is_object()) {
                for (const auto& [key, val] : f.node->items()) {
                    if (val.is_object() || val.is_array()) {
                        stack.push_back({&val, f.depth + 1});
                    }
                }
            } else if (f.node->is_array()) {
                for (const auto& elem : *f.node) {
                    if (elem.is_object() || elem.is_array()) {
                        stack.push_back({&elem, f.depth + 1});
                    }
                }
            }
        }
        if (tooDeep) {
            FFE_LOG_ERROR("PrefabSystem",
                          "loadPrefab: JSON nesting depth exceeds %d levels: %s",
                          MAX_JSON_DEPTH, canonBuf);
            return PrefabHandle{0};
        }
    }

    // --- Step 7: validate top-level structure ---
    if (!doc.is_object()) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: JSON root must be an object: %s", canonBuf);
        return PrefabHandle{0};
    }
    if (!doc.contains("components") || !doc["components"].is_object()) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: missing or invalid 'components' key: %s", canonBuf);
        return PrefabHandle{0};
    }

    // --- Step 7: find a free pool slot ---
    int slot = 0;
    for (int i = 1; i < MAX_PREFABS; ++i) {
        if (!m_occupied[i]) {
            slot = i;
            break;
        }
    }
    if (slot == 0) {
        FFE_LOG_ERROR("PrefabSystem", "loadPrefab: prefab pool is full (max %d)", MAX_PREFABS - 1);
        return PrefabHandle{0};
    }

    // --- Step 8: allocate PrefabData and parse components ---
    std::unique_ptr<PrefabData> dataOwner = std::make_unique<PrefabData>();
    PrefabData* data = dataOwner.get();

    // Optional "name" field.
    if (doc.contains("name") && doc["name"].is_string()) {
        const std::string& nameStr = doc["name"].get_ref<const std::string&>();
        strncpy(data->name, nameStr.c_str(), 63);
        data->name[63] = '\0';
    } else {
        snprintf(data->name, sizeof(data->name), "prefab_%d", slot);
    }

    const json& components = doc["components"];

    // --- Threat 4: whitelist — only known component keys are processed ---
    for (const auto& [compKey, compVal] : components.items()) {
        if (!compVal.is_object()) {
            FFE_LOG_WARN("PrefabSystem",
                         "loadPrefab: component '%s' value is not an object — skipping",
                         compKey.c_str());
            continue;
        }

        if (compKey == "Transform3D") {
            data->hasTransform3D = true;
            if (compVal.contains("x")      && compVal["x"].is_number())      data->tx = compVal["x"].get<float>();
            if (compVal.contains("y")      && compVal["y"].is_number())      data->ty = compVal["y"].get<float>();
            if (compVal.contains("z")      && compVal["z"].is_number())      data->tz = compVal["z"].get<float>();
            if (compVal.contains("rotX")   && compVal["rotX"].is_number())   data->rx = compVal["rotX"].get<float>();
            if (compVal.contains("rotY")   && compVal["rotY"].is_number())   data->ry = compVal["rotY"].get<float>();
            if (compVal.contains("rotZ")   && compVal["rotZ"].is_number())   data->rz = compVal["rotZ"].get<float>();
            if (compVal.contains("scaleX") && compVal["scaleX"].is_number()) data->sx = compVal["scaleX"].get<float>();
            if (compVal.contains("scaleY") && compVal["scaleY"].is_number()) data->sy = compVal["scaleY"].get<float>();
            if (compVal.contains("scaleZ") && compVal["scaleZ"].is_number()) data->sz = compVal["scaleZ"].get<float>();
            // Unknown fields inside Transform3D are silently ignored (forward compat).

        } else if (compKey == "Mesh") {
            if (compVal.contains("path") && compVal["path"].is_string()) {
                data->hasMesh = true;
                data->meshPath = compVal["path"].get<std::string>();
            } else {
                FFE_LOG_WARN("PrefabSystem",
                             "loadPrefab: 'Mesh' component missing string 'path' field — skipping Mesh");
            }

        } else if (compKey == "Material3D") {
            data->hasMaterial3D = true;
            if (compVal.contains("r")         && compVal["r"].is_number())         data->matR         = compVal["r"].get<float>();
            if (compVal.contains("g")         && compVal["g"].is_number())         data->matG         = compVal["g"].get<float>();
            if (compVal.contains("b")         && compVal["b"].is_number())         data->matB         = compVal["b"].get<float>();
            if (compVal.contains("metallic")  && compVal["metallic"].is_number())  data->matMetallic  = compVal["metallic"].get<float>();
            if (compVal.contains("roughness") && compVal["roughness"].is_number()) data->matRoughness = compVal["roughness"].get<float>();

        } else if (compKey == "PBRMaterial") {
            data->hasPBRMaterial = true;
            if (compVal.contains("albedoR")   && compVal["albedoR"].is_number())   data->pbrAlbedoR   = compVal["albedoR"].get<float>();
            if (compVal.contains("albedoG")   && compVal["albedoG"].is_number())   data->pbrAlbedoG   = compVal["albedoG"].get<float>();
            if (compVal.contains("albedoB")   && compVal["albedoB"].is_number())   data->pbrAlbedoB   = compVal["albedoB"].get<float>();
            if (compVal.contains("metallic")  && compVal["metallic"].is_number())  data->pbrMetallic  = compVal["metallic"].get<float>();
            if (compVal.contains("roughness") && compVal["roughness"].is_number()) data->pbrRoughness = compVal["roughness"].get<float>();
            if (compVal.contains("ao")        && compVal["ao"].is_number())        data->pbrAo        = compVal["ao"].get<float>();

        } else {
            // Unknown component key — log and skip (forward compatibility).
            FFE_LOG_WARN("PrefabSystem",
                         "loadPrefab: unknown component key '%s' — skipping (forward compat)",
                         compKey.c_str());
        }
    }

    // --- Step 9: store in pool (transfer ownership from unique_ptr to raw pool) ---
    m_pool[slot]     = dataOwner.release();
    m_occupied[slot] = true;
    ++m_count;

    FFE_LOG_INFO("PrefabSystem", "loaded prefab '%s' from %s (slot %d)", data->name, canonBuf, slot);
    return PrefabHandle{static_cast<uint32_t>(slot)};
}

// ---------------------------------------------------------------------------
// unloadPrefab — free a pool slot.
// ---------------------------------------------------------------------------

void PrefabSystem::unloadPrefab(PrefabHandle handle) {
    if (handle.id == 0 || handle.id >= static_cast<uint32_t>(MAX_PREFABS)) {
        return;  // null or out-of-range — no-op
    }
    const int idx = static_cast<int>(handle.id);
    if (!m_occupied[idx]) {
        return;  // already unloaded — no-op
    }
    FFE_LOG_INFO("PrefabSystem", "unloading prefab '%s' (slot %d)", m_pool[idx]->name, idx);
    delete m_pool[idx];
    m_pool[idx]     = nullptr;
    m_occupied[idx] = false;
    --m_count;
}

// ---------------------------------------------------------------------------
// getPrefabCount
// ---------------------------------------------------------------------------

int PrefabSystem::getPrefabCount() const {
    return m_count;
}

// ---------------------------------------------------------------------------
// instantiatePrefab — hot path, no heap allocation.
//
// Creates a new entity and applies the prefab component template.
// Per-field overrides are applied after the template values.
// Returns NULL_ENTITY on any validation failure.
// ---------------------------------------------------------------------------

// Helper: apply a single Transform3D float override by field name.
// Inline to avoid function-call overhead in the tight override loop.
static inline void applyTransform3DFloat(Transform3D& t, const char* field, float v) {
    if      (strncmp(field, "x",      32) == 0) { t.position.x = v; }
    else if (strncmp(field, "y",      32) == 0) { t.position.y = v; }
    else if (strncmp(field, "z",      32) == 0) { t.position.z = v; }
    else if (strncmp(field, "scaleX", 32) == 0) { t.scale.x    = v; }
    else if (strncmp(field, "scaleY", 32) == 0) { t.scale.y    = v; }
    else if (strncmp(field, "scaleZ", 32) == 0) { t.scale.z    = v; }
    else if (strncmp(field, "rotX",   32) == 0) {
        // Euler override: rebuild the full quaternion from overridden + existing angles.
        // Since we store the quaternion we need to decompose, patch, and recompose.
        // For per-axis euler overrides this is the safest approach. It is warm-path
        // enough (called during instantiation, not every frame) to accept the cost.
        // We extract current euler angles from the quaternion, patch the one axis,
        // then recompose.
        //
        // Note: We do not import glm here — use the raw quaternion members directly.
        // The rotation field is a glm::quat. We recompose from stored euler angles
        // tracked inside the Transform3D. Since Transform3D does NOT store euler angles
        // separately (it stores only the quaternion), we reconstruct the quaternion from
        // the full override set.
        //
        // Design decision (flagged for ADR deviation): rotX/Y/Z overrides on Transform3D
        // rebuild the full quaternion using the override value for this axis and identity
        // (0) for the other axes. This means a single-axis override replaces ALL rotation,
        // not just that axis. The ADR does not specify the composition behaviour for partial
        // euler overrides. The safe, correct approach is: if the game needs rotation overrides
        // it should override all three axes or use setTransform3D after instantiation.
        // This limitation is logged at WARN level.
        FFE_LOG_WARN("PrefabSystem",
                     "instantiatePrefab: rotX/rotY/rotZ overrides set only one euler axis; "
                     "full quaternion rebuild uses identity (0) for unspecified axes");
        const float degToRad = 3.14159265358979323846f / 180.0f;
        // Rebuild quaternion with only rotX set; rotY=rotZ=0.
        const float halfX = v * degToRad * 0.5f;
        t.rotation = glm::quat(
            cosf(halfX),        // w
            sinf(halfX),        // x
            0.0f,               // y
            0.0f                // z
        );
    }
    else if (strncmp(field, "rotY",   32) == 0) {
        const float degToRad = 3.14159265358979323846f / 180.0f;
        const float halfY = v * degToRad * 0.5f;
        t.rotation = glm::quat(cosf(halfY), 0.0f, sinf(halfY), 0.0f);
    }
    else if (strncmp(field, "rotZ",   32) == 0) {
        const float degToRad = 3.14159265358979323846f / 180.0f;
        const float halfZ = v * degToRad * 0.5f;
        t.rotation = glm::quat(cosf(halfZ), 0.0f, 0.0f, sinf(halfZ));
    }
    else {
        FFE_LOG_WARN("PrefabSystem",
                     "instantiatePrefab: unknown Transform3D float field '%s' — override ignored", field);
    }
}

static inline void applyMaterial3DFloat(Material3D& m, const char* field, float v) {
    if      (strncmp(field, "r",         32) == 0) { m.diffuseColor.r = v; }
    else if (strncmp(field, "g",         32) == 0) { m.diffuseColor.g = v; }
    else if (strncmp(field, "b",         32) == 0) { m.diffuseColor.b = v; }
    else if (strncmp(field, "metallic",  32) == 0) { /* no metallic in Material3D */ }
    else if (strncmp(field, "roughness", 32) == 0) { /* no roughness in Material3D */ }
    else {
        FFE_LOG_WARN("PrefabSystem",
                     "instantiatePrefab: unknown Material3D float field '%s' — override ignored", field);
    }
}

static inline void applyPBRMaterialFloat(renderer::PBRMaterial& p, const char* field, float v) {
    if      (strncmp(field, "albedoR",   32) == 0) { p.albedo.r   = v; }
    else if (strncmp(field, "albedoG",   32) == 0) { p.albedo.g   = v; }
    else if (strncmp(field, "albedoB",   32) == 0) { p.albedo.b   = v; }
    else if (strncmp(field, "metallic",  32) == 0) { p.metallic   = v; }
    else if (strncmp(field, "roughness", 32) == 0) { p.roughness  = v; }
    else if (strncmp(field, "ao",        32) == 0) { p.ao         = v; }
    else {
        FFE_LOG_WARN("PrefabSystem",
                     "instantiatePrefab: unknown PBRMaterial float field '%s' — override ignored", field);
    }
}

EntityId PrefabSystem::instantiatePrefab(World& world, PrefabHandle handle,
                                         const PrefabOverrides& overrides) {
    // --- Threat 6: handle validation ---
    if (handle.id == 0 || handle.id >= static_cast<uint32_t>(MAX_PREFABS) || !m_occupied[handle.id]) {
        FFE_LOG_ERROR("PrefabSystem", "instantiatePrefab: invalid handle id %u", handle.id);
        return NULL_ENTITY;
    }

    const PrefabData& data = *m_pool[handle.id];

    // Create a new entity (EnTT O(1), no heap in warm path).
    const EntityId eid = world.createEntity();

    // --- Apply Transform3D template ---
    Transform3D* t3dPtr = nullptr;
    if (data.hasTransform3D) {
        Transform3D t3d;
        t3d.position = {data.tx, data.ty, data.tz};
        t3d.scale    = {data.sx, data.sy, data.sz};
        // Compose quaternion from Euler angles (degrees → radians, XYZ order).
        const float degToRad = 3.14159265358979323846f / 180.0f;
        t3d.rotation = glm::quat(glm::vec3(
            data.rx * degToRad,
            data.ry * degToRad,
            data.rz * degToRad
        ));
        t3dPtr = &world.addComponent<Transform3D>(eid, t3d);
    }

    // --- Apply Mesh template ---
    if (data.hasMesh) {
        Mesh meshComp;
        // Resolve path to handle via linear scan of the mesh pool.
        // Per ADR section 2.7: O(MAX_MESH_ASSETS) lookup; no disk I/O.
        // The game is responsible for pre-loading mesh assets before instantiating.
        renderer::MeshHandle meshHandle{0};
        // Because there is no path->handle lookup in the current mesh_loader API,
        // we set meshHandle to 0 (invalid) and log a warning.
        // The Mesh component is still added so overrides can target it in future.
        if (!renderer::isValid(meshHandle)) {
            FFE_LOG_WARN("PrefabSystem",
                         "instantiatePrefab: Mesh component path '%s' — no findHandleByPath() "
                         "in mesh_loader; Mesh component added with invalid handle. "
                         "Load the mesh with ffe.loadMesh() and use the returned handle directly.",
                         data.meshPath.c_str());
        }
        meshComp.meshHandle = meshHandle;
        world.addComponent<Mesh>(eid, meshComp);
    }

    // --- Apply Material3D template ---
    Material3D* matPtr = nullptr;
    if (data.hasMaterial3D) {
        Material3D mat;
        mat.diffuseColor = {data.matR, data.matG, data.matB, 1.0f};
        matPtr = &world.addComponent<Material3D>(eid, mat);
    }

    // --- Apply PBRMaterial template ---
    renderer::PBRMaterial* pbrPtr = nullptr;
    if (data.hasPBRMaterial) {
        renderer::PBRMaterial pbr;
        pbr.albedo    = {data.pbrAlbedoR, data.pbrAlbedoG, data.pbrAlbedoB, 1.0f};
        pbr.metallic  = data.pbrMetallic;
        pbr.roughness = data.pbrRoughness;
        pbr.ao        = data.pbrAo;
        pbrPtr = &world.addComponent<renderer::PBRMaterial>(eid, pbr);
    }

    // --- Apply overrides (flat if/else chain — no heap, no virtual dispatch) ---
    for (int i = 0; i < overrides.count; ++i) {
        const PrefabOverride& ov = overrides.items[i];

        if (strncmp(ov.component, "Transform3D", 32) == 0) {
            if (t3dPtr == nullptr) {
                FFE_LOG_WARN("PrefabSystem",
                             "instantiatePrefab: override targets 'Transform3D' but prefab has no Transform3D — ignored");
                continue;
            }
            if (ov.type == PrefabOverride::Type::Float) {
                applyTransform3DFloat(*t3dPtr, ov.field, ov.value.f);
            }

        } else if (strncmp(ov.component, "Material3D", 32) == 0) {
            if (matPtr == nullptr) {
                FFE_LOG_WARN("PrefabSystem",
                             "instantiatePrefab: override targets 'Material3D' but prefab has no Material3D — ignored");
                continue;
            }
            if (ov.type == PrefabOverride::Type::Float) {
                applyMaterial3DFloat(*matPtr, ov.field, ov.value.f);
            }

        } else if (strncmp(ov.component, "PBRMaterial", 32) == 0) {
            if (pbrPtr == nullptr) {
                FFE_LOG_WARN("PrefabSystem",
                             "instantiatePrefab: override targets 'PBRMaterial' but prefab has no PBRMaterial — ignored");
                continue;
            }
            if (ov.type == PrefabOverride::Type::Float) {
                applyPBRMaterialFloat(*pbrPtr, ov.field, ov.value.f);
            }

        } else {
            FFE_LOG_WARN("PrefabSystem",
                         "instantiatePrefab: override targets unknown component '%s' — ignored",
                         ov.component);
        }
    }

    return eid;
}

} // namespace ffe
