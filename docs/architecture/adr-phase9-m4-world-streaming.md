# ADR: Phase 9 M4 — Terrain World Streaming

**Status:** Accepted
**Date:** 2026-03-08
**Tier:** LEGACY (OpenGL 3.3). Threading is opt-in; without streaming, zero threading overhead.

---

## Context

The existing terrain system (`terrain.cpp` / `terrain_internal.h`) uploads all chunks to the GPU inside `loadTerrain()`. For current demos (60×60m, 4×4 chunks) this is acceptable. For open-world sessions with very large heightmaps or multiple terrain tiles the full eager-load exceeds LEGACY VRAM budget (~56 MB per terrain) and produces unacceptable load spikes.

`TerrainAsset` holds `chunks[MAX_CHUNKS_TOTAL]` (256 slots) of `TerrainChunkGpu` — one VAO/VBO/IBO per LOD per chunk. The renderer iterates this array every frame for frustum + LOD tests. The streaming design extends this array with per-chunk state without changing the iteration or draw paths.

---

## Decision

### Chunk State Machine

Add `ChunkState` enum to `terrain_internal.h`:

```
EAGER            -- default; loaded at loadTerrain(), never evicted
UNLOADED         -- not on GPU; eligible for load
QUEUED           -- enqueued for background generation
GENERATING       -- worker thread active on this chunk
READY_TO_UPLOAD  -- CPU buffers ready; main thread must GL-upload
LOADED           -- on GPU; participates in render, LOD, frustum
UNLOADING        -- eviction pending (main thread frees GL objects)
```

`EAGER` is the initial state for all chunks when streaming is disabled (`streamingRadius == 0`). Existing `loadTerrain()` path is unchanged: it generates + uploads all chunks eagerly and sets every slot to `EAGER`. No existing demo requires modification.

Add `ChunkStreamState` alongside `TerrainChunkGpu` in `TerrainAsset`:

```cpp
struct ChunkStreamState {
    std::atomic<ChunkState> state{ChunkState::EAGER};
    std::vector<float>    cpuVertices;   // populated by worker; freed after upload
    std::vector<uint32_t> cpuIndices;    // populated by worker; freed after upload
};
std::array<ChunkStreamState, MAX_CHUNKS_TOTAL> chunkStream;
```

`chunkStream` is pre-allocated in `TerrainAsset` — no per-frame heap allocation on the main thread hot path.

### Streaming API

```cpp
// terrain.h additions
void setTerrainStreamingRadius(TerrainHandle handle, int radiusInChunks);
int  getTerrainLoadedChunkCount(TerrainHandle handle);
```

`radiusInChunks == 0` disables streaming (all chunks remain `EAGER`). `radiusInChunks > 0` enables streaming: chunks within the Chebyshev distance (in chunk-grid coordinates) of the camera's current chunk are `LOADED`; chunks outside are `UNLOADED` or evicted.

### Streaming Tick

Called once per frame from `terrainRenderSystem()`, gated by camera movement:

```
if distance(camera.position, lastStreamTickPos) < 0.5 * chunkWidth: skip
```

On tick:
1. Compute camera chunk coordinate `(cx, cz)`.
2. For each chunk slot:
   - If within radius and state is `UNLOADED`: set `QUEUED`, push `ChunkCoord` onto work queue.
   - If outside radius and state is `LOADED`: set `UNLOADING`; main thread frees GL objects, sets `UNLOADED`.
3. Scan for `READY_TO_UPLOAD` chunks: glBufferData upload, set `LOADED`.

`EAGER` chunks are never touched by the streaming tick — they remain permanently loaded.

### Background Worker Thread

One `std::thread` per terrain, created at `loadTerrain()`, destroyed at `unloadTerrain()`.

```
while (running):
    wait on condition_variable (std::mutex + std::deque<ChunkCoord> workQueue)
    pop ChunkCoord
    if state != QUEUED: discard (stale request)
    set state = GENERATING
    generate vertex + index CPU buffers (same code path as eager load, extracted to helper)
    set state = READY_TO_UPLOAD
```

No `std::function` in the worker loop. Work items are plain `struct ChunkCoord { u32 x, z; }`. Cancellation: worker checks `state == UNLOADED` before starting; if true, discards.

Synchronisation primitives: `std::mutex`, `std::condition_variable`, `std::atomic<ChunkState>`. No external dependencies.

### Render Pass Integration

`terrainRenderSystem()` draw loop:

- `EAGER` or `LOADED`: participate in LOD distance test + frustum cull (unchanged).
- `UNLOADED`, `QUEUED`, `GENERATING`, `UNLOADING`: skip draw call (no VAO).
- `READY_TO_UPLOAD`: upload on this frame tick (before draw), then draw as `LOADED`.
- Frustum cull: skip AABB test for `GENERATING` chunks (AABB not yet computed). After upload, AABB is populated identically to the eager path.

LOD selection uses `chunk.center` distance to camera — no change required.

---

## Lua API

```lua
ffe.setTerrainStreamingRadius(handle, radius)  -- 0 = disabled (default)
ffe.getTerrainLoadedChunkCount(handle)         -- returns int
```

Both bindings are cold path only. `setTerrainStreamingRadius(handle, 0)` is a no-op if streaming was never enabled.

---

## Memory Budget

| Item | Size | Allocation site |
|---|---|---|
| `chunkStream` array | `MAX_CHUNKS_TOTAL * sizeof(ChunkStreamState)` ≈ 256 × 48B = ~12 KB | `TerrainAsset` static member — allocated at `loadTerrain`, freed at `unloadTerrain` |
| CPU vertex buffer (per chunk, during generation) | ≤ `chunkResolution² × 32B` ≈ 256 KB at res=64 | Heap, background thread. Freed immediately after GL upload. |
| CPU index buffer (per chunk, during generation) | ≤ `(chunkResolution-1)² × 6 × 4B` ≈ 96 KB at res=64 | Heap, background thread. Freed immediately after GL upload. |
| Work queue | `std::deque<ChunkCoord>`, bounded by chunk count | Heap, negligible |

No per-frame allocation on the main thread. Worker thread allocations are temporary and freed on upload.

---

## Files Changed

| File | Change |
|---|---|
| `engine/renderer/terrain_internal.h` | Add `ChunkState` enum, `ChunkStreamState` struct, extend `TerrainAsset` |
| `engine/renderer/terrain.h` | Add `setTerrainStreamingRadius`, `getTerrainLoadedChunkCount` |
| `engine/renderer/terrain.cpp` | Worker thread management, streaming tick, GL upload path |
| `engine/renderer/terrain_renderer.cpp` | Gate draw calls on chunk state, call streaming tick |
| `engine/scripting/script_engine.cpp` | Lua bindings for streaming API |
| `tests/renderer/test_terrain_streaming.cpp` | Unit tests (state machine transitions, radius calculation) |

---

## Constraints and Non-Goals

- No runtime backend selection: streaming is toggled via `setTerrainStreamingRadius`, not compile-time flags.
- Worker thread does CPU mesh generation only. All GL calls remain on the main thread.
- Maximum one worker thread per terrain. Parallel multi-chunk generation within one terrain is not implemented (single consumer, single queue).
- No priority queue: chunks are streamed FIFO. Camera-distance prioritisation is deferred.
- No compressed/mip streaming: heightmap data is retained in `TerrainAsset::heightData` for height queries; the worker reads from it directly without re-parsing disk.
- Vulkan backend: streaming design is backend-agnostic at the CPU level. GL upload calls are isolated to the main-thread upload step — replacing with RHI calls is straightforward when the Vulkan terrain path is added.
