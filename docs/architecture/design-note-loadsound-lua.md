# Design Note: `ffe.loadSound` / `ffe.unloadSound` Lua Bindings

**Author:** architect
**Date:** 2026-03-06
**Status:** DRAFT

---

## 1. Decision Summary

Expose `ffe.loadSound(path)` and `ffe.unloadSound(handle)` as Lua bindings, following the exact pattern established by `ffe.loadTexture` / `ffe.unloadTexture` in `script_engine.cpp`. Audio files are small; synchronous (blocking) load is acceptable and consistent with `loadTexture`.

---

## 2. API Signatures

```lua
-- Load a WAV or OGG file. Returns an integer handle on success, nil on failure.
local sfx = ffe.loadSound("sounds/jump.wav")

-- Unload a previously loaded sound. No-op for invalid handles.
ffe.unloadSound(sfx)
```

### C++ binding signatures (inside `registerEcsBindings`)

```
ffe.loadSound(path: string) -> integer | nil
ffe.unloadSound(handle: integer) -> nothing
```

---

## 3. Security Constraints

All constraints match the `loadTexture` binding (ADR-005 / texture_loader pattern):

- **Type guard:** `lua_type(state, 1) != LUA_TSTRING` check before `lua_tostring` — reject number/boolean coercion (MEDIUM-1 pattern).
- **Path safety:** `isPathSafe(path)` runs before any syscall. The C++ `ffe::audio::loadSound(path, assetRoot)` performs `realpath()` + assetRoot prefix check internally, but the Lua binding adds the script-side guard first.
- **No script-controlled asset root:** Call the single-argument overload (or pass the engine's internal asset root). Scripts must not control the asset root (LOW-5 pattern).
- **Handle range validation (unloadSound):** Reject `rawHandle <= 0` or `rawHandle > UINT32_MAX` before casting to `SoundHandle` (LOW-2 pattern).

---

## 4. Error Handling

| Condition | Behaviour |
|---|---|
| Argument is not a string | `FFE_LOG_ERROR`, return `nil` |
| Path fails `isPathSafe` | Rejected by C++ `loadSound` internally, return `nil` |
| File not found / decode failure | `FFE_LOG_ERROR` from C++ audio layer, return `nil` |
| `unloadSound` with handle 0 or negative | `FFE_LOG_ERROR`, no-op |
| `unloadSound` with already-freed handle | No-op (C++ `unloadSound` is idempotent for invalid handles) |

---

## 5. Thread Safety

`ffe.loadSound` and `ffe.unloadSound` are main-thread-only operations. They are called from Lua script execution, which always runs on the main thread. They must never be called from the miniaudio callback thread. The C++ `ffe::audio::loadSound` / `unloadSound` functions already enforce this contract (documented in `audio.h`).

---

## 6. Not In Scope

- Streaming load (async) -- not needed; audio files are small and load at scene init
- `ffe.playSound(handle, volume)` binding -- separate design note (trivial addition, same handle pattern)
- Per-frame audio calls -- `loadSound`/`unloadSound` are load-time only, never per-frame
