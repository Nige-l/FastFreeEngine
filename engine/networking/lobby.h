#pragma once

// engine/networking/lobby.h
//
// Lobby / matchmaking system for pre-game player gathering.
// Server creates a lobby, clients join, toggle ready, and the server
// starts the game when all players are ready.
//
// Fixed-size, no heap allocations. All data fits in static arrays.
// No virtual functions, no std::function.
//
// Tiers: LEGACY (primary), STANDARD, MODERN.

#include "networking/packet.h"
#include "networking/transport.h"

#include <cstdint>

namespace ffe::networking {

// ---------------------------------------------------------------------------
// Lobby state — shared between server and client
// ---------------------------------------------------------------------------
struct LobbyState {
    char             name[MAX_LOBBY_NAME_LENGTH]{};
    uint32_t         maxPlayers{MAX_LOBBY_PLAYERS};
    uint32_t         playerCount{0};
    LobbyPlayerInfo  players[MAX_LOBBY_PLAYERS]{};
    bool             gameStarted{false};
};

// ---------------------------------------------------------------------------
// Callback types (raw function pointers + void* userData, no std::function)
// ---------------------------------------------------------------------------
using LobbyUpdateFn  = void(*)(const LobbyState&, void*);
using GameStartFn    = void(*)(void*);
using PlayerJoinFn   = void(*)(uint32_t connectionId, const char* name, void*);
using PlayerLeaveFn  = void(*)(uint32_t connectionId, void*);

// ---------------------------------------------------------------------------
// LobbyServer — manages the lobby on the server side
// ---------------------------------------------------------------------------
class LobbyServer {
public:
    /// Create a lobby with the given name and max players.
    /// Returns false if name is empty or maxPlayers is 0.
    bool create(const char* name, uint32_t maxPlayers);

    /// Destroy the lobby, resetting all state.
    void destroy();

    /// Whether the lobby is currently active.
    bool isActive() const;

    /// Process lobby packets (called from server's receive callback).
    /// Dispatches on PacketType: LOBBY_JOIN, LOBBY_LEAVE, LOBBY_READY.
    void handlePacket(uint32_t connectionId, PacketType type, PacketReader& reader);

    /// Player disconnected (called when transport reports disconnect).
    void removePlayer(uint32_t connectionId);

    /// Check if all players are ready (requires at least one player).
    bool allReady() const;

    /// Mark the game as started. Sets gameStarted flag.
    /// The caller is responsible for broadcasting the LOBBY_GAME_START packet.
    void startGame();

    /// Broadcast current lobby state to all connected clients.
    void broadcastState(ServerTransport& transport);

    // -- Callbacks --
    void setPlayerJoinCallback(PlayerJoinFn fn, void* ud);
    void setPlayerLeaveCallback(PlayerLeaveFn fn, void* ud);

    const LobbyState& state() const;

private:
    LobbyState   m_state{};
    bool         m_active{false};

    PlayerJoinFn  m_joinFn{nullptr};
    void*         m_joinUserData{nullptr};
    PlayerLeaveFn m_leaveFn{nullptr};
    void*         m_leaveUserData{nullptr};

    /// Find a player by connectionId. Returns index or -1.
    int32_t findPlayer(uint32_t connectionId) const;

    /// Find the first free slot. Returns index or -1.
    int32_t findFreeSlot() const;
};

// ---------------------------------------------------------------------------
// LobbyClient — manages the lobby on the client side
// ---------------------------------------------------------------------------
class LobbyClient {
public:
    /// Request to join the server's lobby.
    void requestJoin(const char* playerName, ClientTransport& transport);

    /// Request to leave the lobby.
    void requestLeave(ClientTransport& transport);

    /// Request to toggle ready state.
    void requestReady(bool ready, ClientTransport& transport);

    /// Process lobby state packet from server.
    void handlePacket(PacketType type, PacketReader& reader);

    // -- Callbacks --
    void setLobbyUpdateCallback(LobbyUpdateFn fn, void* ud);
    void setGameStartCallback(GameStartFn fn, void* ud);

    const LobbyState& state() const;
    bool isInLobby() const;
    bool isGameStarted() const;

private:
    LobbyState    m_state{};
    bool          m_inLobby{false};

    LobbyUpdateFn m_updateFn{nullptr};
    void*         m_updateUserData{nullptr};
    GameStartFn   m_startFn{nullptr};
    void*         m_startUserData{nullptr};
};

} // namespace ffe::networking
