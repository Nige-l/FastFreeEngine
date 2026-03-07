// ---------------------------------------------------------------------------
// physics3d_system.cpp — ECS sync system for 3D physics.
//
// Iterates entities with Transform3D + RigidBody3D and writes the Jolt body's
// position and rotation into the ECS Transform3D component each frame.
//
// No heap allocation. No virtual calls. Tight iteration over the view.
// ---------------------------------------------------------------------------

#include "physics/physics3d_system.h"
#include "physics/physics3d.h"
#include "renderer/render_system.h" // Transform3D
#include "core/ecs.h"

#include <entt/entt.hpp>

namespace ffe {

void physics3dSyncSystem(World& world, [[maybe_unused]] const float dt) {
    if (!physics::isPhysics3DInitialized()) { return; }

    auto& reg = world.registry();
    const auto view = reg.view<Transform3D, RigidBody3D>();

    for (const auto entity : view) {
        auto& rb  = view.get<RigidBody3D>(entity);
        if (!rb.initialized || !physics::isValid(rb.handle)) { continue; }

        auto& t3d = view.get<Transform3D>(entity);
        t3d.position = physics::getBodyPosition(rb.handle);
        t3d.rotation = physics::getBodyRotation(rb.handle);
    }
}

} // namespace ffe
