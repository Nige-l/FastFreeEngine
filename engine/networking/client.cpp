// engine/networking/client.cpp
//
// High-level game client: transport polling, snapshot reception,
// and application to the local World.

#include "networking/client.h"
#include "networking/packet.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <cstring>

namespace ffe::networking {

// ===========================================================================
// Transport-level static callbacks
// ===========================================================================

void NetworkClient::onTransportConnect(void* userData) {
    auto* self = static_cast<NetworkClient*>(userData);
    self->m_connected = true;
    if (self->m_connectedCb) {
        self->m_connectedCb(self->m_connectedData);
    }
}

void NetworkClient::onTransportDisconnect(void* userData) {
    auto* self = static_cast<NetworkClient*>(userData);
    self->m_connected = false;
    if (self->m_disconnectedCb) {
        self->m_disconnectedCb(self->m_disconnectedData);
    }
}

void NetworkClient::onTransportReceive(const ReceivedPacket& pkt, void* userData) {
    auto* self = static_cast<NetworkClient*>(userData);

    // Check minimum size for a packet header
    if (pkt.dataLength < HEADER_SIZE) {
        // Not a user message and too small for a snapshot -- forward as message
        if (self->m_messageCb) {
            self->m_messageCb(pkt.data, pkt.dataLength, self->m_messageData);
        }
        return;
    }

    // Try to parse the packet header
    PacketReader reader(pkt.data, pkt.dataLength);
    PacketHeader header;
    if (!readHeader(reader, header)) {
        // Malformed header -- treat as raw user message
        if (self->m_messageCb) {
            self->m_messageCb(pkt.data, pkt.dataLength, self->m_messageData);
        }
        return;
    }

    if (header.type == PacketType::SNAPSHOT_FULL) {
        // Parse snapshot: tick(u32) + entityCount(u32) + entity data
        uint32_t snapshotTick  = 0;
        uint32_t entityCount   = 0;
        if (!reader.readU32(snapshotTick) || !reader.readU32(entityCount)) {
            return; // malformed
        }

        Snapshot snapshot;
        snapshot.tick        = snapshotTick;
        snapshot.entityCount = (entityCount > MAX_SNAPSHOT_ENTITIES)
                                   ? MAX_SNAPSHOT_ENTITIES
                                   : entityCount;

        for (uint32_t e = 0; e < snapshot.entityCount; ++e) {
            EntitySnapshot& es = snapshot.entities[e];

            uint32_t entityId = 0;
            uint16_t mask     = 0;
            if (!reader.readU32(entityId) || !reader.readU16(mask)) {
                snapshot.entityCount = e; // truncate
                break;
            }
            es.entityId      = entityId;
            es.componentMask = mask;
            es.dataSize      = 0;

            // Read component data blocks until we run out of reader data
            // or hit the entity data limit
            while (reader.remaining() >= 4) {
                uint16_t compId   = 0;
                uint16_t compSize = 0;
                if (!reader.readU16(compId) || !reader.readU16(compSize)) {
                    break;
                }
                if (compSize == 0) { break; }

                // Check if this data fits in the entity snapshot buffer
                if (es.dataSize + 4 + compSize > MAX_ENTITY_COMPONENT_DATA) {
                    // Skip this component's data
                    uint8_t discard[MAX_ENTITY_COMPONENT_DATA];
                    reader.readBytes(discard, compSize);
                    break;
                }

                // Store componentId and size in the entity data, followed by raw bytes
                std::memcpy(es.componentData + es.dataSize, &compId, 2);
                es.dataSize += 2;
                std::memcpy(es.componentData + es.dataSize, &compSize, 2);
                es.dataSize += 2;
                if (!reader.readBytes(es.componentData + es.dataSize, compSize)) {
                    break;
                }
                es.dataSize += compSize;
            }
        }

        // Reset interpolation timer on new snapshot
        self->m_timeSinceSnapshot = 0.0f;
        self->m_snapshots.push(snapshot);
    } else if (header.type == PacketType::CONNECT_ACCEPT) {
        // Server assigned us a client ID
        uint32_t assignedId = 0;
        if (reader.readU32(assignedId)) {
            self->m_clientId = assignedId;
        }
    } else {
        // Unknown or user-defined packet type -- forward as raw message
        if (self->m_messageCb) {
            // Forward the payload (after header)
            const uint16_t offset = HEADER_SIZE;
            if (pkt.dataLength > offset) {
                self->m_messageCb(pkt.data + offset,
                                  pkt.dataLength - offset,
                                  self->m_messageData);
            }
        }
    }
}

// ===========================================================================
// Public API
// ===========================================================================

bool NetworkClient::connect(const char* host, const uint16_t port) {
    if (m_connected) { return false; }

    m_transport.setConnectCallback(onTransportConnect, this);
    m_transport.setDisconnectCallback(onTransportDisconnect, this);
    m_transport.setReceiveCallback(onTransportReceive, this);

    if (!m_transport.connect(host, port)) {
        return false;
    }

    return true;
}

void NetworkClient::disconnect() {
    m_transport.disconnect();
    m_connected = false;
    m_clientId  = 0xFFFFFFFF;
}

bool NetworkClient::isConnected() const { return m_connected; }

void NetworkClient::update(const float dt) {
    m_transport.poll(0);
    m_timeSinceSnapshot += dt;

    // Compute interpolation alpha
    if (m_snapshotInterval > 0.0f) {
        m_interpolationAlpha = m_timeSinceSnapshot / m_snapshotInterval;
        if (m_interpolationAlpha > 1.0f) {
            m_interpolationAlpha = 1.0f;
        }
    }
}

void NetworkClient::applySnapshots(ffe::World& world,
                                   const ReplicationRegistry& registry) {
    const Snapshot* snap = m_snapshots.latest();
    if (snap == nullptr) { return; }

    for (uint32_t e = 0; e < snap->entityCount; ++e) {
        const EntitySnapshot& es = snap->entities[e];
        const auto entityId = static_cast<ffe::EntityId>(es.entityId);

        // Ensure the entity exists
        if (!world.isValid(entityId)) {
            continue; // Entity doesn't exist locally -- skip
        }

        // Walk the packed component data in the entity snapshot
        uint16_t offset = 0;
        while (offset + 4 <= es.dataSize) {
            uint16_t compId   = 0;
            uint16_t compSize = 0;
            std::memcpy(&compId,   es.componentData + offset, 2);
            offset += 2;
            std::memcpy(&compSize, es.componentData + offset, 2);
            offset += 2;

            if (offset + compSize > es.dataSize) { break; }

            const ReplicatedComponent* rc = registry.find(compId);
            if (rc != nullptr) {
                // Deserialize into the component
                if (compId == COMPONENT_ID_TRANSFORM &&
                    world.hasComponent<ffe::Transform>(entityId)) {
                    auto& comp = world.getComponent<ffe::Transform>(entityId);
                    rc->deserialize(&comp, es.componentData + offset, compSize);
                } else if (compId == COMPONENT_ID_TRANSFORM3D &&
                           world.hasComponent<ffe::Transform3D>(entityId)) {
                    auto& comp = world.getComponent<ffe::Transform3D>(entityId);
                    rc->deserialize(&comp, es.componentData + offset, compSize);
                }
            }

            offset += compSize;
        }
    }
}

float NetworkClient::getInterpolationAlpha() const {
    return m_interpolationAlpha;
}

uint32_t NetworkClient::clientId() const {
    return m_clientId;
}

bool NetworkClient::send(const uint8_t* data, const uint16_t len) {
    if (!m_connected) { return false; }
    return m_transport.send(0, data, len, true);
}

void NetworkClient::setConnectedCallback(const ConnectedCallback cb, void* userData) {
    m_connectedCb   = cb;
    m_connectedData = userData;
}

void NetworkClient::setDisconnectedCallback(const DisconnectedCallback cb, void* userData) {
    m_disconnectedCb   = cb;
    m_disconnectedData = userData;
}

void NetworkClient::setMessageCallback(const MessageCallback cb, void* userData) {
    m_messageCb   = cb;
    m_messageData = userData;
}

} // namespace ffe::networking
