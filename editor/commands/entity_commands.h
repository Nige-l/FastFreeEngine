#pragma once

#include "commands/command.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <cstring>

namespace ffe::editor {

// CreateEntityCommand — creates a new entity with a Name component.
// Undo destroys the created entity. Redo re-creates it (new ID).
class CreateEntityCommand : public Command {
public:
    // world: the ECS world to create the entity in.
    // name: display name for the new entity (max 63 chars, null-terminated).
    explicit CreateEntityCommand(World& world, const char* name = "Entity");

    void execute() override;
    void undo() override;
    const char* description() const override;

private:
    World& m_world;
    char m_name[64] = {};
    EntityId m_createdEntity = NULL_ENTITY;
};

// DestroyEntityCommand — destroys an entity, storing its component data for undo.
// Undo re-creates the entity with the same components. Redo re-destroys it.
//
// Stores snapshots of known component types so they can be restored on undo.
// Only components that the editor knows about are preserved. Unknown components
// are lost on undo (acceptable for first milestone).
class DestroyEntityCommand : public Command {
public:
    explicit DestroyEntityCommand(World& world, EntityId entity);

    void execute() override;
    void undo() override;
    const char* description() const override;

private:
    void snapshotComponents();
    void restoreComponents(EntityId entity);

    World& m_world;
    EntityId m_entity = NULL_ENTITY;

    // Component snapshots (present flags + data)
    bool m_hasName = false;
    Name m_name;

    bool m_hasTransform = false;
    Transform m_transform;

    bool m_hasTransform3D = false;
    Transform3D m_transform3D;

    bool m_hasSprite = false;
    Sprite m_sprite;

    bool m_hasMaterial3D = false;
    Material3D m_material3D;

    bool m_hasMesh = false;
    Mesh m_mesh;
};

} // namespace ffe::editor
