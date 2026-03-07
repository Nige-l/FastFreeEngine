// engine/networking/server.cpp
//
// High-level game server: transport polling, network tick loop,
// snapshot serialisation and broadcast.

#include "networking/server.h"
#include "networking/packet.h"
#include "networking/prediction.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <cmath>
#include <cstring>

namespace ffe::networking {

// ===========================================================================
// Transport-level static callbacks (forward to NetworkServer members)
// ===========================================================================

void NetworkServer::onTransportConnect(const ConnectionId id, void* userData) {
    auto* self = static_cast<NetworkServer*>(userData);
    if (self->m_connectCb) {
        self->m_connectCb(id.id, self->m_connectData);
    }
}

void NetworkServer::onTransportDisconnect(const ConnectionId id, void* userData) {
    auto* self = static_cast<NetworkServer*>(userData);
    if (self->m_disconnectCb) {
        self->m_disconnectCb(id.id, self->m_disconnectData);
    }
}

void NetworkServer::onTransportReceive(const ReceivedPacket& pkt, void* userData) {
    auto* self = static_cast<NetworkServer*>(userData);

    // Try to parse as a known packet type
    if (pkt.dataLength >= HEADER_SIZE) {
        PacketReader reader(pkt.data, pkt.dataLength);
        PacketHeader header;
        if (readHeader(reader, header) && header.type == PacketType::INPUT) {
            // Parse InputCommand: tick(u32) + bits(u32) + dt(f32) + aimX(f32) + aimY(f32)
            InputCommand cmd;
            if (reader.readU32(cmd.tickNumber) &&
                reader.readU32(cmd.inputBits) &&
                reader.readF32(cmd.dt) &&
                reader.readF32(cmd.aimX) &&
                reader.readF32(cmd.aimY)) {

                // Validate dt — reject NaN, Inf, negative, or impossibly large
                if (!std::isfinite(cmd.dt) || cmd.dt < 0.0f || cmd.dt > 0.1f) {
                    return; // reject malformed input
                }

                // Validate aim values — reject NaN/Inf
                if (!std::isfinite(cmd.aimX) || !std::isfinite(cmd.aimY)) {
                    return; // reject NaN/Inf aim values
                }

                // Validate tick number (reject wildly out-of-range ticks)
                // Allow ticks within a reasonable window of the server tick
                const uint32_t maxDrift = 120; // ~2 seconds at 60Hz
                if (self->m_tick > maxDrift && cmd.tickNumber < self->m_tick - maxDrift) {
                    return; // too far in the past
                }
                if (cmd.tickNumber <= self->m_tick + maxDrift) {
                    // Queue the input for processing during networkTick
                    const uint32_t clientIdx = pkt.sender.id;
                    if (clientIdx < MAX_SERVER_PEERS) {
                        auto& queue = self->m_inputQueues[clientIdx];
                        if (queue.count < MAX_INPUT_QUEUE_PER_CLIENT) {
                            queue.commands[queue.count] = cmd;
                            ++queue.count;
                        }
                    }
                }
            }
            return; // Handled — do not forward to message callback
        }
    }

    // Not an input packet — forward to message callback
    if (self->m_messageCb) {
        self->m_messageCb(pkt.sender.id, pkt.data, pkt.dataLength, self->m_messageData);
    }
}

// ===========================================================================
// Public API
// ===========================================================================

bool NetworkServer::start(const ServerConfig& config) {
    if (m_running) { return false; }

    m_config = config;

    TransportConfig tc;
    tc.port       = config.port;
    tc.maxClients = config.maxClients;

    // Wire up transport callbacks to forward through our API
    m_transport.setConnectCallback(onTransportConnect, this);
    m_transport.setDisconnectCallback(onTransportDisconnect, this);
    m_transport.setReceiveCallback(onTransportReceive, this);

    if (!m_transport.start(tc)) {
        return false;
    }

    m_running         = true;
    m_tick            = 0;
    m_tickAccumulator = 0.0f;
    return true;
}

void NetworkServer::stop() {
    if (!m_running) { return; }
    m_transport.stop();
    m_running         = false;
    m_tick            = 0;
    m_tickAccumulator = 0.0f;
}

bool NetworkServer::isRunning() const { return m_running; }

void NetworkServer::update(const float dt) {
    if (!m_running) { return; }
    m_transport.poll(0);
    m_tickAccumulator += dt;
}

void NetworkServer::networkTick(ffe::World& world,
                                const ReplicationRegistry& registry) {
    if (!m_running) { return; }

    const float tickInterval = 1.0f / m_config.networkTickRate;
    if (m_tickAccumulator < tickInterval) { return; }
    m_tickAccumulator -= tickInterval;
    ++m_tick;

    // Record entity positions for lag compensation BEFORE applying new inputs
    {
        EntityState lagEntities[MAX_TRACKED_ENTITIES];
        uint32_t lagCount = 0;

        // Gather 2D entities (Transform)
        {
            auto view2d = world.view<ffe::Transform>();
            for (const auto entity : view2d) {
                if (lagCount >= MAX_TRACKED_ENTITIES) { break; }
                const auto eid = static_cast<ffe::EntityId>(entity);
                const auto& t = world.getComponent<ffe::Transform>(eid);
                auto& es = lagEntities[lagCount];
                es.entityId = static_cast<uint32_t>(entity);
                es.x        = t.position.x;
                es.y        = t.position.y;
                es.z        = 0.0f;
                es.radius   = 0.5f; // default for 2D
                es.active   = true;
                ++lagCount;
            }
        }

        // Gather 3D entities (Transform3D) not already covered
        {
            auto view3d = world.view<ffe::Transform3D>();
            for (const auto entity : view3d) {
                if (lagCount >= MAX_TRACKED_ENTITIES) { break; }
                const auto eid = static_cast<ffe::EntityId>(entity);
                if (world.hasComponent<ffe::Transform>(eid)) { continue; }
                const auto& t = world.getComponent<ffe::Transform3D>(eid);
                auto& es = lagEntities[lagCount];
                es.entityId = static_cast<uint32_t>(entity);
                es.x        = t.position.x;
                es.y        = t.position.y;
                es.z        = t.position.z;
                es.radius   = 0.5f; // default
                es.active   = true;
                ++lagCount;
            }
        }

        m_lagComp.recordFrame(m_tick, lagEntities, lagCount);
    }

    // Apply queued client inputs before building the snapshot
    applyQueuedInputs(world);

    // If no clients, skip snapshot work
    if (m_transport.clientCount() == 0) { return; }

    // --- Build a snapshot packet ---
    // Layout: PacketHeader + tick(u32) + entityCount(u32) + per-entity data
    // Per-entity: entityId(u32) + componentMask(u16) + [componentId(u16) + size(u16) + data]...

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    // Write packet header (payload length patched later)
    PacketHeader header;
    header.type          = PacketType::SNAPSHOT_FULL;
    header.channel       = 0;
    header.sequence      = static_cast<uint16_t>(m_tick & 0xFFFF);
    header.payloadLength = 0; // patched below
    writeHeader(writer, header);

    // Write tick number and placeholder for entity count
    writer.writeU32(m_tick);
    const uint16_t entityCountPos = writer.bytesWritten();
    writer.writeU32(0); // placeholder

    uint32_t entityCount = 0;

    // Iterate all entities that have a Transform component (2D replicated entities)
    {
        auto view = world.view<ffe::Transform>();
        for (const auto entity : view) {
            const auto entityId = static_cast<uint32_t>(entity);

            // Check remaining space: need at least entityId(4) + mask(2) + one component header
            if (writer.bytesWritten() + 8 >= MAX_PACKET_SIZE) { break; }

            const uint16_t entityStartPos = writer.bytesWritten();
            writer.writeU32(entityId);

            uint16_t mask = 0;
            const uint16_t maskPos = writer.bytesWritten();
            writer.writeU16(0); // placeholder for mask

            // Serialize each registered component that this entity has
            for (uint16_t c = 0; c < registry.count(); ++c) {
                const ReplicatedComponent& rc = registry.components()[c];

                const void* compData = nullptr;
                if (rc.componentId == COMPONENT_ID_TRANSFORM &&
                    world.hasComponent<ffe::Transform>(static_cast<ffe::EntityId>(entity))) {
                    compData = &world.getComponent<ffe::Transform>(
                        static_cast<ffe::EntityId>(entity));
                } else if (rc.componentId == COMPONENT_ID_TRANSFORM3D &&
                           world.hasComponent<ffe::Transform3D>(static_cast<ffe::EntityId>(entity))) {
                    compData = &world.getComponent<ffe::Transform3D>(
                        static_cast<ffe::EntityId>(entity));
                }

                if (compData == nullptr) { continue; }

                // Check space for component header + max data
                if (writer.bytesWritten() + 4 + rc.maxSerializedSize >= MAX_PACKET_SIZE) {
                    break;
                }

                // Serialize into a temp buffer, then write if successful
                uint8_t tempBuf[MAX_ENTITY_COMPONENT_DATA];
                const uint16_t written = rc.serialize(compData, tempBuf, rc.maxSerializedSize);
                if (written == 0) { continue; }

                writer.writeU16(rc.componentId);
                writer.writeU16(written);
                writer.writeBytes(tempBuf, written);

                mask |= static_cast<uint16_t>(1u << c);
            }

            // Patch the component mask
            // We need to write the mask at maskPos. Since PacketWriter doesn't
            // support random access, we patch the raw buffer directly.
            std::memcpy(buffer + maskPos, &mask, sizeof(uint16_t));

            ++entityCount;
            (void)entityStartPos;
        }
    }

    // Also iterate Transform3D entities that don't have Transform
    {
        auto view = world.view<ffe::Transform3D>();
        for (const auto entity : view) {
            // Skip if already covered by Transform view
            if (world.hasComponent<ffe::Transform>(static_cast<ffe::EntityId>(entity))) {
                continue;
            }

            const auto entityId = static_cast<uint32_t>(entity);

            if (writer.bytesWritten() + 8 >= MAX_PACKET_SIZE) { break; }

            writer.writeU32(entityId);

            uint16_t mask = 0;
            const uint16_t maskPos = writer.bytesWritten();
            writer.writeU16(0);

            for (uint16_t c = 0; c < registry.count(); ++c) {
                const ReplicatedComponent& rc = registry.components()[c];

                if (rc.componentId != COMPONENT_ID_TRANSFORM3D) { continue; }

                const auto& comp = world.getComponent<ffe::Transform3D>(
                    static_cast<ffe::EntityId>(entity));

                if (writer.bytesWritten() + 4 + rc.maxSerializedSize >= MAX_PACKET_SIZE) {
                    break;
                }

                uint8_t tempBuf[MAX_ENTITY_COMPONENT_DATA];
                const uint16_t written = rc.serialize(&comp, tempBuf, rc.maxSerializedSize);
                if (written == 0) { continue; }

                writer.writeU16(rc.componentId);
                writer.writeU16(written);
                writer.writeBytes(tempBuf, written);

                mask |= static_cast<uint16_t>(1u << c);
            }

            std::memcpy(buffer + maskPos, &mask, sizeof(uint16_t));
            ++entityCount;
        }
    }

    // Patch entity count
    std::memcpy(buffer + entityCountPos, &entityCount, sizeof(uint32_t));

    // Patch payload length in header (total bytes written minus header size)
    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t)); // offset 4 in header

    // Broadcast to all connected clients (unreliable for snapshots)
    if (!writer.hasError()) {
        m_transport.broadcast(0, buffer, writer.bytesWritten(), false);
    }
}

uint32_t NetworkServer::clientCount() const {
    return m_transport.clientCount();
}

uint32_t NetworkServer::tick() const {
    return m_tick;
}

bool NetworkServer::broadcast(const uint8_t* data, const uint16_t len) {
    if (!m_running) { return false; }
    m_transport.broadcast(0, data, len, true);
    return true;
}

bool NetworkServer::sendTo(const uint32_t clientId, const uint8_t* data, const uint16_t len) {
    if (!m_running) { return false; }
    ConnectionId cid;
    cid.id = clientId;
    return m_transport.send(cid, 0, data, len, true);
}

void NetworkServer::setTickRate(const float hz) {
    // Clamp to [1, 120]
    float clamped = hz;
    if (clamped < 1.0f)   { clamped = 1.0f; }
    if (clamped > 120.0f) { clamped = 120.0f; }
    m_config.networkTickRate = clamped;
}

void NetworkServer::setInputCallback(const InputCallbackFn cb, void* userData) {
    m_inputCb     = cb;
    m_inputCbData = userData;
}

void NetworkServer::applyQueuedInputs(ffe::World& /*world*/) {
    for (uint32_t clientIdx = 0; clientIdx < MAX_SERVER_PEERS; ++clientIdx) {
        auto& queue = m_inputQueues[clientIdx];
        for (uint32_t i = 0; i < queue.count; ++i) {
            if (m_inputCb != nullptr) {
                m_inputCb(clientIdx, queue.commands[i], m_inputCbData);
            }
        }
        queue.count = 0; // Clear the queue after processing
    }
}

void NetworkServer::setClientConnectCallback(const ClientConnectCallback cb, void* userData) {
    m_connectCb   = cb;
    m_connectData = userData;
}

void NetworkServer::setClientDisconnectCallback(const ClientDisconnectCallback cb, void* userData) {
    m_disconnectCb   = cb;
    m_disconnectData = userData;
}

void NetworkServer::setMessageCallback(const MessageCallback cb, void* userData) {
    m_messageCb   = cb;
    m_messageData = userData;
}

// ===========================================================================
// Lag compensation
// ===========================================================================

HitCheckResult NetworkServer::processHitCheck(
    const uint32_t shooterConnectionId,
    const uint32_t atTick,
    const float originX, const float originY, const float originZ,
    const float dirX, const float dirY, const float dirZ,
    const float maxDistance,
    const uint32_t ignoreEntityId)
{
    // Clamp rewind depth
    const uint32_t maxRewind = m_lagComp.maxRewindTicks();
    if (m_tick > maxRewind && atTick < m_tick - maxRewind) {
        return HitCheckResult{}; // too far in the past
    }

    const HitCheckResult result = m_lagComp.performHitCheck(
        atTick, originX, originY, originZ,
        dirX, dirY, dirZ, maxDistance, ignoreEntityId);

    if (result.hit && m_hitConfirmCb != nullptr) {
        m_hitConfirmCb(shooterConnectionId, result, m_hitConfirmData);
    }

    return result;
}

void NetworkServer::setHitConfirmCallback(const HitConfirmFn cb, void* userData) {
    m_hitConfirmCb   = cb;
    m_hitConfirmData = userData;
}

LagCompensator& NetworkServer::lagCompensator() {
    return m_lagComp;
}

const LagCompensator& NetworkServer::lagCompensator() const {
    return m_lagComp;
}

} // namespace ffe::networking
