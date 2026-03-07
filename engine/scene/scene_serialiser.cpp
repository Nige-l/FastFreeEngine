#include "scene/scene_serialiser.h"
#include "core/logging.h"
#include "renderer/render_system.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace ffe::scene {

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

// Validate that all floats in a JSON object's named fields are finite.
// Returns false and logs on the first non-finite value found.
static bool validateFloatFields(const json& obj, const char* componentName,
                                 const std::initializer_list<const char*>& fields) {
    for (const char* field : fields) {
        if (obj.contains(field)) {
            const auto& val = obj[field];
            if (val.is_number()) {
                const double d = val.get<double>();
                if (!std::isfinite(d)) {
                    FFE_LOG_ERROR("Scene", "Non-finite float in %s.%s", componentName, field);
                    return false;
                }
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Component serialisation
// ---------------------------------------------------------------------------

static json serialiseTransform(const Transform& t) {
    json j;
    j["x"]       = t.position.x;
    j["y"]       = t.position.y;
    j["z"]       = t.position.z;
    j["rotation"]= t.rotation;
    j["scaleX"]  = t.scale.x;
    j["scaleY"]  = t.scale.y;
    return j;
}

static bool deserialiseTransform(const json& j, Transform& t) {
    if (!validateFloatFields(j, "Transform", {"x", "y", "z", "rotation", "scaleX", "scaleY"})) {
        return false;
    }
    if (j.contains("x"))        t.position.x = j["x"].get<float>();
    if (j.contains("y"))        t.position.y = j["y"].get<float>();
    if (j.contains("z"))        t.position.z = j["z"].get<float>();
    if (j.contains("rotation")) t.rotation   = j["rotation"].get<float>();
    if (j.contains("scaleX"))   t.scale.x    = j["scaleX"].get<float>();
    if (j.contains("scaleY"))   t.scale.y    = j["scaleY"].get<float>();
    return true;
}

static json serialiseTransform3D(const Transform3D& t) {
    json j;
    j["px"] = t.position.x;
    j["py"] = t.position.y;
    j["pz"] = t.position.z;
    j["rw"] = t.rotation.w;
    j["rx"] = t.rotation.x;
    j["ry"] = t.rotation.y;
    j["rz"] = t.rotation.z;
    j["sx"] = t.scale.x;
    j["sy"] = t.scale.y;
    j["sz"] = t.scale.z;
    return j;
}

static bool deserialiseTransform3D(const json& j, Transform3D& t) {
    if (!validateFloatFields(j, "Transform3D", {"px","py","pz","rw","rx","ry","rz","sx","sy","sz"})) {
        return false;
    }
    if (j.contains("px")) t.position.x = j["px"].get<float>();
    if (j.contains("py")) t.position.y = j["py"].get<float>();
    if (j.contains("pz")) t.position.z = j["pz"].get<float>();
    if (j.contains("rw")) t.rotation.w = j["rw"].get<float>();
    if (j.contains("rx")) t.rotation.x = j["rx"].get<float>();
    if (j.contains("ry")) t.rotation.y = j["ry"].get<float>();
    if (j.contains("rz")) t.rotation.z = j["rz"].get<float>();
    if (j.contains("sx")) t.scale.x    = j["sx"].get<float>();
    if (j.contains("sy")) t.scale.y    = j["sy"].get<float>();
    if (j.contains("sz")) t.scale.z    = j["sz"].get<float>();
    return true;
}

static json serialiseName(const Name& n) {
    json j;
    j["name"] = std::string(n.name);
    return j;
}

static bool deserialiseName(const json& j, Name& n) {
    if (j.contains("name") && j["name"].is_string()) {
        const std::string& s = j["name"].get_ref<const std::string&>();
        // Truncate to fit the fixed-size buffer (63 chars + null)
        const size_t len = s.size() < 63 ? s.size() : 63;
        std::memcpy(n.name, s.c_str(), len);
        n.name[len] = '\0';
    }
    return true;
}

// Parent is serialised as an index into the entity array (not raw entt::entity).
// The caller maps entities to array indices before serialisation.
static json serialiseParent(const uint32_t parentIndex) {
    json j;
    j["parent"] = parentIndex;
    return j;
}

// ---------------------------------------------------------------------------
// Core serialise/deserialise
// ---------------------------------------------------------------------------

// Serialise all entities to a JSON object.
static json serialiseWorld(const ffe::World& world) {
    json root;
    root["version"] = SCENE_FORMAT_VERSION;
    root["entities"] = json::array();

    // Collect all entities so we can build an index map for Parent references.
    const auto& reg = world.registry();

    // Build a list of all valid entities and a map from entt::entity to array index.
    // We iterate the registry's entity storage to get all alive entities.
    std::vector<entt::entity> entities;
    entities.reserve(512);
    for (auto [entity] : reg.storage<entt::entity>()->each()) {
        if (reg.valid(entity)) {
            entities.push_back(entity);
        }
    }

    // entity -> array index map (for Parent serialisation)
    std::unordered_map<uint32_t, uint32_t> entityToIndex;
    entityToIndex.reserve(entities.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(entities.size()); ++i) {
        entityToIndex[static_cast<uint32_t>(entities[i])] = i;
    }

    for (const auto entity : entities) {
        json entityJson;
        json components;

        // Name
        if (reg.all_of<Name>(entity)) {
            components["Name"] = serialiseName(reg.get<Name>(entity));
        }

        // Transform (2D)
        if (reg.all_of<Transform>(entity)) {
            components["Transform"] = serialiseTransform(reg.get<Transform>(entity));
        }

        // Transform3D
        if (reg.all_of<Transform3D>(entity)) {
            components["Transform3D"] = serialiseTransform3D(reg.get<Transform3D>(entity));
        }

        // Parent
        if (reg.all_of<Parent>(entity)) {
            const auto& p = reg.get<Parent>(entity);
            if (p.parent != entt::null) {
                const auto it = entityToIndex.find(static_cast<uint32_t>(p.parent));
                if (it != entityToIndex.end()) {
                    components["Parent"] = serialiseParent(it->second);
                }
            }
        }

        entityJson["components"] = components;

        // Use Name for the top-level "name" field if present
        if (reg.all_of<Name>(entity)) {
            entityJson["name"] = std::string(reg.get<Name>(entity).name);
        }

        root["entities"].push_back(entityJson);
    }

    return root;
}

// Deserialise entities from a JSON object into the world.
// Returns true on success.
static bool deserialiseWorld(ffe::World& world, const json& root) {
    // Validate version
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        FFE_LOG_ERROR("Scene", "Missing or invalid 'version' field");
        return false;
    }
    const int32_t version = root["version"].get<int32_t>();
    if (version != SCENE_FORMAT_VERSION) {
        FFE_LOG_ERROR("Scene", "Unsupported scene version %d (expected %d)",
                      version, SCENE_FORMAT_VERSION);
        return false;
    }

    if (!root.contains("entities") || !root["entities"].is_array()) {
        FFE_LOG_ERROR("Scene", "Missing or invalid 'entities' array");
        return false;
    }

    const auto& entities = root["entities"];
    if (entities.size() > MAX_SERIALISED_ENTITIES) {
        FFE_LOG_ERROR("Scene", "Entity count %zu exceeds maximum %u",
                      entities.size(), MAX_SERIALISED_ENTITIES);
        return false;
    }

    // First pass: create all entities and attach components (except Parent).
    // We need all entities to exist before resolving Parent index references.
    std::vector<EntityId> createdEntities;
    createdEntities.reserve(entities.size());

    for (const auto& entityJson : entities) {
        const EntityId eid = world.createEntity();
        createdEntities.push_back(eid);

        if (!entityJson.contains("components") || !entityJson["components"].is_object()) {
            continue; // Entity with no components is valid
        }
        const auto& comps = entityJson["components"];

        // Name
        if (comps.contains("Name") && comps["Name"].is_object()) {
            auto& name = world.addComponent<Name>(eid);
            if (!deserialiseName(comps["Name"], name)) {
                return false;
            }
        }

        // Transform
        if (comps.contains("Transform") && comps["Transform"].is_object()) {
            auto& t = world.addComponent<Transform>(eid);
            if (!deserialiseTransform(comps["Transform"], t)) {
                return false;
            }
        }

        // Transform3D
        if (comps.contains("Transform3D") && comps["Transform3D"].is_object()) {
            auto& t = world.addComponent<Transform3D>(eid);
            if (!deserialiseTransform3D(comps["Transform3D"], t)) {
                return false;
            }
        }
    }

    // Second pass: resolve Parent references and build Children lists.
    for (size_t i = 0; i < entities.size(); ++i) {
        const auto& entityJson = entities[i];
        if (!entityJson.contains("components") || !entityJson["components"].is_object()) {
            continue;
        }
        const auto& comps = entityJson["components"];

        if (comps.contains("Parent") && comps["Parent"].is_object()) {
            const auto& parentJson = comps["Parent"];
            if (parentJson.contains("parent") && parentJson["parent"].is_number_unsigned()) {
                const uint32_t parentIndex = parentJson["parent"].get<uint32_t>();
                if (parentIndex < createdEntities.size()) {
                    const EntityId parentEid = createdEntities[parentIndex];
                    const EntityId childEid  = createdEntities[i];

                    // Set Parent component on the child
                    auto& p = world.addComponent<Parent>(childEid);
                    p.parent = static_cast<entt::entity>(parentEid);

                    // Add child to parent's Children component
                    if (!world.hasComponent<Children>(parentEid)) {
                        world.addComponent<Children>(parentEid);
                    }
                    auto& ch = world.getComponent<Children>(parentEid);
                    if (ch.count < 32) {
                        ch.children[ch.count] = static_cast<entt::entity>(childEid);
                        ++ch.count;
                    } else {
                        FFE_LOG_WARN("Scene", "Parent entity %u has more than 32 children — excess ignored",
                                     parentEid);
                    }
                } else {
                    FFE_LOG_WARN("Scene", "Parent index %u out of range (entity count: %zu)",
                                 parentIndex, createdEntities.size());
                }
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string serialiseToJson(const ffe::World& world) {
    const json root = serialiseWorld(world);
    return root.dump(2); // Pretty-print with 2-space indent
}

bool deserialiseFromJson(ffe::World& world, const std::string& jsonStr) {
    // Reject obviously oversized input
    if (jsonStr.size() > MAX_SCENE_FILE_SIZE) {
        FFE_LOG_ERROR("Scene", "JSON string size %zu exceeds maximum %zu",
                      jsonStr.size(), MAX_SCENE_FILE_SIZE);
        return false;
    }

    // parse with exceptions disabled (third arg = false). Returns a
    // "discarded" value on parse error instead of throwing.
    const json root = json::parse(jsonStr, nullptr, false);

    if (root.is_discarded()) {
        FFE_LOG_ERROR("Scene", "Failed to parse JSON");
        return false;
    }

    return deserialiseWorld(world, root);
}

bool saveScene(const ffe::World& world, const std::string& path) {
    if (path.empty()) {
        FFE_LOG_ERROR("Scene", "saveScene: empty path");
        return false;
    }

    // Basic path safety: reject paths with '..' traversal
    if (path.find("..") != std::string::npos) {
        FFE_LOG_ERROR("Scene", "saveScene: path traversal rejected: %s", path.c_str());
        return false;
    }

    const std::string jsonStr = serialiseToJson(world);

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        FFE_LOG_ERROR("Scene", "saveScene: failed to open file: %s", path.c_str());
        return false;
    }

    file << jsonStr;
    if (file.fail()) {
        FFE_LOG_ERROR("Scene", "saveScene: write failed: %s", path.c_str());
        return false;
    }

    FFE_LOG_INFO("Scene", "Saved scene to %s", path.c_str());
    return true;
}

bool loadScene(ffe::World& world, const std::string& path) {
    if (path.empty()) {
        FFE_LOG_ERROR("Scene", "loadScene: empty path");
        return false;
    }

    // Basic path safety: reject paths with '..' traversal
    if (path.find("..") != std::string::npos) {
        FFE_LOG_ERROR("Scene", "loadScene: path traversal rejected: %s", path.c_str());
        return false;
    }

    std::ifstream file(path, std::ios::in | std::ios::ate);
    if (!file.is_open()) {
        FFE_LOG_ERROR("Scene", "loadScene: failed to open file: %s", path.c_str());
        return false;
    }

    // Check file size before reading
    const auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize > MAX_SCENE_FILE_SIZE) {
        FFE_LOG_ERROR("Scene", "loadScene: file size %zu exceeds maximum %zu",
                      fileSize, MAX_SCENE_FILE_SIZE);
        return false;
    }

    file.seekg(0, std::ios::beg);
    std::string content(fileSize, '\0');
    file.read(content.data(), static_cast<std::streamsize>(fileSize));
    if (file.fail()) {
        FFE_LOG_ERROR("Scene", "loadScene: read failed: %s", path.c_str());
        return false;
    }

    return deserialiseFromJson(world, content);
}

} // namespace ffe::scene
