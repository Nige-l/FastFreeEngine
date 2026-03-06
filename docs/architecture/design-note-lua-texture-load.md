# Design Note: ffe.loadTexture(path) — Lua Texture Loading Binding

**Author:** architect
**Date:** 2026-03-06
**Status:** PROPOSED — awaiting security-auditor shift-left review before implementation begins
**Tiers:** LEGACY (primary), STANDARD, MODERN
**Security Review Required:** YES — this binding opens a controlled file I/O path from the scripting layer

---

## 1. Purpose

`ffe.loadTexture(path)` is a Lua binding that allows game scripts to load textures at scene load time without requiring C++ pre-loading. The target use case is a game that sets up entities and textures entirely from Lua — the expected pattern for session 5's interactive demo.

This binding is a thin wrapper over the existing `renderer::loadTexture(const char* path)` C++ API defined in `engine/renderer/texture_loader.h`. It does not introduce new loading logic; it exposes existing validated C++ behaviour to the Lua layer.

---

## 2. Decisions

### Decision 1: Asset Root — Use the Global C++ Root (Option A)

**Decision: Option A — the Lua binding calls the single-argument `renderer::loadTexture(path)` overload, which uses the global asset root already set by C++ via `renderer::setAssetRoot()`.**

Rationale:

- The asset root is a security boundary, not a game-logic concept. It belongs to C++ startup code (`Application::startup()`), not to untrusted scripts. Allowing scripts to set or override it defeats the containment the root provides.
- Option B (duplicating the root into Lua) creates two sources of truth and two code paths to audit. Any divergence between them is a latent security bug.
- Option C (`ffe.setTextureRoot(path)` callable from Lua) is worse than Option B: it gives scripts direct control over where files are read from. This is precisely the attack surface the write-once semantics in `setAssetRoot()` (LOW-2 from ADR-005-security-review.md) were designed to prevent.
- The Lua binding has no need to know what the asset root is. Its only contract with the caller is: "relative path in, handle out".
- C++ must call `renderer::setAssetRoot()` before any script that calls `ffe.loadTexture()` runs. This is already required by the C++ API. The init sequence does not change.

The binding is implemented by calling `renderer::loadTexture(path)` directly. The asset root is not a Lua-visible concept.

### Decision 2: Return Value — Integer Handle

**Decision: Return the `u32` id field of the `rhi::TextureHandle` as a Lua integer. Return `nil` on failure.**

Rationale:

- `ffe.addSprite(entityId, texHandle, ...)` already accepts a texture handle as an integer. The integer convention is established; changing to userdata would require changing `addSprite` or creating an inconsistency in the API.
- Lua userdata has no benefit here: it would require a metatable, `__gc` for automatic unloading, and `__tostring` for debugging. That is a meaningful implementation surface for a type the binding layer never needs to inspect — the handle is always consumed immediately by `addSprite`.
- Integers are trivially passed across function boundaries in Lua, stored in tables, and compared for validity (`handle ~= nil`).
- The integer representation is already documented in `.context.md` as the correct way to hold texture handles from Lua (see the "Do not hold texture handles across scene transitions" warning, and the `addSprite` parameter documentation).

Range: the returned integer is the `u32 id` value, which is always in `[1, UINT32_MAX]` on success (0 is the null sentinel, never returned on success). This is consistent with the `addSprite` binding's validation, which rejects handles `<= 0`.

### Decision 3: ffe.unloadTexture(handle) — Include in This Session

**Decision: Implement `ffe.unloadTexture(handle)` in the same session as `ffe.loadTexture(path)`.**

Rationale:

- Without `ffe.unloadTexture`, Lua-loaded textures cannot be freed until `ScriptEngine::shutdown()`. This means a scene transition implemented in Lua would leak GPU memory on every transition. Even if no scene transitions exist today, designing the API this way from the start makes the omission a known gap that game-dev-tester will encounter immediately.
- `unloadTexture` is a single call to `renderer::unloadTexture(handle)` after validating the integer argument. The implementation cost is minimal.
- The `.context.md` already documents that callers own handles and must call `unloadTexture` when done. Completing this contract from Lua closes a documentation gap.
- Pairing load and unload prevents the `security-auditor` from flagging an obvious lifetime management gap in the post-implementation review.

The binding validates the integer before calling C++: it rejects `<= 0` and `> UINT32_MAX` values. Safe to call with a previously-unloaded handle — `renderer::unloadTexture` with id=0 is already specified as a no-op in `texture_loader.h`.

### Decision 4: Thread Safety — Safe for Main Thread Use

**Decision: The binding calls `renderer::loadTexture()` from the main thread. This is safe and requires no additional mechanism.**

`renderer::loadTexture()` is explicitly documented as single-threaded and main-thread-only. The scripting system's update tick runs on the main thread. The RHI is main-thread-only. For LEGACY tier, there is no multi-threaded rendering. This is not a concern at the current architecture level.

If STANDARD or MODERN tier adds a separate render thread in a future session, this binding (and all renderer bindings) will need a re-evaluation. That is an `architect` decision for that session, not this one.

### Decision 5: Security — Existing Validation Is Sufficient; No Additional Layer Required

**Decision: The Lua binding forwards the path string directly to `renderer::loadTexture(path)`, which already runs the full validated path check sequence. No additional validation is needed in the binding layer.**

The existing C++ path is:

1. `isPathSafe(path)` — null, empty, absolute, drive letter, `../` traversal, `strnlen` bounds check (MEDIUM-3)
2. Construct `assetRoot + "/" + path` with overflow check (HIGH-2)
3. `realpath()` to canonicalise, then prefix-check against asset root
4. `stat()` pre-decode file-size check against `MAX_TEXTURE_FILE_BYTES` (MEDIUM-2)
5. `stbi_load()` with `STBI_MAX_DIMENSIONS 8192` (MEDIUM-2)
6. SEC-4 output validation before SEC-3 size multiplication (HIGH-1)

The Lua binding receives an untrusted string from a Lua script. That string is passed to `renderer::loadTexture()`, which treats it as untrusted input — path validation is the first operation. The binding does not need to duplicate any of these checks. Doing so would create a second validation layer that diverges over time.

The key security property: scripts cannot read outside the asset root, cannot influence the asset root, and cannot cause stb_image to process malicious content that has not already passed the C++ validation gauntlet. The sandbox blocking `io`, `os`, and `package` ensures scripts have no alternative file access path.

One property worth noting for the `security-auditor` review: this binding creates a supervised file I/O path from Lua, which is intentional and acceptable. The supervision is the existing `renderer::loadTexture()` implementation. The binding does not bypass sandbox controls — it adds a new validated capability, consistent with how `api-designer` recommends extending the sandbox (see `.context.md`, "Do not try to work around the sandbox" section).

### Decision 6: Error Signalling — nil on Failure

**Decision: Return `nil` on failure. Never throw a Lua error.**

This is consistent with every other `ffe.*` binding. Scripts must check the return value before passing the handle to `addSprite`. The C++ side already logs a specific error message via `FFE_LOG_ERROR` identifying the failure reason (path rejected, file not found, decode failed, GPU upload failed). Scripts do not need to re-read the error — they only need to handle nil.

---

## 3. Proposed Lua API Surface

```lua
-- ffe.loadTexture(path) -> integer or nil
--
-- Load an image file from the engine's configured asset root.
-- path must be a relative path with no traversal sequences (no leading '/', no '../').
-- Returns an integer texture handle (the u32 id of an rhi::TextureHandle) on success.
-- Returns nil on any failure. The failure reason is logged at ERROR level.
--
-- IMPORTANT: This is a scene-load-time operation. It performs file I/O and a GPU upload.
-- Do NOT call inside an update function, per-entity loop, or per-frame callback.
-- Calling this per-frame is a performance violation.
--
-- The returned handle is valid until ffe.unloadTexture(handle) is called.
-- Do not hold handles across scene transitions where C++ may unload them.
local handle = ffe.loadTexture("sprites/player.png")

-- ffe.unloadTexture(handle) -> nothing
--
-- Destroy the GPU texture associated with the handle.
-- Safe to call with nil or an already-unloaded handle (no-op in those cases).
-- After this call, the handle integer must not be passed to addSprite or any other binding.
ffe.unloadTexture(handle)
```

Full usage sequence:

```lua
-- At scene load time (not per-frame):
local playerTex = ffe.loadTexture("sprites/player.png")
if playerTex == nil then
    ffe.log("Failed to load player texture -- cannot spawn player")
    return
end

local id = ffe.createEntity()
if id == nil then return end

ffe.addTransform(id, 100.0, 200.0, 0.0, 1.0, 1.0)
ffe.addSprite(id, playerTex, 32.0, 32.0, 1.0, 1.0, 1.0, 1.0, 0)
ffe.addPreviousTransform(id)

-- At scene teardown time:
-- (Destroy entities that reference playerTex before unloading.)
ffe.destroyEntity(id)
ffe.unloadTexture(playerTex)
playerTex = nil  -- prevent accidental reuse
```

### C++ Init Sequence (Required)

`renderer::setAssetRoot()` must be called from C++ before any script calls `ffe.loadTexture()`. If `setAssetRoot()` was not called, `renderer::loadTexture()` returns `TextureHandle{0}` and logs an error (HIGH-3 condition from ADR-005-security-review.md). The Lua binding will return nil. The game will not crash; it will log the error and the script will handle nil.

---

## 4. Security Properties for security-auditor Review

The following properties must be verified in the post-implementation review:

**P1 — No asset root control from Lua.** The binding calls the single-argument `renderer::loadTexture(path)` overload only. The asset root is not exposed to Lua in any form. The `ffe` table has no `setAssetRoot` or `setTextureRoot` binding.

**P2 — Path forwarded without modification.** The binding retrieves the Lua string argument with `lua_tostring` (or `luaL_checkstring`) and passes it directly to `renderer::loadTexture()`. It does not pre-process, truncate, or concatenate the string. The C++ validation layer receives exactly what the script provided.

**P3 — Nil on empty or nil argument.** If the Lua script passes `nil` or an empty string as the path argument, the binding returns nil immediately (via the C++ rejection in `isPathSafe()`). The binding must not crash on a nil Lua argument; use `lua_tostring` which returns nullptr for nil, and guard before calling into C++.

**P4 — Return value is bounded integer or nil.** The binding never returns a Lua integer outside `[1, UINT32_MAX]` on success. The returned integer is the `u32 id` field of the `rhi::TextureHandle`; it cannot be 0 on success (0 is the null sentinel, and `renderer::loadTexture()` returns `TextureHandle{0}` only on failure, which the binding converts to nil).

**P5 — unloadTexture validates before calling C++.** The `ffe.unloadTexture` binding rejects `<= 0` and `> UINT32_MAX` integer arguments as no-ops, consistent with `addSprite` validation. Passing nil is also a no-op. No unchecked integer is passed to `renderer::unloadTexture()`.

**P6 — No instruction budget bypass.** The binding is a C function (registered with `lua_pushcfunction`). It does not call back into the Lua VM. The instruction budget hook is not affected.

**P7 — No sandbox escape.** The binding does not open new C++ capabilities that are not already mediated by `renderer::loadTexture()`. Scripts cannot use this binding to read arbitrary files — the asset root prefix check in `renderer::loadTexture()` enforces the boundary. The existing `io`, `os`, `package`, and `ffi` blocks remain in place.

---

## 5. Implementation Notes for engine-dev

- Add the binding in `registerEcsBindings()` in `engine/scripting/script_engine.cpp`, alongside the existing `ffe.*` bindings.
- Include `renderer/texture_loader.h` in `script_engine.cpp` (it is already included for the render system; confirm this is sufficient or add explicitly).
- Use `luaL_optstring(state, 1, nullptr)` or check `lua_type(state, 1) == LUA_TSTRING` before calling `lua_tostring`, to handle nil arguments gracefully without a Lua error.
- The binding must NOT call the two-argument `renderer::loadTexture(path, assetRoot)` overload. Only the single-argument overload. The asset root is not a Lua-visible parameter.
- `ffe.unloadTexture(nil)` must be a silent no-op. Use `lua_isnil(state, 1)` check before `luaL_checkinteger`.

---

## 6. Deferred Items

The following are out of scope for this session:

**Texture params from Lua (filter, wrap mode).** The `TextureLoadParams` struct (`NEAREST`/`LINEAR`, `CLAMP_TO_EDGE`/`REPEAT`) would allow Lua scripts to control texture sampling. Deferred because: the default params (`LINEAR`, `CLAMP_TO_EDGE`) are correct for most 2D sprites; adding these parameters would require exposing enum constants from `rhi_types.h` as Lua integer constants, which is an `api-designer` decision and a separate binding design. Recommend deferring to a session after the basic load/unload cycle is validated by `game-dev-tester`.

**Async texture loading.** `renderer::loadTexture()` is synchronous. Async loading (load on a worker thread, callback on main thread) is a STANDARD/MODERN tier concern. Not relevant for LEGACY tier. Deferred indefinitely for this tier.

**Texture handle introspection from Lua.** No `ffe.getTextureWidth(handle)` or similar introspection binding. Scripts do not need to query texture metadata; they set sprite dimensions explicitly in `addSprite`. Deferred unless a concrete use case arises.

**Texture caching / deduplication.** If a script calls `ffe.loadTexture("sprites/player.png")` twice, two GPU textures are created. A texture cache (path → handle) would prevent this. Deferred to the asset manager design, which is a future ADR. For now, game devs should store the returned handle in a Lua variable and not call `loadTexture` for the same path twice.

---

*This design note is complete. security-auditor should review Section 4 (Security Properties) before implementation begins. engine-dev should not proceed until the shift-left review clears.*
