# ADR: Networking and Multiplayer

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Session:** 57 (Phase 4 kickoff)
**Tiers:** LEGACY (primary), STANDARD, MODERN — RETRO not supported (ENet is pure C, no GPU dependency; tier limitation is the engine baseline, not ENet)
**Security Review Required:** YES — networking is a primary attack surface (CLAUDE.md Section 5). Shift-left review of this ADR is mandatory before implementation begins.

---

## 1. Problem Statement

FFE has a complete 2D+3D pipeline, editor, ECS, Lua scripting, physics, and audio — but no multiplayer support. Multiplayer is a core requirement of the engine (CLAUDE.md Section 1: "multiplayer networking" is listed as a first-class engine capability). Phase 4 adds client-server networking so that games built on FFE can support real-time multiplayer for both 2D and 3D.

This ADR defines the transport layer, architecture model, replication strategy, packet format, security hardening, Lua API, headless server mode, directory structure, and test plan.

---

## 2. Transport Library Selection

### 2.1 Decision: ENet

**ENet** (http://enet.bespin.org/) is the transport library for FFE networking.

| Property | ENet |
|----------|------|
| License | MIT |
| Language | Pure C |
| Protocol | UDP with reliability layer |
| vcpkg | Yes (`enet`) |
| Maturity | 20+ years, used in dozens of shipped games |
| Binary size | ~30 KB |
| Dependencies | None (socket API only) |

ENet provides exactly what a game networking layer needs and nothing more:

- **Channels** with configurable delivery: reliable ordered, reliable unordered, unreliable sequenced, unreliable unordered
- **Connection management** with handshake, keep-alive, and timeout
- **Bandwidth throttling** with configurable rate limits per peer
- **Fragmentation and reassembly** for packets exceeding MTU
- **Sequencing** with built-in sequence numbers per channel

### 2.2 Alternatives Considered

| Alternative | Why rejected |
|-------------|-------------|
| **GameNetworkingSockets** (Valve) | Heavy dependency (~500 KB, pulls in protobuf + OpenSSL). Designed for Steam integration. Overkill for FFE's needs. License is BSD-3 but the dependency chain is complex. |
| **Raw UDP sockets** | Too much reinvention. Reliability layer, fragmentation, sequencing, connection management — all must be built from scratch. ENet solves these problems with 20 years of battle-testing. |
| **TCP** | Too slow for real-time games. Head-of-line blocking makes it unsuitable for fast-paced multiplayer. Nagle's algorithm adds latency. TCP is acceptable for lobby/matchmaking metadata but not for gameplay state. |
| **SLikeNet / RakNet forks** | Abandoned or poorly maintained. License ambiguity (RakNet was BSD then proprietary then BSD again). ENet is simpler and actively maintained. |

---

## 3. Client-Server Architecture

### 3.1 Authoritative Server Model

FFE uses an **authoritative server** model. The server is the single source of truth for game state.

```
+----------+       inputs        +----------+       inputs        +----------+
| Client A | ------------------> |          | <------------------ | Client B |
|          | <-- state snapshot   |  Server  |  state snapshot --> |          |
+----------+                     |          |                     +----------+
                                 +----------+
                                   |  runs  |
                                   | physics |
                                   |  logic  |
                                   |   ECS   |
                                   +---------+
```

**Server responsibilities:**
- Owns the canonical ECS `World`
- Runs physics simulation and game logic each tick
- Validates all client input before applying it
- Broadcasts state snapshots to connected clients
- Manages connections, assigns client IDs, enforces limits

**Client responsibilities:**
- Sends player input to the server (keypresses, mouse, actions)
- Receives state snapshots and applies them to the local ECS
- Interpolates between snapshots for smooth rendering
- Renders the scene — the client is a "dumb terminal" with a display

### 3.2 Listen Server vs. Dedicated Server

Both modes are supported:

| Mode | Description |
|------|-------------|
| **Listen server** | A player hosts the game. Their process runs both server and client logic. The server tick runs on the main thread alongside the render loop. Suitable for casual/small games. |
| **Dedicated server** | A headless process runs server logic only. No renderer, no window, no audio. Suitable for competitive or large-player-count games. See Section 11. |

The `NetworkSystem` abstracts this: `startServer()` initialises server-side state, `connectToServer()` initialises client-side state, and both can coexist in the same process (listen server).

---

## 4. ECS Replication

### 4.1 Replicated Component Registry

Not all ECS components need to cross the network. Components are explicitly registered as **replicated** at engine initialisation:

```cpp
namespace ffe::net {

// Called during engine init — registers which component types replicate
void registerReplicatedComponent<Transform>(ComponentId id, SerializeFn serialize, DeserializeFn deserialize);
void registerReplicatedComponent<Transform3D>(ComponentId id, SerializeFn serialize, DeserializeFn deserialize);
// ... etc.

} // namespace ffe::net
```

**Default replicated components:**

| Component | Rationale |
|-----------|-----------|
| `Transform` | 2D position/rotation/scale — must sync for 2D games |
| `Transform3D` | 3D position/rotation/scale — must sync for 3D games |
| `Sprite` | Visual state (texture, color, flip) |
| `Mesh` | Which 3D model an entity uses |
| `Material3D` | Material properties (color, texture) |

Components NOT replicated by default: `ParticleEmitter` (client-side VFX), `Collider2D` (server-side only), `SpriteAnimation` (client-side playback). Games can override this via the registration API.

### 4.2 Network Tick Rate

Replication runs on a **network tick** separate from the render frame rate:

| Parameter | Default | Range |
|-----------|---------|-------|
| Network tick rate | 20 Hz | 10-60 Hz configurable |
| Render frame rate | 60+ Hz (uncapped) | Independent of network tick |

At each network tick, the server:
1. Gathers all entities with replicated components
2. Computes a snapshot (see 4.3)
3. Sends the snapshot to each connected client

### 4.3 Snapshot and Delta Compression

**Full snapshot:** A complete dump of all replicated entity/component data. Sent on initial connection and periodically as a baseline.

**Delta snapshot:** Only components that changed since the last acknowledged snapshot. This is the common case.

```
Snapshot format:
  [tick_number: u32]
  [baseline_tick: u32]       // 0 = full snapshot, else delta from this tick
  [entity_count: u16]
  for each entity:
    [entity_id: u32]
    [component_mask: u16]    // bitfield: which components are present in this delta
    for each set bit in component_mask:
      [component_data: ...]  // serialised by the registered SerializeFn
```

**Change detection:** Each replicated component stores a `dirty` flag set by the ECS when a component is modified. The replication system reads and clears these flags each network tick. Only dirty components are included in the delta.

### 4.4 Client-Side Interpolation

Clients render between two received snapshots to produce smooth visuals at a higher frame rate than the network tick rate.

```
Server tick:  T0 ---- T1 ---- T2 ---- T3
Client render:    ^  ^  ^  ^  ^  ^  ^  ^    (60 fps)
                  interpolating between T0 and T1
```

The client maintains a **snapshot buffer** (ring buffer of the last 8-16 snapshots). The render time is offset behind real time by one network tick interval (the "interpolation delay"). Each render frame, the client interpolates between the two snapshots bracketing the render time.

For `Transform` and `Transform3D`, interpolation is linear (position) and slerp (rotation). Components without a registered interpolation function snap to the newer value.

### 4.5 Future: Client-Side Prediction

Client-side prediction (the client simulates ahead and reconciles when the server confirms) is deferred to a later session. It requires:
- Input replay buffer on the client
- Server acknowledgement of processed input sequence numbers
- Reconciliation (re-simulate from last confirmed state)

This is complex and not needed for the initial networking milestone. Interpolation-only is sufficient for cooperative games and slower-paced multiplayer. Prediction will be added for competitive/fast-paced games.

---

## 5. Packet Format

### 5.1 Binary Format

All packets use a binary format. No JSON, no text protocols. Every field has a fixed or length-prefixed size.

```
Packet layout:
  [type: u8]              // PacketType enum
  [sequence: u16]         // per-channel sequence number (ENet provides this, but we add our own for application-level tracking)
  [payload_length: u16]   // length of payload in bytes (redundant with ENet packet size, but used for validation)
  [payload: u8[]]         // type-specific data
```

**Maximum packet size:** 1200 bytes. This is MTU-safe (standard Ethernet MTU is 1500, minus IP/UDP headers). Packets exceeding this are fragmented by ENet automatically, but we design payloads to fit within 1200 bytes where possible to avoid fragmentation overhead.

### 5.2 Packet Types

```cpp
enum class PacketType : uint8_t {
    // Connection
    CONNECT_REQUEST  = 0x01,   // client -> server: request to join
    CONNECT_ACCEPT   = 0x02,   // server -> client: accepted, here is your client ID
    CONNECT_REJECT   = 0x03,   // server -> client: rejected (full, banned, etc.)
    DISCONNECT       = 0x04,   // either direction: graceful disconnect

    // Gameplay
    INPUT            = 0x10,   // client -> server: player input for this tick
    SNAPSHOT_FULL    = 0x11,   // server -> client: full ECS state
    SNAPSHOT_DELTA   = 0x12,   // server -> client: delta from baseline

    // Events and RPC
    EVENT            = 0x20,   // either direction: game event (reliable)
    RPC              = 0x21,   // either direction: remote procedure call

    // Diagnostics
    PING             = 0xF0,   // either direction: latency measurement
    PONG             = 0xF1,   // response to PING
};
```

### 5.3 Channel Assignment

ENet supports multiple channels per connection. FFE uses 3:

| Channel | Delivery | Purpose |
|---------|----------|---------|
| 0 | Reliable ordered | Connection, events, RPC |
| 1 | Unreliable sequenced | Snapshots (newer snapshot supersedes older) |
| 2 | Reliable ordered | Input (must arrive in order, must not be lost) |

### 5.4 Bounds-Checked Packet Reader

All packet deserialization uses a `PacketReader` that enforces bounds:

```cpp
class PacketReader {
    const uint8_t* m_data;
    uint32_t m_size;
    uint32_t m_offset;
public:
    PacketReader(const uint8_t* data, uint32_t size);

    bool readU8(uint8_t& out);
    bool readU16(uint16_t& out);
    bool readU32(uint32_t& out);
    bool readFloat(float& out);
    bool readBytes(uint8_t* out, uint32_t count);

    bool hasRemaining(uint32_t bytes) const;
    uint32_t remaining() const;
};
```

Every `read*` function returns `false` if the read would exceed the packet boundary. **No read ever proceeds without a bounds check.** A failed read causes the packet to be dropped and the connection flagged for rate-limit evaluation.

Similarly, a `PacketWriter` with size limit enforcement is used for serialization.

---

## 6. Security

**All network packets are untrusted external input.** This section is the most critical part of this ADR. Every item here is a hard requirement, not a suggestion.

### 6.1 Threat Model

| Threat | Mitigation |
|--------|------------|
| **Malformed packets** | Bounds-checked `PacketReader` (Section 5.4). Unknown packet types are dropped. Invalid field values cause packet drop, not crash. |
| **Oversized packets** | Maximum packet size enforced at receive (1200 bytes for application layer; ENet's MTU limit for transport). Packets exceeding the limit are dropped before parsing. |
| **Packet flooding / DoS** | Per-connection rate limiting: max packets per second (default 100), max bytes per second (default 64 KB/s). Exceeding the limit disconnects the peer. |
| **Integer overflow in size fields** | All size calculations use `uint32_t` with explicit overflow checks before allocation or read. `payload_length` is validated against actual ENet packet size before use. |
| **Unbounded allocations** | No heap allocations driven by packet data. Snapshot deserialization writes into pre-allocated fixed-size buffers. Entity count per snapshot is capped (e.g., 1024). Component data sizes are bounded by registered max sizes. |
| **Replay attacks** | Application-level sequence numbers per channel. Duplicate or out-of-window sequence numbers are dropped. Window size: 256 packets. |
| **State manipulation (cheating)** | Authoritative server model. Clients send only input, never state. Server validates all input (e.g., movement speed cannot exceed max, actions must be legal in current game state). |
| **Connection exhaustion** | Maximum client limit (default 32, configurable). New connections beyond the limit receive CONNECT_REJECT. |
| **Zombie connections** | Timeout for inactive connections (default 10 seconds, configurable). ENet provides built-in keep-alive; we add application-level heartbeat validation. |
| **Buffer over-read** | `PacketReader` prevents all over-reads. Component deserialization functions receive a bounded sub-span, not the raw packet. |

### 6.2 Input Validation Rules

Every field read from a packet is validated before use:

```
u8 type      -> must be a known PacketType value; unknown = drop
u16 sequence -> compared against per-channel window; duplicate/old = drop
u16 length   -> must match actual packet remaining bytes; mismatch = drop
u32 entityId -> must be a valid entity known to server/client; unknown = skip entity
u16 mask     -> only registered component bits may be set; unknown bits = drop
float values -> NaN/Inf rejection on all position, rotation, scale fields
```

### 6.3 Rate Limiting Implementation

```cpp
struct ConnectionRateLimit {
    uint32_t m_packetsThisSecond = 0;
    uint32_t m_bytesThisSecond = 0;
    float    m_windowStart = 0.0f;       // time of current window start

    static constexpr uint32_t MAX_PACKETS_PER_SECOND = 100;
    static constexpr uint32_t MAX_BYTES_PER_SECOND = 65536;  // 64 KB/s

    // Returns true if the packet is allowed
    bool allowPacket(uint32_t packetSize, float currentTime);
};
```

Rate limits are checked **before** any packet parsing. If a connection exceeds its limit, all further packets in that window are dropped silently (no response sent — prevents amplification).

### 6.4 Security Invariants

These invariants must hold at all times. Violation of any invariant is a CRITICAL security finding:

1. **No packet can cause a heap allocation whose size is determined by packet data.**
2. **No packet can cause a read or write beyond a buffer boundary.**
3. **No packet from a client can directly modify server-side game state** (only input is accepted; state changes result from server simulation).
4. **No connection can consume more than its rate-limited share of server resources.**
5. **No packet type is processed without first validating the packet length.**

---

## 7. Directory Structure

```
engine/networking/
    transport.h             ENet wrapper: init, connect, send, receive, poll
    transport.cpp
    server.h                Server: client management, connection handling, tick
    server.cpp
    client.h                Client: server connection, snapshot reception
    client.cpp
    packet.h                PacketType enum, PacketReader, PacketWriter
    packet.cpp
    replication.h           Replicated component registry, snapshot/delta, interpolation
    replication.cpp
    network_system.h        ECS system: network tick integration with application loop
    network_system.cpp
    .context.md             AI-native documentation for the networking subsystem
```

### 7.1 Dependency Graph

```
network_system --> server, client, replication     (top-level integration)
server         --> transport, packet, replication   (server uses all layers)
client         --> transport, packet, replication   (client uses all layers)
replication    --> packet                           (serialises components into packets)
transport      --> ENet                             (thin wrapper)
packet         --> (none)                           (standalone serialisation)
```

### 7.2 Integration Points

| Existing system | Integration |
|-----------------|-------------|
| `engine/core/application.cpp` | Calls `NetworkSystem::tick()` each frame. Headless mode skips renderer but still ticks networking. |
| `engine/core/ecs.h` | Dirty flags on replicated components (or a separate dirty-tracking ECS system). |
| `engine/scripting/script_engine.cpp` | Registers `ffe.startServer`, `ffe.connectToServer`, etc. |
| `CMakeLists.txt` | New `engine/networking/` source files. Link `enet` from vcpkg. |
| `vcpkg.json` | Add `"enet"` dependency. |

---

## 8. Lua API

### 8.1 Connection Management

```lua
-- Start a server on the given port (returns true/false)
ffe.startServer(port)               -- e.g., ffe.startServer(7777)

-- Connect to a server (returns true/false, connection completes asynchronously)
ffe.connectToServer(host, port)     -- e.g., ffe.connectToServer("127.0.0.1", 7777)

-- Graceful disconnect
ffe.disconnect()

-- Query connection state
ffe.isServer()                       -- true if this instance is running as server
ffe.isConnected()                    -- true if connected to a server (client-side)
ffe.getClientId()                    -- returns this client's unique ID (0 if not connected)
ffe.getConnectedClients()            -- server only: returns table of client IDs
```

### 8.2 Messaging

```lua
-- Send a message on a named channel (reliable by default)
ffe.sendMessage(channel, data)       -- channel: string key; data: string or table

-- Register a callback for incoming messages
ffe.onNetworkMessage(function(senderId, channel, data)
    -- senderId: client ID of the sender (0 = server)
    -- channel: string key matching the send
    -- data: the received data
end)
```

### 8.3 Connection Events

```lua
-- Server-side: called when a client connects
ffe.onClientConnected(function(clientId)
    ffe.log("Client " .. clientId .. " connected")
end)

-- Server-side: called when a client disconnects
ffe.onClientDisconnected(function(clientId)
    ffe.log("Client " .. clientId .. " disconnected")
end)

-- Client-side: called when connection to server is established
ffe.onConnected(function()
    ffe.log("Connected to server")
end)

-- Client-side: called when disconnected from server
ffe.onDisconnected(function(reason)
    ffe.log("Disconnected: " .. reason)
end)
```

### 8.4 Replication Control (Advanced)

```lua
-- Mark an entity for network replication (server-side)
ffe.setNetworkEntity(entityId, true)

-- Check if an entity is network-replicated
ffe.isNetworkEntity(entityId)

-- Set network tick rate (server-side, default 20)
ffe.setNetworkTickRate(rate)
```

### 8.5 Sandbox Restrictions

All networking Lua bindings go through the same sandbox as existing bindings (1M instruction budget, no raw filesystem/OS access). Specifically:

- `ffe.startServer` and `ffe.connectToServer` are the only functions that open sockets. They are controlled by the engine, not raw Lua `socket` access.
- `ffe.sendMessage` data is serialized through the engine's packet system, not raw bytes. The engine enforces size limits on message payloads (max 1024 bytes per message).
- No Lua script can send raw bytes over the network. All communication is through the structured message API.

---

## 9. Headless Server Mode

### 9.1 Requirement

A dedicated server must run without creating a window, initializing OpenGL, or loading audio. This saves resources and allows servers to run on machines without a GPU.

### 9.2 Implementation

`Application` already has a concept of headless mode (used for audio tests). This extends to full headless:

```cpp
struct ApplicationConfig {
    bool headless = false;          // existing: skip audio device
    bool headlessRenderer = false;  // NEW: skip window + OpenGL + audio entirely
    // ... or combine into a single mode enum
};
```

When `headlessRenderer` is true:
- No GLFW window creation
- No OpenGL context initialization
- No audio device initialization
- No shader compilation
- The application loop still ticks: ECS systems, physics, networking, scripting

### 9.3 Dedicated Server Binary

```
examples/dedicated_server/
    main.cpp           -- minimal main() that creates Application in headless mode,
                          loads a Lua game script, starts the server
    server_game.lua    -- example server-side game script
```

Alternatively, runtime flag: `./ffe_runtime --server --port 7777 game.lua`

The runtime binary (`ffe_runtime`) gains a `--server` flag that enables headless mode and auto-calls `ffe.startServer(port)`.

---

## 10. Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO | **No** | Engine baseline does not support RETRO. ENet itself would work fine. |
| LEGACY | **Yes** (primary) | ENet is pure C, uses only POSIX/Winsock sockets. No GPU involvement. Network tick at 20 Hz is trivially cheap. Snapshot serialization/deserialization is bounded CPU work (iterate replicated components, write/read fixed-size structs). Well within LEGACY budget. |
| STANDARD | **Yes** | Identical to LEGACY. Higher tick rates (30-60 Hz) available if desired. |
| MODERN | **Yes** | Identical to LEGACY. Future: could add encryption (DTLS) as optional layer for competitive games. |

### 10.1 Performance Budget

| Operation | Cost | Frequency |
|-----------|------|-----------|
| ENet poll (receive) | ~0.01 ms | Once per frame |
| Snapshot serialization (server, 100 entities, 3 components each) | ~0.1 ms | Once per network tick (20 Hz) |
| Snapshot deserialization (client) | ~0.05 ms | Once per network tick |
| Interpolation (client, 100 entities) | ~0.02 ms | Once per render frame |
| Rate limit check | ~0.001 ms | Per received packet |

**Total networking overhead per frame:** < 0.5 ms on LEGACY hardware. Well within the 16.6 ms frame budget at 60 fps.

### 10.2 Memory Budget

| Allocation | Size | Lifetime |
|------------|------|----------|
| ENet host structure | ~2 KB | Session |
| Per-peer state (32 peers max) | ~1 KB each = 32 KB | Per connection |
| Snapshot buffer (client, 16 slots) | ~16 KB per slot = 256 KB | Session |
| Packet read/write buffers | 1200 bytes x 2 | Per frame (stack or arena) |
| Replicated component registry | ~1 KB | Static |

**Total:** < 512 KB. Fits comfortably in LEGACY's 1 GB VRAM constraint (though networking uses system RAM, not VRAM).

---

## 11. Test Plan

### 11.1 Unit Tests (no network I/O)

| Test area | What is tested |
|-----------|----------------|
| `PacketReader` bounds checking | Read past end returns false. Read exactly to end succeeds. Zero-length packet. |
| `PacketWriter` size limits | Write past capacity returns false. Payload length field matches written bytes. |
| `PacketReader` type validation | Unknown `PacketType` values are rejected. |
| Float validation | NaN and Inf values rejected during deserialization. |
| Integer overflow | Size calculations with large values do not overflow. |
| Snapshot serialization | Round-trip: serialize 10 entities with known component data, deserialize, compare. |
| Delta compression | Modify 2 of 10 entities, delta snapshot contains only the 2. Unmodified entities are absent. |
| Delta application | Apply delta to a baseline snapshot, verify result matches expected full state. |
| Interpolation | Two snapshots with known transforms, interpolate at t=0.0, 0.5, 1.0, verify positions. |
| Sequence number window | Accept in-window, reject duplicate, reject too-old. |
| Rate limiting | Exceed packet limit, verify packets are rejected. Time window rollover. |
| Replicated component registry | Register, query, serialize, deserialize with registered functions. |

### 11.2 Integration Tests (loopback, no real network)

| Test area | What is tested |
|-----------|----------------|
| Connection state machine | Server start -> client connect -> handshake -> connected -> disconnect -> cleanup. All on localhost. |
| Client ID assignment | Connect 3 clients, verify unique IDs. Disconnect one, reconnect, verify new ID. |
| Full snapshot delivery | Server creates 5 entities, client connects, receives full snapshot, verifies local ECS matches. |
| Delta delivery | Server modifies 1 entity, client receives delta, verifies only that entity changed. |
| Connection limit | Start server with max_clients=2, connect 3 clients, verify third gets CONNECT_REJECT. |
| Timeout | Connect client, stop sending, verify server disconnects after timeout. |
| Listen server | Same process runs server + client, verify it works without deadlock. |

### 11.3 Lua Binding Tests

| Test area | What is tested |
|-----------|----------------|
| `ffe.startServer` | Returns true, `ffe.isServer()` returns true. |
| `ffe.connectToServer` | Loopback connection succeeds, `ffe.isConnected()` returns true. |
| `ffe.sendMessage` / `ffe.onNetworkMessage` | Round-trip message delivery on loopback. |
| `ffe.getClientId` | Returns non-zero after connection. |
| `ffe.getConnectedClients` | Server sees connected client IDs. |
| `ffe.disconnect` | `ffe.isConnected()` returns false after disconnect. |
| Error handling | `ffe.startServer` on invalid port returns false. `ffe.connectToServer` to unreachable host times out gracefully. |

### 11.4 Security Tests

| Test area | What is tested |
|-----------|----------------|
| Malformed packet | Send packet with invalid type byte, verify server does not crash. |
| Oversized packet | Send 64 KB packet, verify it is dropped. |
| Truncated packet | Send packet with `payload_length` larger than actual data, verify `PacketReader` rejects. |
| Rate limit enforcement | Send 200 packets in 1 second (limit 100), verify excess are dropped and connection is flagged. |
| Entity ID spoofing | Client sends snapshot (should be server-only), verify server ignores it. |
| Sequence replay | Send same sequence number twice, verify second is dropped. |

---

## 12. Implementation Phases

This is a large subsystem. PM should break implementation across multiple sessions:

### Phase 4a — Foundation (Sessions 57-58)

- `packet.h/.cpp` — PacketReader, PacketWriter, PacketType enum
- `transport.h/.cpp` — ENet wrapper (init, shutdown, connect, send, receive, poll)
- `vcpkg.json` update — add `enet` dependency
- `CMakeLists.txt` — add `engine/networking/` sources
- Unit tests for packet and transport layers

### Phase 4b — Server and Client (Sessions 59-60)

- `server.h/.cpp` — connection management, client tracking, rate limiting
- `client.h/.cpp` — server connection, state management
- `network_system.h/.cpp` — tick integration with application loop
- Connection state machine tests
- Headless server mode in `Application`

### Phase 4c — Replication (Sessions 61-62)

- `replication.h/.cpp` — component registry, snapshot, delta, interpolation
- ECS dirty tracking integration
- Snapshot/delta round-trip tests
- Interpolation tests

### Phase 4d — Lua Bindings and Demo (Sessions 63-64)

- Lua bindings for all networking functions
- `.context.md` for the networking subsystem
- Networked demo game (e.g., simple multiplayer arena)
- Security test suite

### Phase 4e — Polish (Sessions 65+)

- Client-side prediction and server reconciliation
- NAT traversal / relay (if needed)
- Lobby/matchmaking API
- Lag compensation

---

## 13. Alternatives Considered

### 13.1 Peer-to-Peer Architecture

In P2P, all clients are equal and communicate directly with each other.

**Rejected because:**
- No authoritative state makes cheating trivial — any client can claim any game state.
- Scales poorly: N clients require N*(N-1)/2 connections.
- NAT traversal is harder when every client must reach every other client.
- Desync bugs are extremely difficult to debug without a canonical state owner.

The authoritative server model is the industry standard for competitive and cooperative multiplayer.

### 13.2 Deterministic Lockstep

Used in RTS games: all clients run the same simulation with the same inputs. Only inputs are sent over the network.

**Rejected because:**
- Requires perfectly deterministic simulation (floating-point determinism across platforms is notoriously difficult).
- Any desync is catastrophic — the entire game diverges.
- Latency is felt directly (simulation cannot advance until all inputs arrive).
- FFE supports multiple platforms (Linux, macOS, Windows) with different compilers — floating-point determinism is not guaranteed.

Snapshot replication is more forgiving and works with non-deterministic simulations.

### 13.3 WebRTC / WebSocket

**Rejected.** FFE is a native C++ engine, not a browser game. WebRTC adds enormous complexity (ICE, STUN, DTLS, SRTP). WebSockets are TCP-based (head-of-line blocking). If FFE ever targets web (via Emscripten), a WebSocket fallback transport can be added behind the `transport.h` abstraction without changing the rest of the networking stack.

---

## 14. Open Questions

These do not block initial implementation but should be resolved during Phase 4:

1. **Encryption:** Should packet payloads be encrypted? ENet does not provide encryption natively. Options: DTLS wrapper, libsodium for symmetric encryption, or defer to Phase 5. For LAN/casual games, encryption is optional. For internet-facing competitive games, it is important.

2. **NAT Traversal:** ENet does not handle NAT hole-punching. Options: relay server (simple, reliable, adds latency), STUN/TURN integration, or require port forwarding for self-hosted servers. This is a Phase 4e concern.

3. **Bandwidth Profiling:** The 1200-byte MTU-safe target and 20 Hz tick rate should be validated with realistic game scenes (100+ entities, multiple component types). If snapshots exceed MTU regularly, consider more aggressive delta compression or component prioritization.

4. **Entity Ownership:** For some games, specific clients "own" specific entities (e.g., their player character). The replication system may benefit from an ownership model where the owning client has authority over certain components. This overlaps with client-side prediction and is deferred.
