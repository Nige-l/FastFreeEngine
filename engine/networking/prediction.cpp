// engine/networking/prediction.cpp
//
// Client-side prediction with server reconciliation.

#include "networking/prediction.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

#include <cmath>

namespace ffe::networking {

// ===========================================================================
// PredictionBuffer
// ===========================================================================

void PredictionBuffer::record(const InputCommand& cmd) {
    m_commands[m_head] = cmd;
    m_head = (m_head + 1) % MAX_PREDICTION_BUFFER;
    if (m_count < MAX_PREDICTION_BUFFER) {
        ++m_count;
    }
}

const InputCommand* PredictionBuffer::get(const uint32_t tickNumber) const {
    if (m_count == 0) { return nullptr; }

    // Search the circular buffer for the matching tick
    // Start from the most recent and walk backwards
    for (uint32_t i = 0; i < m_count; ++i) {
        const uint32_t idx = (m_head + MAX_PREDICTION_BUFFER - 1 - i) % MAX_PREDICTION_BUFFER;
        if (m_commands[idx].tickNumber == tickNumber) {
            return &m_commands[idx];
        }
    }
    return nullptr;
}

void PredictionBuffer::discardBefore(const uint32_t tickNumber) {
    // Remove entries with tickNumber < the given value.
    // Since this is a circular buffer, we need to find the new logical start.
    // We walk from the oldest entry and remove until we find one >= tickNumber.
    while (m_count > 0) {
        // Oldest entry is at (m_head + MAX_PREDICTION_BUFFER - m_count) % MAX_PREDICTION_BUFFER
        const uint32_t oldestIdx = (m_head + MAX_PREDICTION_BUFFER - m_count) % MAX_PREDICTION_BUFFER;
        if (m_commands[oldestIdx].tickNumber < tickNumber) {
            --m_count;
        } else {
            break;
        }
    }
}

uint32_t PredictionBuffer::count() const {
    return m_count;
}

void PredictionBuffer::clear() {
    m_head  = 0;
    m_count = 0;
}

// ===========================================================================
// ClientPrediction
// ===========================================================================

void ClientPrediction::setMovementFunction(const MoveFn fn, void* userData) {
    m_moveFn       = fn;
    m_moveUserData = userData;
}

void ClientPrediction::setLocalEntity(const uint32_t entityId) {
    m_localEntity = entityId;
}

uint32_t ClientPrediction::localEntity() const {
    return m_localEntity;
}

void ClientPrediction::recordAndPredict(ffe::World& world, const InputCommand& cmd) {
    ++m_currentTick;

    // Store a copy with the current tick number
    InputCommand stored = cmd;
    stored.tickNumber = m_currentTick;
    m_buffer.record(stored);

    // Apply the movement prediction locally
    if (m_moveFn != nullptr && m_localEntity != 0xFFFFFFFF) {
        m_moveFn(world, m_localEntity, stored, m_moveUserData);
    }
}

float ClientPrediction::reconcile(ffe::World& world, const uint32_t serverTick,
                                  const float serverX, const float serverY,
                                  const float serverZ) {
    if (m_localEntity == 0xFFFFFFFF) {
        m_lastError = 0.0f;
        return 0.0f;
    }

    // Get current entity position to compare with server state
    const auto entityId = static_cast<ffe::EntityId>(m_localEntity);
    if (!world.isValid(entityId)) {
        m_lastError = 0.0f;
        return 0.0f;
    }

    // Read the predicted position at the server tick.
    // We need to compute the error between where we predicted we'd be
    // at serverTick and where the server says we actually are.
    //
    // For simplicity: we compare current entity position with server state.
    // The entity's current position reflects all predictions including those
    // after serverTick. We'll snap to server and replay.

    float currentX = 0.0f;
    float currentY = 0.0f;
    float currentZ = 0.0f;

    if (world.hasComponent<ffe::Transform3D>(entityId)) {
        const auto& t = world.getComponent<ffe::Transform3D>(entityId);
        currentX = t.position.x;
        currentY = t.position.y;
        currentZ = t.position.z;
    } else if (world.hasComponent<ffe::Transform>(entityId)) {
        const auto& t = world.getComponent<ffe::Transform>(entityId);
        currentX = t.position.x;
        currentY = t.position.y;
        currentZ = t.position.z;
    }

    // Calculate error magnitude
    const float dx = serverX - currentX;
    const float dy = serverY - currentY;
    const float dz = serverZ - currentZ;
    const float error = std::sqrt(dx * dx + dy * dy + dz * dz);
    m_lastError = error;

    // Discard inputs that the server has already processed
    m_buffer.discardBefore(serverTick + 1);

    if (error <= RECONCILE_THRESHOLD) {
        // Error is small enough — no correction needed
        return error;
    }

    // Snap entity to the server's authoritative position
    if (world.hasComponent<ffe::Transform3D>(entityId)) {
        auto& t = world.getComponent<ffe::Transform3D>(entityId);
        t.position.x = serverX;
        t.position.y = serverY;
        t.position.z = serverZ;
    } else if (world.hasComponent<ffe::Transform>(entityId)) {
        auto& t = world.getComponent<ffe::Transform>(entityId);
        t.position.x = serverX;
        t.position.y = serverY;
        t.position.z = serverZ;
    }

    // Replay all buffered inputs after serverTick
    if (m_moveFn != nullptr) {
        for (uint32_t tick = serverTick + 1; tick <= m_currentTick; ++tick) {
            const InputCommand* cmd = m_buffer.get(tick);
            if (cmd != nullptr) {
                m_moveFn(world, m_localEntity, *cmd, m_moveUserData);
            }
        }
    }

    return error;
}

uint32_t ClientPrediction::currentTick() const {
    return m_currentTick;
}

float ClientPrediction::lastError() const {
    return m_lastError;
}

} // namespace ffe::networking
