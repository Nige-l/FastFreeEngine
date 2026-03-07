#pragma once

// engine/networking/network_system.h
//
// Module-level interface for integrating networking into the Application loop.
// Follows the same pattern as physics (module-level globals, init/shutdown).
//
// Only one mode is active at a time: server XOR client.
//
// No virtual functions, no std::function.
//
// Tiers: LEGACY (primary), STANDARD, MODERN.

#include "networking/lag_compensation.h"
#include "networking/lobby.h"
#include "networking/prediction.h"
#include "networking/replication.h"
#include "networking/server.h"
#include "networking/client.h"

#include <cstdint>

namespace ffe {
class World;
} // namespace ffe

namespace ffe::networking {

/// Initialise the networking subsystem (ENet + default component registration).
void initNetworkSystem();

/// Shut down the networking subsystem. Stops server/client if running.
void shutdownNetworkSystem();

/// Call each frame from the application tick. Handles both server and client
/// update + snapshot logic.
void updateNetworkSystem(float dt, ffe::World& world);

// -- Accessors --
NetworkServer* getServer();
NetworkClient* getClient();
bool isServer();
bool isClient();

// -- Control --
bool startServer(uint16_t port, uint32_t maxClients = 32);
bool connectToServer(const char* host, uint16_t port);
void disconnectNetwork();

// -- Messaging --
/// Send a custom game message.  Server: broadcasts to all clients.
/// Client: sends to server.  msgType is a user-defined u8.
/// Returns false if not connected or packet exceeds MTU.
bool sendGameMessage(uint8_t msgType, const uint8_t* data, uint16_t len);

/// Send a custom game message to a specific client (server only).
/// Returns false if not in server mode or client not found.
bool sendGameMessageTo(uint32_t clientId, uint8_t msgType,
                       const uint8_t* data, uint16_t len);

// -- Tick rate --
/// Set the network tick rate (Hz).  Clamped to [1, 120].
void setNetworkTickRate(float hz);

// -- Shared replication registry --
ReplicationRegistry& getReplicationRegistry();

// -- Prediction (client-side) --
/// Designate the locally predicted entity (client-side prediction).
void setLocalPlayer(uint32_t entityId);

/// Send an input command to the server and apply predicted movement locally.
bool sendInput(const InputCommand& cmd);

/// Set the movement function for client-side prediction.
void setMovementFunction(MoveFn fn, void* userData);

/// Get the last reconciliation error magnitude.
float getPredictionError();

/// Get the current network tick (server tick if server, prediction tick if client).
uint32_t getCurrentNetworkTick();

// -- Input handling (server-side) --
/// Set a callback that fires when a client sends an InputCommand.
void setInputCallback(InputCallbackFn cb, void* userData);

// -- Lag compensation (server-side rewind) --
/// Perform a lag-compensated hit check.
/// Server-side: uses LagCompensator rewind.  Client-side: sends HIT_CHECK
/// packet to server (returns empty result; response arrives via callback).
HitCheckResult performHitCheck(float originX, float originY, float originZ,
                               float dirX, float dirY, float dirZ,
                               float maxDist, uint32_t ignoreEntity);

/// Set the maximum rewind depth in ticks. Clamped to MAX_HISTORY_TICKS.
void setLagCompensationWindow(uint32_t ticks);

/// Set a callback for confirmed server-side hits.
void onHitConfirm(HitConfirmFn fn, void* userData);

// -- Lobby / Matchmaking --
/// Server: create a lobby with the given name and max players.
bool createLobby(const char* name, uint32_t maxPlayers);

/// Server: destroy the active lobby.
void destroyLobby();

/// Server: whether a lobby is currently active.
bool isLobbyActive();

/// Server: start the game if all players are ready.
void startLobbyGame();

/// Client: request to join the server's lobby.
bool joinLobby(const char* playerName);

/// Client: request to leave the lobby.
void leaveLobby();

/// Client: toggle ready state.
void setReady(bool ready);

/// Client: whether we are currently in a lobby.
bool isInLobby();

/// Get the lobby state (server or client, whichever is active).
const LobbyState& getLobbyState();

/// Set callback for lobby state updates (client-side).
void setLobbyUpdateCallback(LobbyUpdateFn fn, void* userData);

/// Set callback for game start (client-side).
void setGameStartCallback(GameStartFn fn, void* userData);

} // namespace ffe::networking
