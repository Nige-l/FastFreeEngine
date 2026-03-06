# Security Review: ffe.loadTexture Lua Binding (Shift-Left)

**Reviewer:** security-auditor
**Date:** 2026-03-06
**Document reviewed:** design-note-lua-texture-load.md
**Underlying C++ implementation reviewed:** engine/renderer/texture_loader.cpp
**ADR reviewed:** ADR-005-texture-loading.md, ADR-005-security-review.md
**Review type:** Shift-left design review — gates implementation start

---

## Verdict: PASS WITH CONDITIONS

**Implementation may begin: YES WITH CONDITIONS**

One MEDIUM condition must be satisfied in the binding implementation before merge. The existing C++ validation chain in `texture_loader.cpp` is comprehensive and correctly implements all HIGH conditions from the prior ADR-005 review. Those conditions are confirmed satisfied at the C++ layer. The remaining condition is specific to the Lua binding layer itself.

---

## Conditions for Implementation (if any)

### HIGH — Hard requirements

None. All HIGH findings from ADR-005-security-review.md (HIGH-1 through HIGH-3) are confirmed implemented in `texture_loader.cpp`. The Lua binding layer introduces no new HIGH-severity attack surface.

### MEDIUM — Required before merge

**M-1: Explicit nil guard before lua_tostring in ffe.loadTexture binding.**

The design (Section 5, implementation notes) recommends using `luaL_optstring(state, 1, nullptr)` or checking `lua_type(state, 1) == LUA_TSTRING`. This is correct. The binding must implement the nil guard explicitly and must not rely on the C++ `isPathSafe()` null check as the sole protection.

Rationale: `lua_tostring()` returns a non-null empty string for some Lua types (e.g., numbers — Lua coerces them to strings). If a script calls `ffe.loadTexture(0)` or `ffe.loadTexture(false)`, `lua_tostring` will not return null, and the path forwarded to `renderer::loadTexture()` will be `"0"` or `"false"`. The C++ `isPathSafe()` will reject these strings (they are neither valid relative paths nor traversal attempts), so no file is opened, and nil is returned correctly. However, silently coercing non-string arguments to strings is confusing API behaviour that could mask script bugs. The binding should check `lua_type(state, 1) == LUA_TSTRING` (or use `luaL_checkstring` to raise a Lua type error) and return nil for non-string, non-nil arguments without forwarding anything to C++.

This condition has no security impact at the C++ boundary — the C++ layer rejects the coerced strings — but it prevents silent type confusion in scripts and is the correct defensive posture for an untrusted-input binding.

The implementation note in design-note-lua-texture-load.md is consistent with this requirement. Confirm it is implemented as specified before merge.

---

## Advisory Findings (LOW — do not gate implementation)

**L-1: Null byte injection — confirmed not a concern at this binding layer.**

ADR-005 Q5 addressed the embedded-null concern at the C++ layer; the fix (`strnlen` + `PATH_MAX` guard in `isPathSafe()`) is implemented. The Lua binding adds no new exposure: Lua strings are length-counted (`lua_tolstring` returns the length independently of null content), but `lua_tostring` returns a `const char*` that points to the Lua string's internal buffer, which is null-terminated at the Lua length boundary. If a Lua string contains an embedded null (e.g., `"sprites/foo\0../etc/passwd"`), `lua_tostring` returns a pointer where the C `strlen` sees only `"sprites/foo"`. The remaining bytes after the embedded null are invisible to the C++ string APIs. `isPathSafe()` will see a clean path and `realpath()` will operate on it normally. This is safe — the embedded content after the null is silently dropped, not acted upon. No action required, but the post-implementation review should confirm the binding uses `lua_tostring` (not a length-aware copy) consistently.

**L-2: Lua integer overflow on ffe.unloadTexture — design handles it correctly.**

The design specifies rejecting `rawHandle > UINT32_MAX` before calling `renderer::unloadTexture()`. In Lua 5.4 (and LuaJIT), `lua_Integer` is 64-bit. A Lua script can pass `math.maxinteger` (2^63 - 1) to `ffe.unloadTexture()`. The binding must retrieve the argument with `luaL_checkinteger` and then range-check: `rawHandle <= 0 || rawHandle > UINT32_MAX` → no-op, consistent with the design note. The design specifies this correctly. Confirm the implementation matches; the post-implementation review will verify.

**L-3: Use-after-unload (dangling handle) — document in .context.md, no additional protection warranted.**

If a Lua script calls `ffe.unloadTexture(handle)` and then passes the same integer to `ffe.addSprite()`, the GPU has a dangling texture reference. The C++ `addSprite` binding already accepts integer texture handles without re-validating that the underlying GPU texture is still live (this is the same gap flagged in the addSprite design). Adding validation here would require a live-handle registry with O(1) lookup maintained across Lua calls — non-trivial surface with its own failure modes.

The correct mitigation is documentation, not runtime validation. The `.context.md` should explicitly state that calling `ffe.unloadTexture(handle)` invalidates the integer — after that call, the integer must not be passed to `ffe.addSprite()` or any other binding. The design note (Section 3) already shows the correct teardown sequence (`destroyEntity` before `unloadTexture`). Confirm `.context.md` covers this explicitly.

**L-4: No per-frame guard in the binding — confirmed acceptable by design.**

There is no mechanism in the binding layer that prevents a Lua script from calling `ffe.loadTexture()` inside an update loop. The design correctly identifies this as a developer responsibility and documents it in the API comment ("Do NOT call inside an update function"). The instruction budget hook does not protect against this — `loadTexture` will complete before the next instruction count check, regardless of cost. This is consistent with how all other I/O-adjacent bindings in the engine are treated. Document in `.context.md` with the same "per-frame use is a performance violation" language used in `texture_loader.h`.

**L-5: Two-argument loadTexture overload is not accessible from Lua — confirm this holds.**

The C++ API exposes a two-argument `loadTexture(path, assetRoot)` overload that bypasses the global asset root. The design explicitly restricts the Lua binding to the single-argument overload (Decision 1, P1). If `engine-dev` accidentally wires the binding to the two-argument overload (e.g., to pass a Lua-side assetRoot), that would be a HIGH security regression — scripts would gain indirect asset root control. The post-implementation review will verify that only the single-argument `renderer::loadTexture(path)` overload is called from the binding.

---

## Full Findings Reference

| ID | Severity | Summary | Gates implementation? |
|----|----------|---------|----------------------|
| M-1 | MEDIUM | Binding must perform explicit Lua type check before forwarding path to C++; no silent coercion of non-string arguments | YES — required before merge |
| L-1 | LOW | Embedded null in Lua string silently truncated at C boundary — safe, no action required, confirm lua_tostring usage in post-impl review | NO |
| L-2 | LOW | lua_Integer overflow on unloadTexture — design handles correctly, confirm in post-impl review | NO |
| L-3 | LOW | Use-after-unload creates dangling GPU handle — document in .context.md, no runtime validation warranted | NO |
| L-4 | LOW | No per-frame guard in binding — acceptable, document in .context.md | NO |
| L-5 | LOW | Two-argument loadTexture overload must not be callable from Lua — verify in post-impl review | NO |

---

## Assessment of Design Properties P1–P7

The design note (Section 4) lists seven security properties for post-implementation verification. This review confirms they are architecturally sound:

- **P1 (No asset root control from Lua):** Correctly enforced by using only the single-argument overload. No `setAssetRoot` or `setTextureRoot` binding must exist. Write-once semantics in C++ (LOW-2 from ADR-005-security-review.md, confirmed implemented) means even if a future binding mistake exposed `setAssetRoot`, the first call from C++ startup would have locked it.
- **P2 (Path forwarded without modification):** Correct and preferred. Any pre-processing in the binding layer risks divergence from the C++ validation. Forward the raw Lua string; let `isPathSafe()` and `realpath()` do their work.
- **P3 (Nil or empty argument returns nil):** Correct. Guard implemented as M-1 above. `isPathSafe()` in C++ is a backstop, not the primary guard.
- **P4 (Return value bounded or nil):** The u32 id field is always in [1, UINT32_MAX] on success by construction (`TextureHandle{0}` is the only failure sentinel, and the binding converts it to nil). Correct.
- **P5 (unloadTexture validates before calling C++):** Correct. The `renderer::unloadTexture()` implementation already handles id=0 as a no-op, but the Lua binding should still range-check before calling, as specified.
- **P6 (No instruction budget bypass):** Confirmed. C functions registered with `lua_pushcfunction` do not re-enter the Lua VM and are not subject to the instruction count hook. The hook fires on the next Lua instruction after the C binding returns. No bypass.
- **P7 (No sandbox escape):** The binding does not add filesystem, network, or OS access beyond what `renderer::loadTexture()` already provides and constrains. The `io`, `os`, `package`, and `ffi` sandbox blocks in `ScriptEngine::setupSandbox()` are unaffected.

---

## Prior ADR-005 Conditions — Confirmation Status

All HIGH and MEDIUM conditions from ADR-005-security-review.md are confirmed implemented in `texture_loader.cpp`:

| ID | Summary | Status |
|----|---------|--------|
| HIGH-1 | SEC-4 before SEC-3 ordering | CONFIRMED — code comment and ordering explicit at lines 230-272 |
| HIGH-2 | Path concat buffer overflow check | CONFIRMED — strnlen + overflow guard before memcpy at lines 157-163 |
| HIGH-3 | No cwd default when setAssetRoot() not called | CONFIRMED — isSet guard at lines 341-345 |
| MEDIUM-2 | Pre-decode stat() file-size check + STBI_MAX_DIMENSIONS=8192 | CONFIRMED — both present |
| MEDIUM-3 | strnlen + PATH_MAX guard replaces null-byte scan | CONFIRMED |
| LOW-2 | Write-once setAssetRoot() semantics | CONFIRMED — isSet flag at lines 307-312 |

The C++ validation chain is sound. The Lua binding inherits this chain when it calls `renderer::loadTexture(path)` directly.

---

*This review gates implementation of the `ffe.loadTexture` and `ffe.unloadTexture` Lua bindings. Implementation may begin. The M-1 condition must be satisfied and verified in the post-implementation security review before merge.*
