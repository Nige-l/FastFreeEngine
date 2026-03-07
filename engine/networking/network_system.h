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

} // namespace ffe::networking
