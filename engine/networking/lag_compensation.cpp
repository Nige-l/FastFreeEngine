// engine/networking/lag_compensation.cpp
//
// Server-side lag compensation: circular history buffer + ray-vs-sphere
// hit checks at rewound ticks.

#include "networking/lag_compensation.h"

#include <cmath>
#include <cstring>

namespace ffe::networking {

// ===========================================================================
// recordFrame
// ===========================================================================

void LagCompensator::recordFrame(const uint32_t tick,
                                 const EntityState* entities,
                                 const uint32_t count) {
    HistoryFrame& frame = m_frames[m_head];
    frame.tick        = tick;
    frame.entityCount = (count <= MAX_TRACKED_ENTITIES) ? count : MAX_TRACKED_ENTITIES;

    if (entities != nullptr && frame.entityCount > 0) {
        std::memcpy(frame.entities, entities,
                    frame.entityCount * sizeof(EntityState));
    } else {
        frame.entityCount = 0;
    }

    // Zero out remaining slots so stale data is never read
    for (uint32_t i = frame.entityCount; i < MAX_TRACKED_ENTITIES; ++i) {
        frame.entities[i] = EntityState{};
    }

    m_head = (m_head + 1) % MAX_HISTORY_TICKS;
    if (m_count < MAX_HISTORY_TICKS) {
        ++m_count;
    }
}

// ===========================================================================
// getFrame
// ===========================================================================

const HistoryFrame* LagCompensator::getFrame(const uint32_t tick) const {
    for (uint32_t i = 0; i < m_count; ++i) {
        // Walk backwards from head
        const uint32_t idx = (m_head + MAX_HISTORY_TICKS - 1 - i) % MAX_HISTORY_TICKS;
        if (m_frames[idx].tick == tick) {
            return &m_frames[idx];
        }
    }
    return nullptr;
}

// ===========================================================================
// performHitCheck — ray-vs-sphere against all entities at atTick
// ===========================================================================

HitCheckResult LagCompensator::performHitCheck(
    const uint32_t atTick,
    const float originX, const float originY, const float originZ,
    const float dirX, const float dirY, const float dirZ,
    const float maxDistance,
    const uint32_t ignoreEntityId) const
{
    HitCheckResult result;

    // Validate float inputs
    if (!std::isfinite(originX) || !std::isfinite(originY) || !std::isfinite(originZ)) {
        return result;
    }
    if (!std::isfinite(dirX) || !std::isfinite(dirY) || !std::isfinite(dirZ)) {
        return result;
    }
    if (!std::isfinite(maxDistance) || maxDistance <= 0.0f) {
        return result;
    }

    // Normalize direction
    const float dirLen = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (dirLen < 1e-6f) {
        return result; // zero-length direction
    }
    const float invLen = 1.0f / dirLen;
    const float ndx = dirX * invLen;
    const float ndy = dirY * invLen;
    const float ndz = dirZ * invLen;

    const HistoryFrame* frame = getFrame(atTick);
    if (frame == nullptr) {
        return result;
    }

    float closestDist = maxDistance + 1.0f; // sentinel

    for (uint32_t i = 0; i < frame->entityCount; ++i) {
        const EntityState& ent = frame->entities[i];
        if (!ent.active) { continue; }
        if (ent.entityId == ignoreEntityId) { continue; }
        if (ent.radius <= 0.0f) { continue; }

        // Vector from ray origin to sphere center
        const float ocx = ent.x - originX;
        const float ocy = ent.y - originY;
        const float ocz = ent.z - originZ;

        // Project center onto ray: t_ca = dot(oc, dir)
        const float tca = ocx * ndx + ocy * ndy + ocz * ndz;

        // If sphere center is behind the ray and ray origin is outside sphere, skip
        const float ocLenSq = ocx * ocx + ocy * ocy + ocz * ocz;
        const float rSq = ent.radius * ent.radius;
        if (tca < 0.0f && ocLenSq > rSq) {
            continue;
        }

        // Distance squared from sphere center to closest point on ray
        const float d2 = ocLenSq - tca * tca;
        if (d2 > rSq) {
            continue; // ray misses the sphere
        }

        // Distance from closest-point to sphere surface along ray
        const float thc = std::sqrt(rSq - d2);

        // Entry point
        float t = tca - thc;
        if (t < 0.0f) {
            // Origin inside sphere: use exit point
            t = tca + thc;
        }

        if (t < 0.0f || t > maxDistance) {
            continue;
        }

        if (t < closestDist) {
            closestDist      = t;
            result.hit        = true;
            result.hitEntityId = ent.entityId;
            result.hitX       = originX + ndx * t;
            result.hitY       = originY + ndy * t;
            result.hitZ       = originZ + ndz * t;
            result.distance   = t;
        }
    }

    return result;
}

// ===========================================================================
// Accessors
// ===========================================================================

uint32_t LagCompensator::oldestTick() const {
    if (m_count == 0) { return 0; }
    const uint32_t idx = (m_head + MAX_HISTORY_TICKS - m_count) % MAX_HISTORY_TICKS;
    return m_frames[idx].tick;
}

uint32_t LagCompensator::newestTick() const {
    if (m_count == 0) { return 0; }
    const uint32_t idx = (m_head + MAX_HISTORY_TICKS - 1) % MAX_HISTORY_TICKS;
    return m_frames[idx].tick;
}

uint32_t LagCompensator::frameCount() const {
    return m_count;
}

void LagCompensator::setMaxRewindTicks(const uint32_t ticks) {
    m_maxRewind = (ticks <= MAX_HISTORY_TICKS) ? ticks : MAX_HISTORY_TICKS;
}

uint32_t LagCompensator::maxRewindTicks() const {
    return m_maxRewind;
}

void LagCompensator::clear() {
    m_head  = 0;
    m_count = 0;
}

} // namespace ffe::networking
