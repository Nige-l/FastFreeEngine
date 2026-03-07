#pragma once

// engine/networking/prediction.h
//
// Client-side prediction with server reconciliation.
//
// The prediction system records local input commands, applies them
// immediately for responsive gameplay, and corrects when the server's
// authoritative state diverges.  Works WITHOUT networking for local
// testing — moveFn just moves the entity, reconcile can be tested with
// synthetic server state.
//
// No virtual functions, no std::function, no per-frame heap allocations.
// All buffers are fixed-size arrays on the stack / in the object.
//
// Tiers: LEGACY (primary), STANDARD, MODERN.

#include <cstdint>
#include <cmath>

namespace ffe {
class World;
} // namespace ffe

namespace ffe::networking {

// ---------------------------------------------------------------------------
// InputCommand — sent from client to server each tick
// ---------------------------------------------------------------------------
struct InputCommand {
    uint32_t tickNumber{0};   ///< Client tick when input was recorded
    uint32_t inputBits{0};    ///< Bitfield of pressed inputs (up to 32 buttons)
    float    dt{0.0f};        ///< Delta time for this tick
    float    aimX{0.0f};      ///< Optional aim direction X
    float    aimY{0.0f};      ///< Optional aim direction Y
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint32_t MAX_PREDICTION_BUFFER = 64;

// ---------------------------------------------------------------------------
// MoveFn — movement function pointer
// ---------------------------------------------------------------------------
// Applies one InputCommand to an entity's position in the World.
// Parameters: (World&, entityId, InputCommand, userData)
// No virtual dispatch, no std::function.
using MoveFn = void(*)(ffe::World&, uint32_t, const InputCommand&, void*);

// ---------------------------------------------------------------------------
// InputCallbackFn — server-side input callback
// ---------------------------------------------------------------------------
// Called when a client input packet arrives on the server.
// Parameters: (clientId, InputCommand, userData)
using InputCallbackFn = void(*)(uint32_t, const InputCommand&, void*);

// ---------------------------------------------------------------------------
// PredictionBuffer — circular buffer of InputCommands
// ---------------------------------------------------------------------------
class PredictionBuffer {
public:
    /// Store an input command. Overwrites oldest if full.
    void record(const InputCommand& cmd);

    /// Retrieve a command by tick number. Returns nullptr if not found.
    const InputCommand* get(uint32_t tickNumber) const;

    /// Discard all inputs before the given tick number.
    void discardBefore(uint32_t tickNumber);

    /// Number of commands currently stored.
    uint32_t count() const;

    /// Clear all stored commands.
    void clear();

private:
    InputCommand m_commands[MAX_PREDICTION_BUFFER]{};
    uint32_t     m_head{0};
    uint32_t     m_count{0};
};

// ---------------------------------------------------------------------------
// ClientPrediction — records input, applies predicted movement, reconciles
// ---------------------------------------------------------------------------
class ClientPrediction {
public:
    /// Set the movement function used to apply inputs to entities.
    void setMovementFunction(MoveFn fn, void* userData);

    /// Set the entity that is locally predicted.
    void setLocalEntity(uint32_t entityId);

    /// Get the local entity ID.
    uint32_t localEntity() const;

    /// Called each client tick: records input and applies predicted movement.
    /// Increments the current tick, stores the command, and calls m_moveFn.
    void recordAndPredict(ffe::World& world, const InputCommand& cmd);

    /// Called when a server snapshot arrives: compare predicted position with
    /// server's authoritative position.  If error exceeds threshold, snap
    /// the entity to the server position and replay all buffered inputs
    /// after serverTick.
    /// Returns the reconciliation error magnitude.
    float reconcile(ffe::World& world, uint32_t serverTick,
                    float serverX, float serverY, float serverZ);

    /// Current client prediction tick.
    uint32_t currentTick() const;

    /// Last reconciliation error magnitude.
    float lastError() const;

private:
    PredictionBuffer m_buffer;
    MoveFn           m_moveFn{nullptr};
    void*            m_moveUserData{nullptr};
    uint32_t         m_localEntity{0xFFFFFFFF};
    uint32_t         m_currentTick{0};
    float            m_lastError{0.0f};

    static constexpr float RECONCILE_THRESHOLD = 0.1f;
};

} // namespace ffe::networking
