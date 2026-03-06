#pragma once

#include "core/types.h"
#include "core/system.h"

#include <entt/entt.hpp>
#include <vector>
#include <algorithm>

namespace ffe {

// ---------------------------------------------------------------------------
// ShutdownSignal — stored in the ECS registry context so any system can
// request engine shutdown without holding a pointer to Application.
//
// Usage inside a system:
//   world.registry().ctx().get<ffe::ShutdownSignal>().requested = true;
//
// Application checks this after every tick and stops the loop if set.
// ---------------------------------------------------------------------------
struct ShutdownSignal {
    bool requested = false;
};

// ---------------------------------------------------------------------------
// HudTextBuffer — stored in the ECS registry context so Lua scripts (or any
// system) can set HUD text without a direct pointer to the editor overlay.
//
// The editor overlay reads this buffer each frame to display on-screen text.
// The buffer is a fixed-size char array (no heap allocation).
// ---------------------------------------------------------------------------
static constexpr u32 HUD_TEXT_BUFFER_SIZE = 256;

struct HudTextBuffer {
    char text[HUD_TEXT_BUFFER_SIZE] = "";
};

// ---------------------------------------------------------------------------
// CameraShake — stored in the ECS registry context. Lua (or any system) sets
// intensity and duration; the render path applies a random offset to the
// camera each frame, decaying over the duration.
// ---------------------------------------------------------------------------
struct CameraShake {
    f32 intensity = 0.0f; // max pixel offset
    f32 duration  = 0.0f; // seconds remaining
    f32 elapsed   = 0.0f; // time since shake started
};

// ---------------------------------------------------------------------------
// ClearColor — stored in the ECS registry context. Controls the background
// color. Lua scripts set this via ffe.setBackgroundColor(r, g, b).
// ---------------------------------------------------------------------------
struct ClearColor {
    f32 r = 0.1f;
    f32 g = 0.1f;
    f32 b = 0.12f;
};

class World {
public:
    World() { m_systems.reserve(32); }

    // --- Entity lifecycle ---
    EntityId createEntity();
    void destroyEntity(EntityId id);
    bool isValid(EntityId id) const;

    // --- Component access (templates — resolved at compile time, zero overhead) ---
    template<typename T, typename... Args>
    T& addComponent(EntityId id, Args&&... args);

    template<typename T>
    void removeComponent(EntityId id);

    template<typename T>
    T& getComponent(EntityId id);

    template<typename T>
    const T& getComponent(EntityId id) const;

    template<typename T>
    bool hasComponent(EntityId id) const;

    // --- Views (iterate entities with specific components) ---
    // Returns an EnTT view. This is intentionally not wrapped further —
    // views are the hot path and any wrapper adds overhead.
    template<typename... Components>
    auto view();

    template<typename... Components>
    auto view() const;

    // --- System management ---
    void registerSystem(const SystemDescriptor& desc);
    void sortSystems(); // Called once after all systems registered
    const std::vector<SystemDescriptor>& systems() const;

    // --- Access to raw registry (escape hatch for renderer/advanced use) ---
    entt::registry& registry();
    const entt::registry& registry() const;

private:
    entt::registry m_registry;
    std::vector<SystemDescriptor> m_systems;
};

// --- Implementation (header-only for template functions) ---

inline EntityId World::createEntity() {
    return static_cast<EntityId>(m_registry.create());
}

inline void World::destroyEntity(const EntityId id) {
    m_registry.destroy(static_cast<entt::entity>(id));
}

inline bool World::isValid(const EntityId id) const {
    return m_registry.valid(static_cast<entt::entity>(id));
}

template<typename T, typename... Args>
T& World::addComponent(const EntityId id, Args&&... args) {
    return m_registry.emplace<T>(
        static_cast<entt::entity>(id),
        std::forward<Args>(args)...
    );
}

template<typename T>
void World::removeComponent(const EntityId id) {
    m_registry.remove<T>(static_cast<entt::entity>(id));
}

template<typename T>
T& World::getComponent(const EntityId id) {
    return m_registry.get<T>(static_cast<entt::entity>(id));
}

template<typename T>
const T& World::getComponent(const EntityId id) const {
    return m_registry.get<T>(static_cast<entt::entity>(id));
}

template<typename T>
bool World::hasComponent(const EntityId id) const {
    return m_registry.all_of<T>(static_cast<entt::entity>(id));
}

template<typename... Components>
auto World::view() {
    return m_registry.view<Components...>();
}

template<typename... Components>
auto World::view() const {
    return m_registry.view<Components...>();
}

inline void World::registerSystem(const SystemDescriptor& desc) {
    m_systems.push_back(desc);
}

inline void World::sortSystems() {
    std::sort(m_systems.begin(), m_systems.end(),
        [](const SystemDescriptor& a, const SystemDescriptor& b) {
            return a.priority < b.priority;
        });
}

inline const std::vector<SystemDescriptor>& World::systems() const {
    return m_systems;
}

inline entt::registry& World::registry() {
    return m_registry;
}

inline const entt::registry& World::registry() const {
    return m_registry;
}

} // namespace ffe
