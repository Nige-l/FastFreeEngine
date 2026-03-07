# ADR: Positional 3D Audio

**Status:** PROPOSED
**Author:** architect
**Date:** 2026-03-07
**Tiers:** LEGACY (primary), STANDARD, MODERN — RETRO not supported (unchanged from ADR-006)
**Security Review Required:** NO — no new file I/O paths, no new external input parsing (see Section 10)

---

## 1. Problem Statement

FFE's audio subsystem (ADR-006) provides fire-and-forget 2D sound effects and streaming music. All sounds play at uniform volume regardless of where the player or the sound source is in 3D space. For 3D games this is immersion-breaking — a footstep behind the player should sound different from one 50 metres away.

This ADR adds positional 3D audio: sounds that attenuate with distance and pan left/right based on the listener's orientation relative to the sound source.

---

## 2. Current Architecture Constraints

The existing audio subsystem uses **`ma_device` with a custom PCM mixer callback** — it does NOT use miniaudio's higher-level `ma_engine` or `ma_sound` APIs. This is important because:

- `ma_sound_set_position()` requires `ma_engine`, which we do not initialise.
- The custom callback manually mixes pre-decoded float32 PCM buffers from a fixed voice pool.
- A lock-free SPSC ring buffer passes commands (PLAY, STOP, PLAY_MUSIC, etc.) from the main thread to the audio callback thread.

The 3D spatialization must therefore be implemented **within our existing custom mixer**, not by switching to `ma_engine`. Switching to `ma_engine` would be a rewrite of the entire audio subsystem and is out of scope.

---

## 3. Spatialization Approach

### 3.1 Decision: Manual spatialization in the custom mixer

Apply per-voice **distance attenuation** and **stereo panning** during the mix step in `audioCallback()`. This is the simplest approach that works within the existing architecture.

For each active 3D voice, every callback invocation computes:

1. **Distance attenuation** — inverse distance clamped (see Section 6).
2. **Stereo pan** — based on the angle between the listener's forward direction and the vector from the listener to the source, projected onto the listener's right axis.

### 3.2 Per-voice spatial data

Each `Voice` struct gains three new fields:

```cpp
struct Voice {
    // ... existing fields ...
    bool  is3D = false;          // false = 2D (legacy behaviour), true = spatialised
    float posX = 0.0f;           // world-space position (set once at play time)
    float posY = 0.0f;
    float posZ = 0.0f;
};
```

2D voices (`is3D == false`) are mixed exactly as today — no regression.

### 3.3 Listener state

A global listener is stored in `AudioState`:

```cpp
struct Listener {
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float fwdX = 0.0f, fwdY = 0.0f, fwdZ = -1.0f;  // forward direction (normalised)
    float upX  = 0.0f, upY  = 1.0f, upZ  = 0.0f;    // up direction (normalised)
};
```

The right vector is derived as `cross(forward, up)` in the mixer. No need to store it.

### 3.4 Mix formula

For a 3D voice:

```
dx = voice.posX - listener.posX
dy = voice.posY - listener.posY
dz = voice.posZ - listener.posZ
distance = sqrt(dx*dx + dy*dy + dz*dz)

// Inverse distance clamped attenuation
gain = clamp(minDist / distance, 0.0, 1.0)   // when distance < minDist, gain = 1.0
if (distance > maxDist) gain = 0.0            // silence beyond max range

// Stereo pan: dot product of normalised source direction with listener right vector
if (distance > epsilon) {
    dirX = dx / distance;  dirY = dy / distance;  dirZ = dz / distance;
    rightX = fwdY*upZ - fwdZ*upY;  // cross(forward, up)
    rightY = fwdZ*upX - fwdX*upZ;
    rightZ = fwdX*upY - fwdY*upX;
    pan = dirX*rightX + dirY*rightY + dirZ*rightZ;  // [-1, +1]
} else {
    pan = 0.0;  // source at listener position = center
}

leftGain  = gain * clamp(1.0 - pan, 0.0, 1.0)   // simple linear pan law
rightGain = gain * clamp(1.0 + pan, 0.0, 1.0)
```

This runs per-sample-frame (not per-sample) in the callback — two multiplies per frame for stereo output. Well within LEGACY budget.

---

## 4. Listener Model

### 4.1 Auto-sync with 3D camera

Each frame, `Application::tick()` (or the render loop) calls an internal `audio::updateListener()` function that reads the `Camera` singleton from the ECS registry:

```cpp
namespace ffe::audio {
void updateListener(float posX, float posY, float posZ,
                    float fwdX, float fwdY, float fwdZ,
                    float upX,  float upY,  float upZ);
}
```

The application layer computes the forward direction as `normalize(camera.target - camera.position)` and passes it along with the camera's position and up vector. This happens once per frame — O(1), no allocations, no mutex (the listener struct is read by the callback and written by the main thread; we use atomics or a small spinlock-free double buffer).

### 4.2 Manual override

`setListenerPosition(x, y, z, fwdX, fwdY, fwdZ)` allows game code to override the automatic camera sync. When called, it sets a flag that suppresses auto-sync until the next scene load or explicit re-enable. This supports:

- Split-screen (listener follows a specific player, not the camera)
- Cutscenes (listener stays with the player character while the camera flies)
- 2D games that want positional audio without a 3D camera

### 4.3 One global listener

Matches the single-device, single-mixer architecture. Multi-listener (split-screen with separate audio) is a future concern and would require significant mixer changes.

---

## 5. Sound Source Model

### 5.1 Fire-and-forget positioned sounds

```cpp
void playSound3D(SoundHandle handle, float x, float y, float z, float volume = 1.0f);
```

This mirrors `playSound()` exactly:
- Finds a free voice slot from the existing pool
- Sets `is3D = true` and stores position
- Posts a PLAY_3D command to the SPSC ring buffer
- The callback picks it up and mixes with spatialization

Position is set **once at creation time**. The sound plays from that fixed world position until it finishes or is evicted. This matches the fire-and-forget model.

### 5.2 No persistent handles (this session)

Moving sound sources (e.g., a car engine that follows an entity) require persistent sound handles with per-frame position updates. This is deferred to a future session. The current scope is fixed-position fire-and-forget sounds, which covers:

- Footsteps, gunshots, explosions, impacts
- Environmental ambience (campfire, waterfall)
- UI/notification sounds positioned in the world

### 5.3 Future: persistent sound handles

A future ADR will introduce `SoundInstance` handles returned by `playSound3D()` that can be updated with `setSoundPosition(instance, x, y, z)` and stopped with `stopSound(instance)`. This requires voice slot ownership semantics and a new command type in the ring buffer.

---

## 6. Attenuation

### 6.1 Model: inverse distance clamped

This is the standard model used by OpenAL, miniaudio, FMOD, and most game engines. It produces natural-sounding falloff.

```
gain = minDistance / distance       (clamped to [0, 1])
gain = 0  when distance > maxDistance
```

### 6.2 Default distances

| Parameter | Default | Rationale |
|-----------|---------|-----------|
| `minDistance` | 1.0 | Within 1 unit, sound is at full volume. Prevents infinite gain at distance=0. |
| `maxDistance` | 100.0 | Beyond 100 units, sound is silent. Reasonable for most game scenes. |

### 6.3 Configuration

Global defaults can be changed via:

```cpp
void setSound3DMinDistance(float dist);  // clamps to [0.01, maxDist)
void setSound3DMaxDistance(float dist);  // clamps to (minDist, 10000.0]
```

Per-sound distance overrides are deferred (would require extending the PLAY_3D command or adding persistent handles).

---

## 7. Voice Pool

**No new pool.** 3D voices and 2D voices share the existing `Voice[MAX_AUDIO_VOICES]` pool (32 slots). The only difference is that 3D voices have `is3D = true` and carry position data.

Rationale:
- The voice pool is a fixed array with no heap allocation — adding a second pool would double memory for no benefit.
- 32 simultaneous voices is already generous for LEGACY-tier hardware.
- The `is3D` flag is a single byte per voice — negligible overhead.

---

## 8. ECS Integration

**None for this session.** Sounds are fire-and-forget function calls, not components. There is no `Audio3DSource` component or `AudioSystem` that runs per frame.

Future work: a component-based audio system where entities with an `AudioSource3D` component automatically play/update positioned sounds as the entity moves. This requires persistent sound handles (Section 5.3) and is deferred.

---

## 9. Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO | **No** | Audio subsystem does not support RETRO (unchanged from ADR-006). |
| LEGACY | **Yes** (primary) | All spatialization is CPU-side arithmetic in the mixer callback. No GPU involvement. No hardware HRTF. The per-voice cost is: 1 sqrt, 1 normalise, 1 cross product, 2 dot products, 4 multiplies — per callback invocation (~5ms of audio = ~220 frames at 44.1kHz). For 32 voices this is well under 1% of a single core on 2012-era hardware. |
| STANDARD | **Yes** | Identical to LEGACY. |
| MODERN | **Yes** | Identical to LEGACY. Future: could add optional HRTF via miniaudio's `ma_hrtf` if we ever switch to `ma_engine`. |

---

## 10. Security

**No new attack surface.** Specifically:

- **File I/O:** `playSound3D()` reuses the same `SoundHandle` returned by `loadSound()`. No new file loading paths. All path validation from ADR-006 (SEC-1 through SEC-6) applies unchanged.
- **External input:** The position floats (x, y, z) and volume are clamped/validated the same way as existing volume parameters (NaN/Inf handled via `clampVolume()` or equivalent float validation).
- **Network:** No network involvement. When networked audio is added (Phase 4), position data from network packets will need bounds validation — but that is a future concern.
- **Memory:** No new heap allocations. Voice position fields are inline in the existing fixed-size `Voice` struct.

---

## 11. Performance Constraints

| Constraint | How it is met |
|------------|---------------|
| No per-frame heap allocations | Listener update writes to a fixed struct. `playSound3D()` posts to the existing SPSC ring buffer (no alloc). |
| Listener update is O(1) | One write to the global `Listener` struct per frame. No iteration, no lookup. |
| `playSound3D()` matches `playSound()` cost | Same path: cache lookup, ring buffer post, fire-and-forget. The only addition is writing 3 floats + 1 bool to the voice slot. |
| Mixer overhead is bounded | Per-voice 3D cost: ~20 FLOPs per callback frame. 32 voices x 220 frames per 5ms callback = ~140K FLOPs per callback. Trivial on any LEGACY CPU. |
| No mutex contention on listener | Listener state is written by main thread, read by audio thread. Use a SeqLock or atomic snapshot (two 32-byte copies) to avoid mutex. |

---

## 12. Lua Bindings

### 12.1 Proposed API

```lua
-- Play a positioned sound at default volume (1.0)
ffe.playSound3D(path, x, y, z)

-- Play a positioned sound at specified volume
ffe.playSound3D(path, x, y, z, volume)

-- Manual listener override (suppresses auto-sync with camera)
-- fwdX/fwdY/fwdZ = listener forward direction
ffe.setListenerPosition(x, y, z, fwdX, fwdY, fwdZ)

-- Configure global attenuation range
ffe.setSound3DMinDistance(dist)
ffe.setSound3DMaxDistance(dist)
```

### 12.2 Notes

- `ffe.playSound3D` takes a **path** (string), not a SoundHandle. This matches the existing `ffe.playSound(path, volume)` Lua pattern where the scripting layer manages handle caching internally.
- `ffe.setListenerPosition` takes 6 floats. The up vector defaults to (0, 1, 0) — sufficient for all standard game orientations. A 9-float variant with explicit up can be added later if needed.
- No `ffe.updateListener()` — auto-sync is the default and happens automatically each frame. Manual override via `setListenerPosition` is opt-in.

---

## 13. Implementation Plan

### Files modified

| File | Changes |
|------|---------|
| `engine/audio/audio.h` | Add `playSound3D()`, `updateListener()`, `setListenerPosition()`, `setSound3DMinDistance/MaxDistance()` declarations. Add `Listener` struct (internal, but declared in header for `Application` to call `updateListener`). |
| `engine/audio/audio.cpp` | Add `Listener` to `AudioState`. Add `PLAY_3D` command to ring buffer enum. Add spatialization math to `audioCallback()` mixer loop for `is3D` voices. Implement new public functions. |
| `engine/core/application.cpp` | In the tick/render path, after camera update: read `Camera` from registry, compute forward direction, call `audio::updateListener()`. |
| `engine/scripting/script_engine.cpp` | Register `ffe.playSound3D`, `ffe.setListenerPosition`, `ffe.setSound3DMinDistance`, `ffe.setSound3DMaxDistance`. |

### Files added

| File | Purpose |
|------|---------|
| `tests/audio/test_3d_audio.cpp` | Unit tests: attenuation math, pan calculation, listener update, clamping. |

### Files NOT modified

- `engine/renderer/camera.h` — no changes needed; `Application` reads the Camera singleton directly.
- No new shaders, no new ECS components, no new CMake targets (tests added to existing audio test target).

---

## 14. Alternatives Considered

### 14.1 Switch to `ma_engine` / `ma_sound`

miniaudio's `ma_engine` provides built-in spatialization via `ma_sound_set_position()`. This would be the simplest possible approach IF we were starting fresh.

**Rejected because:**
- FFE's audio uses `ma_device` with a custom callback, SPSC ring buffer, and manual PCM mixing. Switching to `ma_engine` requires rewriting the entire audio subsystem.
- The custom mixer gives us full control over voice management, mixing order, and the lock-free command architecture.
- The spatialization math is straightforward (~30 lines of code in the mixer) and does not justify a full rewrite.

### 14.2 Per-sound distance parameters in `playSound3D()`

```cpp
void playSound3D(SoundHandle h, float x, float y, float z, float volume,
                 float minDist, float maxDist);
```

**Deferred.** Adds complexity to the ring buffer command and the Lua binding for a feature most games will not need (global defaults are sufficient for most scenes). Can be added later without breaking changes.

### 14.3 HRTF (Head-Related Transfer Function)

HRTF provides more realistic 3D audio by modelling how sound wraps around the head. miniaudio supports it via `ma_hrtf`.

**Deferred.** Requires `ma_engine` or significant DSP work. Simple stereo panning is sufficient for LEGACY-tier and covers 90% of game audio needs. HRTF can be added as a MODERN-tier optional enhancement.
