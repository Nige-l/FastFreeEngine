# Net Arena -- FFE Multiplayer Demo

A simple 2D networked multiplayer demo. Players are colored squares moving in a shared arena.

## How to Run

Build the project, then open two terminals:

**Terminal 1 (Server):**

```
./build/examples/net_demo/ffe_net_demo
```

Press **S** to host a server on port 7777.

**Terminal 2 (Client):**

```
./build/examples/net_demo/ffe_net_demo
```

Press **C** to connect to localhost:7777.

## Controls

- **WASD** -- move your player
- **ESC** -- quit

## What It Demonstrates

- Server hosting and client connection via ENet UDP
- Automatic ECS snapshot replication (Transform components)
- Client-side input sending via `ffe.sendInput()`
- Server-side authoritative movement via `ffe.onServerInput()`
- Client-side prediction with `ffe.setLocalPlayer()`
- Network tick and prediction error display
