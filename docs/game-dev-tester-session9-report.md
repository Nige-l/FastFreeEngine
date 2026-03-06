# Game-Dev Tester Usage Report — Session 9

**Author:** game-dev-tester agent
**Date:** 2026-03-06
**Session:** 9
**Features under test:** C++ audio subsystem (`ffe::audio`), Lua texture lifecycle bindings (`ffe.loadTexture`, `ffe.unloadTexture`)

---

## Summary

Two new feature surfaces were evaluated this session:

1. The `ffe::audio` C++ subsystem — a global-singleton audio mixer backed by miniaudio, supporting WAV and OGG, with a LEGACY-tier budget of 32 simultaneous voices.
2. Two new Lua bindings — `ffe.loadTexture(path)` and `ffe.unloadTexture(handle)` — which let Lua scripts load and unload GPU textures using the renderer's global asset root.

The audio API is well-structured and makes sense for a game scenario. The Lua texture API works in principle and has been exercised in the lua_demo. One important friction point was found regarding `ffe.loadTexture` in the existing demo setup.

---

## 1. C++ Audio API

### Scenario Under Evaluation

Imagine a 2D dungeon crawl: a player walks through corridors, attacks enemies, and picks up items. The sound events are:

- `footstep.wav` — plays every N steps (game-event boundary, not per-frame)
- `sword_swing.ogg` — plays on attack input
- `coin_pickup.wav` — plays on overlap with item entities
- `ambient_cave_loop.ogg` — would require looping, which is outside the current API scope

### Init / Shutdown

```cpp
// In Application startup:
if (!ffe::audio::init()) {
    // Graceful degradation — engine continues without sound
    FFE_LOG_WARN("Game", "Audio unavailable on this device; muted");
}

// In Application teardown:
ffe::audio::shutdown();
```

The `headless` parameter default (`false`) means production code needs no extra argument. The bool return and graceful degradation path are exactly right: on machines with no audio device (CI, servers, certain Linux configs) the engine continues without crashing. This is a deliberate accessibility concern given the LEGACY-tier focus.

Calling `init()` twice logs a warning and returns false — this is safe and the behaviour is clearly documented.

### Load / Unload

```cpp
// At scene load time (NOT per-frame):
const char* AUDIO_ROOT = "/path/to/game/assets/audio";

ffe::audio::SoundHandle swordSwing = ffe::audio::loadSound("sword_swing.ogg", AUDIO_ROOT);
ffe::audio::SoundHandle footstep   = ffe::audio::loadSound("footstep.wav",    AUDIO_ROOT);

if (!ffe::audio::isValid(swordSwing)) {
    FFE_LOG_WARN("Game", "sword_swing.ogg failed to load");
}

// At scene teardown:
ffe::audio::unloadSound(swordSwing);
ffe::audio::unloadSound(footstep);
```

The `assetRoot` + `path` pattern is consistent with `renderer::loadTexture(path, assetRoot)` — a developer already familiar with the renderer will find this natural. The `isValid()` helper mirrors `rhi::isValid()`, maintaining API consistency across the engine.

**Security design observation:** Path traversal checks run before any syscall — this follows the shift-left security principle from the FFE constitution and matches what the texture loader does. File size caps (`AUDIO_MAX_FILE_BYTES = 32 MB`, `AUDIO_MAX_DECODED_BYTES = 10 MB`) are appropriate for a LEGACY-tier target.

**Limitation to note:** The current API is fire-and-forget — there is no way to stop an individual voice after `playSound()` returns. This is acceptable for short SFX but rules out the ambient cave loop scenario above. This is an expected scope limitation, not a bug; it should be documented as a known constraint in the `.context.md`.

### Playback and Volume

```cpp
// On attack input (game-event boundary):
ffe::audio::playSound(swordSwing, 0.9f);

// On coin pickup:
ffe::audio::playSound(coinPickup); // defaults to volume 1.0f

// Volume slider in options menu:
ffe::audio::setMasterVolume(userPreference);
```

The per-instance `volume` parameter on `playSound()` (range [0.0, 1.0], clamped, NaN-safe) is a useful addition. It allows quieter ambient variants of the same sound asset without duplicating the PCM buffer.

`getMasterVolume()` rounds out the API for a settings screen — read the current value to initialise the slider widget.

`getActiveVoiceCount()` is helpful for debugging voice exhaustion. The 32-voice cap (LEGACY budget) means a dense battle scene could hit it; the API logs a warning and drops the voice, which is the right behaviour (no crash, no corruption).

### API Discoverability Score: 8/10

The API maps directly onto how audio works in practice. `init` → `loadSound` → `playSound` → `shutdown` reads like a tutorial. Minor deductions:

- The `assetRoot` parameter being required on `loadSound` is slightly verbose compared to a `setAudioRoot()` pattern (though this is consistent with the current renderer approach and arguably more explicit).
- There is no `setAudioRoot()` / single-argument `loadSound(path)` convenience overload, which means Lua bindings for audio (if added in a future session) would need to handle the root themselves or use the global-root pattern from the texture loader.
- The absence of loop/stop/pause is noticeable for anyone building anything beyond SFX. It is clearly in scope for a future session.

### Blockers: None

The API is complete for its declared scope (one-shot SFX, volume control, headless mode). No blockers.

---

## 2. Lua Texture API

### API Under Evaluation

```lua
-- Load a texture at script-init time:
local handle = ffe.loadTexture("sprites/player.png")
if handle == nil then
    ffe.log("WARNING: failed to load player texture")
end

-- Use the handle when attaching a sprite:
local entity = ffe.createEntity()
ffe.addTransform(entity, 0.0, 0.0, 0.0, 1.0, 1.0)
ffe.addSprite(entity, handle, 48.0, 48.0, 1.0, 1.0, 1.0, 1.0, 5)

-- Unload when the scene is torn down:
-- (no Lua shutdown callback exists yet — see friction points)
ffe.unloadTexture(handle)
```

### Does the API Feel Natural?

Yes, mostly. The flow `loadTexture` → `addSprite(handle)` → `unloadTexture` is the correct three-step lifecycle, and it mirrors the mental model a developer coming from Unity (`Resources.Load`, assigning to a component, `Resources.UnloadAsset`) would expect.

The nil-on-failure return from `loadTexture` is appropriate. Lua developers are accustomed to nil as a sentinel. The integer handle returned on success feeds directly into `ffe.addSprite`'s second argument with no type conversion needed.

The type guard in the binding (rejecting non-string arguments with a log message rather than coercing silently) is a good developer experience choice — it gives a meaningful error instead of a mysterious texture load failure.

### Friction Points

**Friction 1 (Significant): `ffe.loadTexture` depends on a global asset root set from C++, but the lua_demo does not call `renderer::setAssetRoot()`.**

The `ffe.loadTexture` binding calls the single-argument `renderer::loadTexture(path)` overload, which requires `renderer::setAssetRoot()` to have been called first. In the lua_demo's `main.cpp`, texture loading is done using the two-argument overload (`loadTexture("checkerboard.png", ASSET_ROOT)`) directly in C++, which bypasses the global asset root entirely. The global asset root is never set.

This means `ffe.loadTexture("checkerboard.png")` called from game.lua would fail silently with a nil return, because no asset root has been registered with the texture loader's global root slot.

**Resolution applied:** The game.lua update (see section 3) guards `ffe.loadTexture` with a nil check and falls back to the stub handle `1` if the load fails, so the demo does not break. The underlying setup gap should be fixed in a future session: either `lua_demo/main.cpp` should call `renderer::setAssetRoot(ASSET_ROOT)` before the system runs, or the Lua binding should be extended with a two-argument overload `ffe.loadTexture(path, assetRoot)` for cases where no global root is set.

**Friction 2 (Minor): No Lua shutdown callback for `ffe.unloadTexture`.**

The ScriptEngine has no `onShutdown()` callback. There is no way for a Lua script to call `ffe.unloadTexture` at clean shutdown time. For the lua_demo this is acceptable (the process exits and the driver reclaims resources), but for a real game with scene transitions, this becomes a leak path. A future session should add an optional `shutdown()` Lua function that the ScriptEngine calls before `lua_close`.

**Friction 3 (Minor): No `ffe.isTextureValid(handle)` introspection.**

After `ffe.loadTexture` returns an integer handle, there is no way from Lua to check whether that integer is still a live texture (e.g., whether `ffe.unloadTexture` has already been called on it). The C++ side has `rhi::isValid()` but it checks only whether `handle.id != 0`, not whether the texture has been destroyed. This is consistent with how the C++ API works, but Lua developers are more likely to pass stale handles accidentally. A guard like `ffe.isTextureLoaded(handle)` could help.

### API Discoverability Score: 7/10

The load/unload symmetry is clear and the nil-on-failure convention is Lua-idiomatic. The score is held back by:

- The invisible dependency on `setAssetRoot()` having been called from C++ (not discoverable from Lua at all)
- No shutdown hook meaning responsible resource management requires C++ cooperation
- No introspection function for the returned integer handle

---

## 3. lua_demo/game.lua Update

The game.lua has been updated to demonstrate `ffe.loadTexture` in a meaningful way. Key changes:

- A module-level `textureHandle` variable is initialised to `nil`.
- On the first update tick (guarded by `textureHandle == nil`), `ffe.loadTexture("checkerboard.png")` is called.
- If the load succeeds, the returned integer handle replaces the stub constant `FOLLOWER_TEX = 1` for the follower sprite.
- If the load fails (e.g., because the asset root was not set in C++), a warning is logged and the follower falls back to `FOLLOWER_TEX = 1` (the existing headless-safe stub).
- `ffe.unloadTexture` is NOT called at demo end because no shutdown callback exists (see Friction 2 above). A comment documents this gap explicitly.

The load is called exactly once — the nil guard ensures it does not fire again on subsequent ticks.

---

## 4. Overall Status

| Feature | Status | Blockers |
|---|---|---|
| `ffe::audio` C++ API | Ready for integration | None |
| `ffe.loadTexture` Lua binding | Functional with caveats | Friction 1 (asset root gap) |
| `ffe.unloadTexture` Lua binding | Functional | No shutdown hook (Friction 2) |
| lua_demo updated | Done | None |

**Recommended follow-up tasks for a future session:**

1. `lua_demo/main.cpp`: call `renderer::setAssetRoot(ASSET_ROOT)` so that `ffe.loadTexture` works without the C++ two-argument workaround.
2. ScriptEngine: add an optional `shutdown()` Lua function call in `ScriptEngine::shutdown()` before `lua_close`.
3. Audio Lua bindings: bind `ffe::audio::loadSound`, `ffe::audio::playSound`, `ffe::audio::setMasterVolume` to `ffe.*` for the next session.
4. Document the no-loop/no-stop constraint in `engine/audio/.context.md`.
