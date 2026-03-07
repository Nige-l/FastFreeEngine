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

// Template command for adding a component to an entity via the undo/redo system.
// Execute adds the component with default construction. Undo removes it.
// Safe against entity deletion (checks validity before writing).
template<typename T>
class AddComponentCommand : public Command {
public:
    AddComponentCommand(World& world, entt::entity entity)
        : m_world(world)
        , m_entity(entity) {}

    void execute() override {
        if (m_world.registry().valid(m_entity) &&
            !m_world.registry().all_of<T>(m_entity)) {
            m_world.registry().emplace<T>(m_entity);
        }
    }

    void undo() override {
        if (m_world.registry().valid(m_entity) &&
            m_world.registry().all_of<T>(m_entity)) {
            m_world.registry().remove<T>(m_entity);
        }
    }

    const char* description() const override { return "Add Component"; }

private:
    World& m_world;
    entt::entity m_entity;
};

// Template command for removing a component from an entity via the undo/redo system.
// Execute snapshots the component value and removes it. Undo restores the component
// with the snapshotted value.
// Safe against entity deletion (checks validity before writing).
template<typename T>
class RemoveComponentCommand : public Command {
public:
    RemoveComponentCommand(World& world, entt::entity entity)
        : m_world(world)
        , m_entity(entity)
        , m_snapshot{} {
        // Snapshot the component at construction time so undo can restore it
        if (m_world.registry().valid(m_entity) &&
            m_world.registry().all_of<T>(m_entity)) {
            m_snapshot = m_world.registry().get<T>(m_entity);
            m_hadComponent = true;
        }
    }

    void execute() override {
        if (m_world.registry().valid(m_entity) &&
            m_world.registry().all_of<T>(m_entity)) {
            // Re-snapshot in case value changed since construction
            m_snapshot = m_world.registry().get<T>(m_entity);
            m_hadComponent = true;
            m_world.registry().remove<T>(m_entity);
        }
    }

    void undo() override {
        if (m_hadComponent &&
            m_world.registry().valid(m_entity) &&
            !m_world.registry().all_of<T>(m_entity)) {
            m_world.registry().emplace<T>(m_entity, m_snapshot);
        }
    }

    const char* description() const override { return "Remove Component"; }

private:
    World& m_world;
    entt::entity m_entity;
    T m_snapshot;
    bool m_hadComponent = false;
};

} // namespace ffe::editor
