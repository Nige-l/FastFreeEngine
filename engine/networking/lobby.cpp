// engine/networking/lobby.cpp
//
// Lobby / matchmaking system implementation.
// All data is fixed-size, no heap allocations.

#include "networking/lobby.h"
#include "networking/packet.h"

#include <cstring>

namespace ffe::networking {

// ===========================================================================
// LobbyServer
// ===========================================================================

bool LobbyServer::create(const char* name, const uint32_t maxPlayers) {
    if (m_active) { return false; }
    if (name == nullptr || name[0] == '\0') { return false; }
    if (maxPlayers == 0 || maxPlayers > MAX_LOBBY_PLAYERS) { return false; }

    m_state = LobbyState{};
    m_state.maxPlayers = maxPlayers;

    // Copy name, truncate to MAX_LOBBY_NAME_LENGTH - 1
    const size_t nameLen = std::strlen(name);
    const size_t copyLen = (nameLen < MAX_LOBBY_NAME_LENGTH - 1)
                               ? nameLen
                               : MAX_LOBBY_NAME_LENGTH - 1;
    std::memcpy(m_state.name, name, copyLen);
    m_state.name[copyLen] = '\0';

    m_active = true;
    return true;
}

void LobbyServer::destroy() {
    m_state  = LobbyState{};
    m_active = false;
}

bool LobbyServer::isActive() const { return m_active; }

void LobbyServer::handlePacket(const uint32_t connectionId,
                                const PacketType type,
                                PacketReader& reader) {
    if (!m_active) { return; }

    switch (type) {
    case PacketType::LOBBY_JOIN: {
        // Read player name from packet
        char playerName[MAX_LOBBY_NAME_LENGTH]{};
        if (!reader.readString(playerName, MAX_LOBBY_NAME_LENGTH)) {
            return; // malformed
        }

        // Reject if name is empty
        if (playerName[0] == '\0') { return; }

        // Reject duplicate join from same connectionId
        if (findPlayer(connectionId) >= 0) { return; }

        // Find a free slot
        const int32_t slot = findFreeSlot();
        if (slot < 0) { return; } // lobby full

        // Add the player
        LobbyPlayerInfo& info = m_state.players[slot];
        info.connectionId = connectionId;
        info.ready = false;

        const size_t nameLen = std::strlen(playerName);
        const size_t copyLen = (nameLen < MAX_LOBBY_NAME_LENGTH - 1)
                                   ? nameLen
                                   : MAX_LOBBY_NAME_LENGTH - 1;
        std::memcpy(info.name, playerName, copyLen);
        info.name[copyLen] = '\0';

        ++m_state.playerCount;

        // Fire callback
        if (m_joinFn != nullptr) {
            m_joinFn(connectionId, info.name, m_joinUserData);
        }
        break;
    }

    case PacketType::LOBBY_LEAVE: {
        removePlayer(connectionId);
        break;
    }

    case PacketType::LOBBY_READY: {
        const int32_t idx = findPlayer(connectionId);
        if (idx < 0) { return; }

        uint8_t readyByte = 0;
        if (!reader.readU8(readyByte)) { return; }

        m_state.players[idx].ready = (readyByte != 0);
        break;
    }

    default:
        break; // ignore unknown types
    }
}

void LobbyServer::removePlayer(const uint32_t connectionId) {
    if (!m_active) { return; }

    const int32_t idx = findPlayer(connectionId);
    if (idx < 0) { return; }

    // Clear the slot
    m_state.players[idx] = LobbyPlayerInfo{};
    if (m_state.playerCount > 0) {
        --m_state.playerCount;
    }

    // Fire callback
    if (m_leaveFn != nullptr) {
        m_leaveFn(connectionId, m_leaveUserData);
    }
}

bool LobbyServer::allReady() const {
    if (!m_active || m_state.playerCount == 0) { return false; }

    uint32_t readyCount = 0;
    for (uint32_t i = 0; i < MAX_LOBBY_PLAYERS; ++i) {
        if (m_state.players[i].connectionId != 0) {
            if (!m_state.players[i].ready) {
                return false;
            }
            ++readyCount;
        }
    }
    return readyCount > 0;
}

void LobbyServer::startGame() {
    if (!m_active) { return; }
    m_state.gameStarted = true;
}

void LobbyServer::broadcastState(ServerTransport& transport) {
    if (!m_active) { return; }

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    // Header (payload length patched below)
    PacketHeader header;
    header.type          = PacketType::LOBBY_STATE;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0; // patched
    writeHeader(writer, header);

    // Lobby name
    writer.writeString(m_state.name);

    // Max players and player count
    writer.writeU32(m_state.maxPlayers);
    writer.writeU32(m_state.playerCount);

    // gameStarted flag
    writer.writeU8(m_state.gameStarted ? 1 : 0);

    // Each active player
    for (uint32_t i = 0; i < MAX_LOBBY_PLAYERS; ++i) {
        const LobbyPlayerInfo& info = m_state.players[i];
        if (info.connectionId == 0) { continue; }

        writer.writeU32(info.connectionId);
        writer.writeU8(info.ready ? 1 : 0);
        writer.writeString(info.name);
    }

    if (writer.hasError()) { return; }

    // Patch payload length
    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t));

    transport.broadcast(0, buffer, writer.bytesWritten(), true);
}

void LobbyServer::setPlayerJoinCallback(const PlayerJoinFn fn, void* ud) {
    m_joinFn       = fn;
    m_joinUserData = ud;
}

void LobbyServer::setPlayerLeaveCallback(const PlayerLeaveFn fn, void* ud) {
    m_leaveFn       = fn;
    m_leaveUserData = ud;
}

const LobbyState& LobbyServer::state() const { return m_state; }

int32_t LobbyServer::findPlayer(const uint32_t connectionId) const {
    for (uint32_t i = 0; i < MAX_LOBBY_PLAYERS; ++i) {
        if (m_state.players[i].connectionId == connectionId) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t LobbyServer::findFreeSlot() const {
    for (uint32_t i = 0; i < m_state.maxPlayers; ++i) {
        if (m_state.players[i].connectionId == 0) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// ===========================================================================
// LobbyClient
// ===========================================================================

void LobbyClient::requestJoin(const char* playerName, ClientTransport& transport) {
    if (playerName == nullptr || playerName[0] == '\0') { return; }

    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_JOIN;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0; // patched
    writeHeader(writer, header);

    writer.writeString(playerName);

    if (writer.hasError()) { return; }

    // Patch payload length
    const uint16_t payloadLen = writer.bytesWritten() - HEADER_SIZE;
    std::memcpy(buffer + 4, &payloadLen, sizeof(uint16_t));

    transport.send(0, buffer, writer.bytesWritten(), true);
}

void LobbyClient::requestLeave(ClientTransport& transport) {
    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_LEAVE;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 0;
    writeHeader(writer, header);

    if (!writer.hasError()) {
        transport.send(0, buffer, writer.bytesWritten(), true);
    }

    m_inLobby = false;
}

void LobbyClient::requestReady(const bool ready, ClientTransport& transport) {
    uint8_t buffer[MAX_PACKET_SIZE];
    PacketWriter writer(buffer, MAX_PACKET_SIZE);

    PacketHeader header;
    header.type          = PacketType::LOBBY_READY;
    header.channel       = 0;
    header.sequence      = 0;
    header.payloadLength = 1;
    writeHeader(writer, header);

    writer.writeU8(ready ? 1 : 0);

    if (!writer.hasError()) {
        transport.send(0, buffer, writer.bytesWritten(), true);
    }
}

void LobbyClient::handlePacket(const PacketType type, PacketReader& reader) {
    if (type == PacketType::LOBBY_STATE) {
        // Deserialize lobby state
        LobbyState newState{};

        if (!reader.readString(newState.name, MAX_LOBBY_NAME_LENGTH)) { return; }
        if (!reader.readU32(newState.maxPlayers)) { return; }
        if (!reader.readU32(newState.playerCount)) { return; }

        uint8_t startedByte = 0;
        if (!reader.readU8(startedByte)) { return; }
        newState.gameStarted = (startedByte != 0);

        // Read player entries
        uint32_t readCount = 0;
        for (uint32_t i = 0; i < newState.playerCount && i < MAX_LOBBY_PLAYERS; ++i) {
            LobbyPlayerInfo info{};
            if (!reader.readU32(info.connectionId)) { break; }

            uint8_t readyByte = 0;
            if (!reader.readU8(readyByte)) { break; }
            info.ready = (readyByte != 0);

            if (!reader.readString(info.name, MAX_LOBBY_NAME_LENGTH)) { break; }

            newState.players[readCount] = info;
            ++readCount;
        }
        newState.playerCount = readCount;

        m_state   = newState;
        m_inLobby = true;

        if (m_updateFn != nullptr) {
            m_updateFn(m_state, m_updateUserData);
        }
    } else if (type == PacketType::LOBBY_GAME_START) {
        m_state.gameStarted = true;

        if (m_startFn != nullptr) {
            m_startFn(m_startUserData);
        }
    }
}

void LobbyClient::setLobbyUpdateCallback(const LobbyUpdateFn fn, void* ud) {
    m_updateFn       = fn;
    m_updateUserData = ud;
}

void LobbyClient::setGameStartCallback(const GameStartFn fn, void* ud) {
    m_startFn       = fn;
    m_startUserData = ud;
}

const LobbyState& LobbyClient::state() const { return m_state; }
bool LobbyClient::isInLobby() const { return m_inLobby; }
bool LobbyClient::isGameStarted() const { return m_state.gameStarted; }

} // namespace ffe::networking
