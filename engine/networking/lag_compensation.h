#pragma once

// engine/networking/lag_compensation.h
//
// Server-side lag compensation: records entity position history and
// performs rewound hit checks (ray-vs-sphere) at past ticks.
//
// No virtual functions, no std::function, no per-frame heap allocations.
// All buffers are fixed-size arrays in the object.
//
// Tiers: LEGACY (primary), STANDARD, MODERN.

#include <cstdint>

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint32_t MAX_HISTORY_TICKS    = 64;  // ~3.2 s at 20 Hz
static constexpr uint32_t MAX_TRACKED_ENTITIES = 256;

// ---------------------------------------------------------------------------
// EntityState — snapshot of one entity's position + collision radius
// ---------------------------------------------------------------------------
struct EntityState {
    uint32_t entityId{0xFFFFFFFF};
    float    x{0.0f};
    float    y{0.0f};
    float    z{0.0f};
    float    radius{0.5f};
    bool     active{false};
};

// ---------------------------------------------------------------------------
// HistoryFrame — all tracked entities at a single tick
// ---------------------------------------------------------------------------
struct HistoryFrame {
    uint32_t    tick{0};
    uint32_t    entityCount{0};
    EntityState entities[MAX_TRACKED_ENTITIES]{};
};

// ---------------------------------------------------------------------------
// HitCheckResult — outcome of a lag-compensated ray-vs-sphere check
// ---------------------------------------------------------------------------
struct HitCheckResult {
    bool     hit{false};
    uint32_t hitEntityId{0xFFFFFFFF};
    float    hitX{0.0f};
    float    hitY{0.0f};
    float    hitZ{0.0f};
    float    distance{0.0f};
};

// ---------------------------------------------------------------------------
// HitConfirmFn — callback for confirmed server-side hits
// ---------------------------------------------------------------------------
using HitConfirmFn = void(*)(uint32_t shooterEntityId,
                              const HitCheckResult& result, void*);

// ---------------------------------------------------------------------------
// LagCompensator — circular buffer of HistoryFrames + ray-vs-sphere checks
// ---------------------------------------------------------------------------
class LagCompensator {
public:
    /// Record entity positions for this tick. Call every network tick on the
    /// server BEFORE applying new inputs.
    void recordFrame(uint32_t tick, const EntityState* entities, uint32_t count);

    /// Perform a lag-compensated hit check: rewinds to \p atTick, does a
    /// ray-vs-sphere check against all entities at that tick's positions.
    /// Returns the closest hit (if any). Ignores \p ignoreEntityId.
    HitCheckResult performHitCheck(uint32_t atTick,
                                   float originX, float originY, float originZ,
                                   float dirX, float dirY, float dirZ,
                                   float maxDistance,
                                   uint32_t ignoreEntityId) const;

    /// Get the frame at a specific tick (or nullptr if not in history).
    const HistoryFrame* getFrame(uint32_t tick) const;

    uint32_t oldestTick() const;
    uint32_t newestTick() const;
    uint32_t frameCount() const;

    void     setMaxRewindTicks(uint32_t ticks); ///< Clamped to MAX_HISTORY_TICKS
    uint32_t maxRewindTicks() const;

    void clear();

private:
    HistoryFrame m_frames[MAX_HISTORY_TICKS]{};
    uint32_t     m_head{0};
    uint32_t     m_count{0};
    uint32_t     m_maxRewind{MAX_HISTORY_TICKS};
};

} // namespace ffe::networking
