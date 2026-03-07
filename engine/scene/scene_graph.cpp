#include "scene/scene_graph.h"
#include "renderer/render_system.h"

#include <algorithm>

namespace ffe::scene {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Remove `child` from the parent's Children component.
// If the Children component becomes empty, remove it from the parent entity.
static void removeChildFromParent(ffe::World& world, entt::entity parent, entt::entity child) {
    auto& reg = world.registry();
    if (!reg.all_of<Children>(parent)) {
        return;
    }

    auto& ch = reg.get<Children>(parent);
    for (uint32_t i = 0; i < ch.count; ++i) {
        if (ch.children[i] == child) {
            // Swap with last element and shrink
            ch.children[i] = ch.children[ch.count - 1];
            ch.children[ch.count - 1] = entt::null;
            --ch.count;

            // Clean up empty Children component
            if (ch.count == 0) {
                reg.remove<Children>(parent);
            }
            return;
        }
    }
}

// Add `child` to parent's Children component (creates it if needed).
// Returns false if the Children array is full (32 max).
static bool addChildToParent(ffe::World& world, entt::entity parent, entt::entity child) {
    auto& reg = world.registry();

    if (!reg.all_of<Children>(parent)) {
        auto& ch = reg.emplace<Children>(parent);
        ch.children[0] = child;
        ch.count = 1;
        return true;
    }

    auto& ch = reg.get<Children>(parent);
    if (ch.count >= 32) {
        return false; // Children array full
    }

    ch.children[ch.count] = child;
    ++ch.count;
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool setParent(ffe::World& world, entt::entity child, entt::entity parent) {
    // Self-parenting is not allowed
    if (child == parent) {
        return false;
    }

    // Circular reference check: parent must not be a descendant of child
    if (isAncestor(world, parent, child)) {
        return false;
    }

    auto& reg = world.registry();

    // If child already has a parent, detach from old parent first
    if (reg.all_of<Parent>(child)) {
        const entt::entity oldParent = reg.get<Parent>(child).parent;
        if (oldParent != entt::null) {
            removeChildFromParent(world, oldParent, child);
        }
    }

    // Set/update Parent component on child
    if (reg.all_of<Parent>(child)) {
        reg.get<Parent>(child).parent = parent;
    } else {
        auto& p = reg.emplace<Parent>(child);
        p.parent = parent;
    }

    // Add child to parent's Children list
    if (!addChildToParent(world, parent, child)) {
        // Children array full — rollback
        reg.remove<Parent>(child);
        return false;
    }

    return true;
}

void removeParent(ffe::World& world, entt::entity child) {
    auto& reg = world.registry();

    if (!reg.all_of<Parent>(child)) {
        return; // Already a root — no-op
    }

    const entt::entity oldParent = reg.get<Parent>(child).parent;
    if (oldParent != entt::null) {
        removeChildFromParent(world, oldParent, child);
    }

    reg.remove<Parent>(child);
}

entt::entity getParent(const ffe::World& world, entt::entity entity) {
    const auto& reg = world.registry();
    if (reg.all_of<Parent>(entity)) {
        return reg.get<Parent>(entity).parent;
    }
    return entt::null;
}

const entt::entity* getChildren(const ffe::World& world, entt::entity entity, uint32_t& count) {
    const auto& reg = world.registry();
    if (reg.all_of<Children>(entity)) {
        const auto& ch = reg.get<Children>(entity);
        count = ch.count;
        return ch.children;
    }
    count = 0;
    return nullptr;
}

bool isRoot(const ffe::World& world, entt::entity entity) {
    return !world.registry().all_of<Parent>(entity);
}

bool isAncestor(const ffe::World& world, entt::entity entity, entt::entity ancestor) {
    const auto& reg = world.registry();
    entt::entity current = entity;

    for (uint32_t depth = 0; depth < MAX_HIERARCHY_DEPTH; ++depth) {
        if (!reg.all_of<Parent>(current)) {
            return false; // Reached root without finding ancestor
        }

        const entt::entity p = reg.get<Parent>(current).parent;
        if (p == entt::null) {
            return false;
        }
        if (p == ancestor) {
            return true;
        }
        current = p;
    }

    return false; // Exceeded max depth — treat as not found
}

uint32_t getRootEntities(const ffe::World& world, entt::entity* output, uint32_t maxCount) {
    const auto& reg = world.registry();
    uint32_t written = 0;

    // Iterate all alive entities and collect those without a Parent component.
    const auto* storage = reg.storage<entt::entity>();
    if (storage == nullptr) {
        return 0;
    }

    for (auto [entity] : storage->each()) {
        if (written >= maxCount) {
            break;
        }
        if (reg.valid(entity) && !reg.all_of<Parent>(entity)) {
            output[written] = entity;
            ++written;
        }
    }

    return written;
}

} // namespace ffe::scene
