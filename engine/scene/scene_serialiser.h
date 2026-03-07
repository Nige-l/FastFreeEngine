#pragma once

#include "core/ecs.h"

#include <string>

namespace ffe::scene {

// Maximum number of entities that can be serialised/deserialised.
// Prevents unbounded memory allocation from malicious or corrupt files.
inline constexpr uint32_t MAX_SERIALISED_ENTITIES = 10000;

// Maximum file size (bytes) accepted by loadScene. Prevents loading
// multi-gigabyte files that could exhaust memory during JSON parsing.
inline constexpr size_t MAX_SCENE_FILE_SIZE = 64 * 1024 * 1024; // 64 MB

// Scene file format version. Incremented when the format changes.
inline constexpr int32_t SCENE_FORMAT_VERSION = 1;

// Save all serialisable entities in the world to a JSON file at `path`.
// Returns true on success, false on I/O or serialisation error.
bool saveScene(const ffe::World& world, const std::string& path);

// Load a JSON scene file from `path` into the world.
// Existing entities in the world are NOT cleared — call world.clearAllEntities()
// first if you want a fresh load. Returns true on success.
bool loadScene(ffe::World& world, const std::string& path);

// Serialise all entities in the world to a JSON string.
// Useful for testing and clipboard operations (no file I/O).
std::string serialiseToJson(const ffe::World& world);

// Deserialise entities from a JSON string into the world.
// Returns true on success, false on parse/validation error.
bool deserialiseFromJson(ffe::World& world, const std::string& json);

} // namespace ffe::scene
