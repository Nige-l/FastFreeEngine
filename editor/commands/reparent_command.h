#pragma once

#include "commands/command.h"
#include "core/ecs.h"
#include "scene/scene_graph.h"

#include <entt/entt.hpp>

namespace ffe::editor {

// ReparentCommand — moves an entity to a new parent (or to root).
// Stores old and new parent so the operation is fully reversible.
// Uses ffe::scene::setParent / removeParent for the actual hierarchy mutation.
class ReparentCommand : public Command {
public:
    // entity: the entity being reparented.
    // newParent: the new parent entity, or entt::null to make it a root.
    // world: the ECS world containing the hierarchy.
    ReparentCommand(World& world, entt::entity entity, entt::entity newParent)
        : m_world(world)
        , m_entity(entity)
        , m_newParent(newParent)
        , m_oldParent(ffe::scene::getParent(world, entity))
    {}

    void execute() override {
        applyParent(m_newParent);
    }

    void undo() override {
        applyParent(m_oldParent);
    }

    const char* description() const override {
        return "Reparent Entity";
    }

private:
    void applyParent(entt::entity target) {
        if (target == entt::null) {
            ffe::scene::removeParent(m_world, m_entity);
        } else {
            ffe::scene::setParent(m_world, m_entity, target);
        }
    }

    World& m_world;
    entt::entity m_entity = entt::null;
    entt::entity m_newParent = entt::null;
    entt::entity m_oldParent = entt::null;
};

} // namespace ffe::editor
