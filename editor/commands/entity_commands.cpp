#include "commands/entity_commands.h"

#include <cstring>

namespace ffe::editor {

// ---------------------------------------------------------------------------
// CreateEntityCommand
// ---------------------------------------------------------------------------

CreateEntityCommand::CreateEntityCommand(World& world, const char* name)
    : m_world(world)
{
    if (name) {
        std::strncpy(m_name, name, sizeof(m_name) - 1);
        m_name[sizeof(m_name) - 1] = '\0';
    }
}

void CreateEntityCommand::execute() {
    const EntityId id = m_world.createEntity();
    auto& nameComp = m_world.addComponent<Name>(id);
    std::strncpy(nameComp.name, m_name, sizeof(nameComp.name) - 1);
    nameComp.name[sizeof(nameComp.name) - 1] = '\0';
    m_createdEntity = id;
}

void CreateEntityCommand::undo() {
    if (m_createdEntity != NULL_ENTITY && m_world.isValid(m_createdEntity)) {
        m_world.destroyEntity(m_createdEntity);
    }
    m_createdEntity = NULL_ENTITY;
}

const char* CreateEntityCommand::description() const {
    return "Create Entity";
}

// ---------------------------------------------------------------------------
// DestroyEntityCommand
// ---------------------------------------------------------------------------

DestroyEntityCommand::DestroyEntityCommand(World& world, const EntityId entity)
    : m_world(world)
    , m_entity(entity)
{
}

void DestroyEntityCommand::execute() {
    if (m_entity == NULL_ENTITY || !m_world.isValid(m_entity)) return;

    // Snapshot all known components before destruction
    snapshotComponents();

    m_world.destroyEntity(m_entity);
}

void DestroyEntityCommand::undo() {
    // Re-create the entity (gets a new ID — EnTT may recycle)
    const EntityId newId = m_world.createEntity();
    restoreComponents(newId);
    m_entity = newId;
}

const char* DestroyEntityCommand::description() const {
    return "Delete Entity";
}

void DestroyEntityCommand::snapshotComponents() {
    m_hasName = m_world.hasComponent<Name>(m_entity);
    if (m_hasName) {
        m_name = m_world.getComponent<Name>(m_entity);
    }

    m_hasTransform = m_world.hasComponent<Transform>(m_entity);
    if (m_hasTransform) {
        m_transform = m_world.getComponent<Transform>(m_entity);
    }

    m_hasTransform3D = m_world.hasComponent<Transform3D>(m_entity);
    if (m_hasTransform3D) {
        m_transform3D = m_world.getComponent<Transform3D>(m_entity);
    }

    m_hasSprite = m_world.hasComponent<Sprite>(m_entity);
    if (m_hasSprite) {
        m_sprite = m_world.getComponent<Sprite>(m_entity);
    }

    m_hasMaterial3D = m_world.hasComponent<Material3D>(m_entity);
    if (m_hasMaterial3D) {
        m_material3D = m_world.getComponent<Material3D>(m_entity);
    }

    m_hasMesh = m_world.hasComponent<Mesh>(m_entity);
    if (m_hasMesh) {
        m_mesh = m_world.getComponent<Mesh>(m_entity);
    }
}

void DestroyEntityCommand::restoreComponents(const EntityId entity) {
    if (m_hasName) {
        m_world.addComponent<Name>(entity, m_name);
    }
    if (m_hasTransform) {
        m_world.addComponent<Transform>(entity, m_transform);
    }
    if (m_hasTransform3D) {
        m_world.addComponent<Transform3D>(entity, m_transform3D);
    }
    if (m_hasSprite) {
        m_world.addComponent<Sprite>(entity, m_sprite);
    }
    if (m_hasMaterial3D) {
        m_world.addComponent<Material3D>(entity, m_material3D);
    }
    if (m_hasMesh) {
        m_world.addComponent<Mesh>(entity, m_mesh);
    }
}

} // namespace ffe::editor
