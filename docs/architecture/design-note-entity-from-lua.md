# Design Note: Entity Lifecycle Management from Lua

**Author:** architect
**Date:** 2026-03-06
**Status:** AWAITING security-auditor review — implementation blocked until review completes
**Security Review Required:** YES — this design adds ECS mutation and renderer component access from Lua (CLAUDE.md Section 5, attack surface: external input)

---

## Purpose

This note covers the design decisions for the following new Lua bindings:

- `ffe.createEntity()` → entity ID integer
- `ffe.destroyEntity(entityId)`
- `ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY)` → bool
- `ffe.addPreviousTransform(entityId)` → bool
- `ffe.addSprite(entityId, texHandle, width, height, r, g, b, a, layer)` → bool
- `ffe.loadTexture(path)` → texture handle integer (deferred — see Section 8)

These bindings give Lua scripts the ability to create and configure entities at runtime. This is new territory: existing bindings are read-only queries (input, transform read) or single write operations on pre-existing entities (setTransform). These bindings allow scripts to modify the structure of the ECS world.

`security-auditor` must review this document before `engine-dev` begins implementation.

---

## 1. Entity ID Representation in Lua

**Decision:** Pass entity IDs as `lua_Integer`. On the C++ side, validate that the received integer is in the range `[0, UINT32_MAX - 1]` before casting to `EntityId`.

**Rationale:**

`EntityId` is `u32` (defined in `engine/core/types.h`). On a 64-bit platform with LuaJIT, `lua_Integer` is 64 bits (`ptrdiff_t` on LuaJIT x86-64, which is 64-bit). The `u32` range fits entirely within a 64-bit `lua_Integer` with no information loss in either direction.

The risk is the return journey: a script can store an ID, manipulate it with arithmetic (Lua has no integer overflow protection), and pass back a value that was never a valid entity ID. The guard on the C++ side is:

```
lua_Integer rawId = lua_tointeger(state, argIndex);
if (rawId < 0 || rawId > static_cast<lua_Integer>(UINT32_MAX - 1)) {
    // reject
}
EntityId id = static_cast<EntityId>(rawId);
if (!world->isValid(id)) {
    // reject
}
```

The `UINT32_MAX - 1` upper bound reserves the maximum `u32` value. `NULL_ENTITY` in FFE is `~EntityId{0}` which equals `UINT32_MAX`. Rejecting `UINT32_MAX` from Lua prevents a script from constructing a null entity sentinel via arithmetic and passing it through.

EnTT entity IDs encode a version counter in the upper 12 bits. `isValid()` checks both the index and the version, so a recycled (previously destroyed) ID with a stale version fails the validity check. No additional generation tracking is required in the binding layer.

**NULL_ENTITY sentinel:** Do not expose `NULL_ENTITY` to Lua. Return `nil` from `createEntity` on failure rather than a sentinel integer. Scripts already check for nil on every existing binding that can fail; this is consistent.

---

## 2. Entity Ownership

**Decision:** No ownership tracking. Both Lua and C++ code can create and destroy any entity the World considers valid. A Lua script may destroy an entity created by C++.

**Rationale:**

Introducing an ownership table (tracking which entities were created by Lua versus C++) adds complexity with marginal benefit. The existing boundary — `world->isValid(id)` — already protects against the primary hazard: using a stale ID after the entity was destroyed.

The real concern is intentional misuse: a script destroys a critical engine-managed entity (e.g., a camera or player entity that C++ systems hold an ID to). This is a game design problem, not a security problem. The scripting sandbox threat model covers OS-level attacks and memory safety, not preventing scripts from doing things that are valid operations in the ECS but bad for the game's logic.

**Consequence:** C++ code that holds EntityId values and registers them with the scripting engine must be prepared for those entities to be destroyed by scripts. If a C++ system holds a cached EntityId, it must call `world->isValid()` before every use once Lua lifecycle bindings are available. This must be documented.

**The C++ side does not need a destroy hook or notification system for this session.** If gameplay systems need destroy callbacks in the future, that is a separate design effort.

---

## 3. Texture Handle Exposure to Lua

**Decision:** Expose texture handles as opaque integers — specifically, the `u32 id` field of `rhi::TextureHandle`. Return the integer from `ffe.loadTexture()`. Receive it back in `ffe.addSprite()` and validate it before use.

**Three options considered:**

**(a) Opaque integer (chosen):** Return `static_cast<lua_Integer>(handle.id)` from `loadTexture`. Receive it as `lua_Integer` in `addSprite`, validate `> 0` and `<= UINT32_MAX`, cast to `u32`, reconstruct `rhi::TextureHandle{id}`. The C++ side cannot verify that the handle is still live — it is the caller's responsibility not to use a destroyed handle.

**(b) Lua userdata:** Would allow attaching a metatable and type-checking on receipt. Overkill for a single u32 field. Adds GC pressure (userdata is heap-allocated per Lua GC) and would require `rawset`/`rawget` which are blocked by the sandbox. Rejected.

**(c) Pre-loaded handles passed from C++:** Requires C++ to pre-load all textures and register their handles in the Lua state. This removes `ffe.loadTexture` entirely. Acceptable as a restriction but limits flexibility for games that want to load textures conditionally at runtime. Deferred as a future option if `ffe.loadTexture` proves unsafe.

**Validation on the `addSprite` C++ side:**

```
lua_Integer rawHandle = luaL_checkinteger(state, 2);
if (rawHandle <= 0 || rawHandle > static_cast<lua_Integer>(UINT32_MAX)) {
    // reject — push false, return
}
rhi::TextureHandle texHandle{ static_cast<u32>(rawHandle) };
// rhi::isValid(texHandle) checks id != 0 — this is already guaranteed by rawHandle > 0
// but we check anyway for defence in depth
if (!rhi::isValid(texHandle)) {
    // reject
}
```

The binding does not validate that the handle refers to a currently live GPU texture. The RHI does not expose a validity-by-id query that is safe to call from a Lua binding context. If a script calls `addSprite` with a handle from a destroyed texture, the resulting Sprite component holds a dangling handle — rendering it produces undefined GPU behaviour. This risk is accepted. Documentation must state: destroy texture handles only after removing all Sprite components that reference them.

---

## 4. addTransform — Duplicate Component Behaviour

**Decision:** If the entity already has a Transform, log a warning and overwrite. Return `true`.

**Alternatives considered:**

- **Silent no-op:** Surprising and hard to debug. A script that calls `addTransform` twice gets no feedback that the second call did nothing.
- **Return false:** Callers must check the return value. Most scripts will not, and the failure is silent.
- **Lua error (luaL_error):** Breaks the existing convention; all current bindings use nil/false for errors rather than throwing. Rejected for consistency.
- **Log + overwrite (chosen):** Matches the least-surprise principle for the target audience. A student who accidentally calls `addTransform` twice sees a log message pointing to the bug. The entity is still in a valid state.

The warning log message should include the entity ID:

```
FFE_LOG_WARN("ScriptEngine", "addTransform: entity %u already has Transform — overwriting", entityId);
```

The same policy applies to `addSprite` and `addPreviousTransform`.

---

## 5. ffe.loadTexture from Lua — Thread Safety and Path Validation

**Decision:** Defer `ffe.loadTexture` to a future session. Do not implement it in this session.

**Rationale:**

`ffe::renderer::loadTexture()` (designed in ADR-005) is explicitly documented as a non-hot-path, main-thread-only function that performs synchronous file I/O, heap allocation, and GPU upload. Calling it from a Lua binding creates several layered problems:

1. **Thread safety:** The scripting engine runs on the main thread and `callFunction` is synchronous, so threading is not the issue. The issue is timing: `loadTexture` invokes the RHI (OpenGL calls), which requires a valid GL context on the current thread. This is true during normal script execution in the main loop, but is not guaranteed in all contexts where `doString`/`doFile` might be called (e.g., an editor thread, a loading screen with a deferred GL context). The binding cannot enforce this invariant from inside Lua.

2. **Path validation:** ADR-005 specifies a strict path validation and `realpath()` canonicalisation path. That same validation must run if `loadTexture` is called from Lua. The scripting engine already has `isPathSafe()` for `doFile`. The texture loader has its own `isPathSafe()` and a separately configured asset root. These two asset roots may differ (scripts vs textures). A Lua binding for `loadTexture` would need to use the texture loader's asset root, not the script loader's. This cross-system coupling is better designed explicitly than patched in.

3. **Security surface:** `ffe.loadTexture` from Lua would give scripts the ability to trigger file I/O with a caller-controlled path. Even with path validation, this is a significant expansion of the scripting attack surface that deserves its own security-auditor review after ADR-005 is fully implemented and reviewed.

**For this session:** C++ code pre-loads textures and passes their handle integer IDs to Lua via `callFunction` arguments or by writing them into a Lua global at startup. This is already possible and does not require any new binding.

---

## 6. addPreviousTransform — Default Value

**Decision:** `addPreviousTransform` copies the entity's current Transform into the newly added PreviousTransform component. If the entity has no Transform, it initialises PreviousTransform to `{position={0,0,0}, scale={1,1,1}, rotation=0}` and logs a warning.

**Rationale:**

The purpose of `PreviousTransform` is to provide the render interpolation system with a stable previous-frame snapshot. If no Transform exists, there is nothing sensible to copy. Using the zero-position/unit-scale default is safe — it will not crash anything — but an entity that has PreviousTransform without Transform is almost certainly a bug. The warning makes this discoverable.

The better default would be to require that `addTransform` is called before `addPreviousTransform`. This is documented as a usage convention but not enforced by the binding (which would require returning an error, inconsistent with other bindings). The log warning is the enforcement mechanism.

**Implementation sketch:**

```
// C++ binding body for addPreviousTransform:
if (!world->isValid(entityId)) { push false, return }
if (world->hasComponent<PreviousTransform>(entityId)) {
    log warning "overwriting existing PreviousTransform on entity %u"
}
if (world->hasComponent<Transform>(entityId)) {
    const Transform& t = world->getComponent<Transform>(entityId);
    world->addComponent<PreviousTransform>(entityId,
        PreviousTransform{t.position, t.scale, t.rotation});
} else {
    log warning "addPreviousTransform on entity %u that has no Transform"
    world->addComponent<PreviousTransform>(entityId, PreviousTransform{});
}
push true, return 1
```

Note: `world->addComponent<PreviousTransform>` calls `m_registry.emplace<PreviousTransform>`. On EnTT, `emplace` when the component already exists is undefined behaviour — it asserts in debug mode. The binding must call `removeComponent<PreviousTransform>` before the overwrite if the component already exists. Or use `emplace_or_replace` via the raw registry escape hatch. The implementation must handle this correctly.

---

## 7. Error Signalling Convention

**Decision:** Follow the existing convention: return `nil` or `false` on error, never throw a Lua error from these bindings.

**Existing convention summary (from script_engine.cpp):**
- `getTransform` returns `nil` on invalid entity or missing component
- `setTransform` returns `false` on non-finite value (and is a no-op), otherwise returns nothing
- Input queries return `false` on out-of-range key codes

**New bindings:**

| Function | Success return | Failure return | Notes |
|----------|---------------|----------------|-------|
| `createEntity()` | integer (the entity ID) | `nil` | Failure means World is nil or registry is full |
| `destroyEntity(id)` | nothing (0 return values) | nothing (0 return values) | No-op on invalid ID; never errors |
| `addTransform(...)` | `true` | `false` | false = no World registered; invalid entity; non-finite values |
| `addPreviousTransform(id)` | `true` | `false` | false = no World; invalid entity |
| `addSprite(...)` | `true` | `false` | false = no World; invalid entity; invalid tex handle; non-finite dimensions or color |

Lua scripts should check return values on the creation path. Example idiomatic usage:

```lua
local id = ffe.createEntity()
if id == nil then
    ffe.log("createEntity failed")
    return
end
local ok = ffe.addTransform(id, 100, 200, 0, 1, 1)
if not ok then
    ffe.log("addTransform failed on entity " .. tostring(id))
end
```

---

## 8. Implementation Sketches

All bindings follow the same structure used in `registerEcsBindings()`: lambda pushed with `lua_pushcfunction`, set into the `ffe` table with `lua_setfield`. World pointer retrieved from `LUA_REGISTRYINDEX` via `s_worldRegistryKey`.

### ffe.createEntity()

```
// 1. Retrieve World from registry. Return nil if not set.
// 2. Call world->createEntity() — returns EntityId (u32).
// 3. Push static_cast<lua_Integer>(entityId).
// 4. Return 1.
```

No arguments. No failure modes beyond a null World pointer. EnTT `m_registry.create()` can return `entt::null` if the entity count is at the platform limit (~1M with default entity type). Add a check:

```
EntityId id = world->createEntity();
if (id == NULL_ENTITY) {
    lua_pushnil(state);
    return 1;
}
```

### ffe.destroyEntity(entityId)

```
// 1. Retrieve World. If nil, return 0 (no-op).
// 2. Validate entityId: rawId < 0 || rawId > UINT32_MAX-1 → return 0 (no-op).
// 3. world->isValid(id) → if false, return 0 (no-op — already gone).
// 4. world->destroyEntity(id).
// 5. Return 0.
```

No return value. No error signalling — destroying an invalid entity is a no-op, not an error. Logging a warning on double-destroy is optional; the performance-critic may flag it if it appears in hot paths.

### ffe.addTransform(entityId, x, y, rotation, scaleX, scaleY)

```
// 1. Retrieve World. Return false if nil.
// 2. Validate entityId. Return false if invalid.
// 3. Read 5 numbers with luaL_checknumber.
// 4. Reject non-finite values (std::isfinite check on all 5). Return false if any non-finite.
// 5. If hasComponent<Transform>(id): log warning, removeComponent<Transform>(id).
// 6. addComponent<Transform>(id, Transform{
//        {x, y, 0.0f},        // position (z=0 for 2D)
//        {scaleX, scaleY, 1.0f},  // scale (scaleZ=1 for 2D)
//        rotation
//    });
// 7. Push true. Return 1.
```

Note on z and scaleZ: same convention as `setTransform` — z and scaleZ are not script-settable; they default to 0 and 1 respectively.

### ffe.addPreviousTransform(entityId)

```
// 1. Retrieve World. Return false if nil.
// 2. Validate entityId. Return false if invalid.
// 3. If hasComponent<PreviousTransform>(id): log warning, removeComponent<PreviousTransform>.
// 4. If hasComponent<Transform>(id):
//        const Transform& t = getComponent<Transform>(id);
//        addComponent<PreviousTransform>(id, PreviousTransform{t.position, t.scale, t.rotation});
//    Else:
//        log warning "entity has no Transform"
//        addComponent<PreviousTransform>(id, PreviousTransform{});
// 5. Push true. Return 1.
```

### ffe.addSprite(entityId, texHandle, width, height, r, g, b, a, layer)

```
// Arguments: (1) entityId [integer], (2) texHandle [integer], (3-4) width, height [number],
//            (5-8) r, g, b, a [number, 0.0..1.0], (9) layer [integer]
//
// 1. Retrieve World. Return false if nil.
// 2. Validate entityId. Return false if invalid.
// 3. Validate texHandle: rawHandle <= 0 || rawHandle > UINT32_MAX → return false.
// 4. Read width, height. Reject non-positive (width <= 0 || height <= 0) → return false.
//    Reject non-finite → return false.
// 5. Read r, g, b, a. Reject non-finite → return false.
//    Clamp r, g, b, a to [0.0, 1.0] — do not reject; clamping is more friendly for scripted animations.
//    (Note: clamping vs rejecting is a design call — see Q below.)
// 6. Read layer: clamp to [INT16_MIN, INT16_MAX]. layer is i16 in the Sprite struct.
// 7. If hasComponent<Sprite>(id): log warning, removeComponent<Sprite>(id).
// 8. addComponent<Sprite>(id, Sprite{
//        rhi::TextureHandle{static_cast<u32>(rawHandle)},
//        glm::vec2{width, height},
//        glm::vec2{0.0f, 0.0f},       // uvMin — full texture
//        glm::vec2{1.0f, 1.0f},       // uvMax
//        glm::vec4{r, g, b, a},
//        static_cast<i16>(layer),
//        0                             // sortOrder — not exposed to Lua in this design
//    });
// 9. Push true. Return 1.
```

**Open design question for security-auditor:** Should colour components `r, g, b, a` be clamped to `[0.0, 1.0]` silently, or should out-of-range values be rejected with `false`? Arguments for clamping: scripts that animate colour toward 0 or 1 may momentarily exceed range due to floating-point arithmetic; silent clamping is forgiving. Arguments for rejecting: out-of-range colour may indicate a script bug (e.g., passing pixel values 0-255 instead of 0.0-1.0). Recommend: clamp and log a warning on first occurrence per entity.

---

## 9. Security Properties — Checklist for security-auditor

The following properties must be verified by security-auditor before implementation may begin. They are numbered for traceability.

**SEC-E-1: Entity ID range check before cast**
Every binding that receives an entity ID must check `rawId >= 0 && rawId <= (UINT32_MAX - 1)` before `static_cast<EntityId>(rawId)`. Negative values must be rejected. `UINT32_MAX` must be rejected to prevent NULL_ENTITY construction from Lua.

**SEC-E-2: isValid() always before component access**
After the range check and cast, `world->isValid(entityId)` must be called before any `hasComponent`, `getComponent`, `addComponent`, or `removeComponent` call. This protects against stale/recycled entity IDs. No path through any binding may reach component access without first passing `isValid()`.

**SEC-E-3: World pointer null check**
Every binding retrieves the World from `LUA_REGISTRYINDEX`. If the result is nil or the pointer is null, the binding must return the documented failure value immediately. No dereference of a null world pointer is permitted.

**SEC-E-4: Texture handle range check before use**
`ffe.addSprite` must check `rawHandle > 0 && rawHandle <= UINT32_MAX` before constructing `rhi::TextureHandle{static_cast<u32>(rawHandle)}`. Zero is the invalid handle sentinel and must be explicitly rejected.

**SEC-E-5: Non-finite float rejection**
All float arguments passed to `addTransform` and `addSprite` must be checked with `std::isfinite()`. Non-finite values (NaN, +/-Infinity) must be rejected before any component is written. The Transform is left unchanged (or not created) on rejection.

**SEC-E-6: Sprite width and height must be positive and finite**
Width and height arguments to `addSprite` must satisfy `width > 0 && height > 0` in addition to the `std::isfinite` check. A zero or negative size would produce degenerate sprites and could trigger undefined GPU behaviour in the sprite batch.

**SEC-E-7: Layer clamping is safe**
The `layer` argument to `addSprite` is an unchecked `lua_Integer` narrowed to `i16`. The binding must clamp or range-check before cast: `if (rawLayer < INT16_MIN || rawLayer > INT16_MAX) reject`. Narrowing an out-of-range integer to `i16` is undefined behaviour in C++.

**SEC-E-8: No heap allocation in bindings beyond EnTT component storage**
`addComponent` calls into EnTT, which may allocate storage internally. This is acceptable — EnTT manages its own storage and these bindings are not per-frame operations (createEntity/addComponent are called at entity creation time, not 60 times per second). If a script calls these in a per-frame callback, that is a misuse documented in the `.context.md` "What not to do" section. The bindings themselves must not introduce any additional heap allocations (no `std::string`, no `std::vector`).

**SEC-E-9: destroyEntity must not double-free**
`world->destroyEntity()` calls `m_registry.destroy()` which asserts in debug mode on an invalid entity. The binding must call `isValid()` first and return cleanly (no-op) if the entity is not valid. This prevents a script from crashing a debug build by passing a recycled or never-valid ID.

**SEC-E-10: No raw pointer exposure**
None of these bindings return C++ pointers to Lua. Entity IDs and texture handles are integers. World and RHI pointers remain in C++ memory, retrieved from the Lua registry only within the binding function's scope and never pushed to the Lua stack.

**SEC-E-11: removeComponent before overwrite**
Bindings that overwrite existing components (addTransform, addSprite, addPreviousTransform on entities that already have those components) must call `removeComponent` before `addComponent`. Calling `emplace` on an EnTT entity that already has a component type is undefined behaviour. Alternatively, use `registry().emplace_or_replace<T>()` via the raw registry escape hatch, which is atomic and does not require the remove step.

**SEC-E-12: Instruction budget applies to binding execution**
The 1,000,000 instruction budget set in `ScriptEngine::init()` covers Lua bytecode execution. C++ binding functions called from Lua do not consume the instruction budget. A script that calls `ffe.createEntity()` in a loop, creating entities indefinitely, will eventually exhaust memory but will not be stopped by the instruction budget alone. The binding should not attempt to enforce a per-call entity creation limit (that is a game design concern), but security-auditor should confirm that memory exhaustion from `registry.create()` in a loop is safe (no UB, no crash — just allocation failure) on the target platform.

---

## 10. Deferred to Future Session

**ffe.loadTexture from Lua** is deferred entirely. See Section 5 for rationale.

The binding can be added in a future session after:
1. ADR-005 texture loading is implemented and has passed its full security-auditor post-implementation review
2. A separate design note (or addendum to this one) addresses the cross-subsystem coupling between the script engine's asset root and the texture loader's asset root
3. security-auditor reviews the expanded attack surface (Lua-controlled file path → file I/O)

**uvMin / uvMax / sortOrder** on the Sprite component are not exposed to Lua in this design. Spritesheet animation (adjusting uvMin/uvMax per frame) is a common pattern and should be addressed in a dedicated animation binding design note.

**Component removal bindings** (`ffe.removeSprite`, `ffe.removeTransform`) are not included in this design. They can be added in a future session. The decision to omit them is intentional: giving scripts the ability to remove renderer components from arbitrary entities increases the attack surface (a script removes a component a C++ system depends on). Deferring until there is a concrete use case.

---

## 11. Summary of Decisions

| Question | Decision |
|----------|----------|
| Entity ID representation in Lua | `lua_Integer`; C++ range-checks to `[0, UINT32_MAX-1]` then calls `isValid()` |
| NULL_ENTITY handling | Never expose to Lua; return `nil` from `createEntity` on failure |
| Entity ownership | No ownership tracking; Lua may destroy C++ entities; `isValid()` is the safety net |
| Texture handles | Opaque integer (`u32 id`); validated `> 0 && <= UINT32_MAX` before use |
| addTransform on existing component | Log warning, overwrite |
| addSprite on existing component | Log warning, overwrite |
| addPreviousTransform on existing component | Log warning, overwrite; use removeComponent+addComponent or emplace_or_replace |
| addPreviousTransform with no Transform | Use zero default, log warning |
| Error signalling convention | Return `nil` or `false`; no `luaL_error` from these bindings |
| ffe.loadTexture | Deferred — security surface too large for this session |
| Colour clamping in addSprite | Clamp to [0.0, 1.0] with warning on out-of-range |

---

*This design note is the primary input for the security-auditor shift-left review. engine-dev implementation is blocked until security-auditor returns a verdict with no CRITICAL or HIGH findings unresolved.*
