#pragma once

#include "core/ecs.h"

#include <entt/entt.hpp>

namespace ffe::scene {

// Maximum depth for ancestor traversal. Prevents infinite loops if data
// is somehow corrupted. 64 levels is far more than any sane scene graph.
inline constexpr uint32_t MAX_HIERARCHY_DEPTH = 64;

// Set parent-child relationship. Removes any existing parent first.
// Returns false if the operation would create a circular reference
// (i.e., parent is a descendant of child) or if child == parent.
bool setParent(ffe::World& world, entt::entity child, entt::entity parent);

// Remove parent from entity (makes it a root entity).
// No-op if the entity has no parent.
void removeParent(ffe::World& world, entt::entity child);

// Get parent entity (returns entt::null if root).
entt::entity getParent(const ffe::World& world, entt::entity entity);

// Get children of an entity. Returns pointer to the children array and
// sets count to the number of children. Returns nullptr and count=0 if
// the entity has no Children component.
const entt::entity* getChildren(const ffe::World& world, entt::entity entity, uint32_t& count);

// Check if entity is a root (has no Parent component).
bool isRoot(const ffe::World& world, entt::entity entity);

// Check if `ancestor` is an ancestor of `entity` (walks up the parent chain).
// Returns false if entity == ancestor (an entity is not its own ancestor).
bool isAncestor(const ffe::World& world, entt::entity entity, entt::entity ancestor);

// Get all root entities (entities with no Parent component).
// Writes entity handles to `output`, up to `maxCount`. Returns the number written.
uint32_t getRootEntities(const ffe::World& world, entt::entity* output, uint32_t maxCount);

} // namespace ffe::scene
