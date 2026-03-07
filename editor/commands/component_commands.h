#pragma once

#include "commands/command.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

namespace ffe::editor {

// Template command for modifying any ECS component via the undo/redo system.
// Stores old and new values by copy. On execute, writes the new value into the
// component. On undo, writes the old value back. Safe against entity deletion
// (checks validity before writing).
//
// Virtual dispatch is acceptable here — commands execute on user interaction
// (inspector field edit), not per-frame.
template<typename T>
class ModifyComponentCommand : public Command {
public:
    ModifyComponentCommand(World& world, entt::entity entity,
                           const T& oldValue, const T& newValue)
        : m_world(world)
        , m_entity(entity)
        , m_oldValue(oldValue)
        , m_newValue(newValue) {}

    void execute() override {
        if (m_world.registry().valid(m_entity)) {
            m_world.registry().get<T>(m_entity) = m_newValue;
        }
    }

    void undo() override {
        if (m_world.registry().valid(m_entity)) {
            m_world.registry().get<T>(m_entity) = m_oldValue;
        }
    }

    const char* description() const override { return "Modify Component"; }

private:
    World& m_world;
    entt::entity m_entity;
    T m_oldValue;
    T m_newValue;
};

} // namespace ffe::editor
