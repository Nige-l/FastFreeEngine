# ADR-007: 3D Foundation — Shift-Left Security Review

**Status:** APPROVED WITH REQUIRED DESIGN CHANGES (see HIGH findings below)
**Reviewer:** security-auditor
**Date:** 2026-03-06
**ADR Reviewed:** ADR-007-3d-foundation.md v1.0
**Overall Rating:** HIGH ISSUES — implementation blocked until H-1 and H-2 design changes are confirmed

---

## Executive Summary

ADR-007 is substantially well-designed. The architect has carried the established path-traversal prevention patterns from `texture_loader` into the mesh loading pipeline and pre-specified a meaningful set of security constraints (SEC-M1 through SEC-M7). The design closes the most obvious attack vectors.

Two HIGH findings require explicit design clarification before implementation begins. Neither requires architectural rework — they require specific, localisable additions to the ADR's implementation notes. Once those additions are in place, the design is safe to implement.

Four MEDIUM and two LOW/INFO findings are documented for Phase 4 remediation. None block implementation.

---

## Findings Summary

| ID | Severity | Area | Short Description |
|----|----------|------|-------------------|
| H-1 | HIGH | External .bin buffer loading | cgltf resolves `.bin` URIs relative to the .gltf file path — not constrained to asset root without explicit intervention |
| H-2 | HIGH | cgltf accessor read safety | `cgltf_validate()` does not fully protect `cgltf_accessor_read_float/index` from OOB; FFE must validate accessor offset+stride before data extraction |
| M-1 | MEDIUM | External .bin file size | 64 MB limit applies only to the primary file; individual `.bin` references are uncapped without additional enforcement |
| M-2 | MEDIUM | Heap fallback null handling | Arena fallback uses `new` — ADR does not explicitly require null/exception guard on that allocation before cgltf_free |
| M-3 | MEDIUM | GL_OUT_OF_MEMORY after glBufferData | GPU OOM during buffer upload is silent in the current RHI; not detected or reported back to loadMesh |
| M-4 | MEDIUM | Zero-length lightDir NaN in shader | `ffe.setLightDirection(0,0,0)` from Lua produces a zero-length vector; normalise(0,0,0) is undefined in GLSL |
| L-1 | LOW | Lua mesh handle type guard | No explicit type guard on the meshHandle integer argument to createEntity3D and setMeshColor |
| I-1 | INFO | cgltf CVE status (Q-M1 answer) | No known CVEs in cgltf v1.x as of this review date; iterative JSON parser |

---

## Detailed Findings

---

### H-1 — HIGH: External .bin Buffer Path Traversal via cgltf_load_buffers

**Affected component:** `engine/renderer/mesh_loader.cpp` (Step 4 of Section 10.2 data flow)

**Description:**

`cgltf_load_buffers()` takes a `base_path` argument. When parsing a `.gltf` text file, cgltf resolves external buffer URIs by concatenating `base_path + buffer.uri`. If a malicious `.gltf` file contains:

```json
"buffers": [{"uri": "../../etc/passwd", "byteLength": 42}]
```

cgltf will attempt to open `<base_path>/../../etc/passwd`. The path traversal prevention in SEC-M1 only validates the top-level `.gltf` or `.glb` path supplied by the caller — it does not control what cgltf subsequently resolves for buffer URIs, texture URIs, or other external references.

The ADR specifies `cgltf_load_buffers()` is called in Step 4, but does not specify what `base_path` is passed, nor that buffer URIs are inspected before cgltf follows them.

This is the same class of vulnerability as SSRF/path traversal in image loaders that follow embedded file references.

**Required design change:**

The ADR must add a requirement to Section 10.4 (SEC-M2 expansion) specifying one of these two approaches:

**Option A (recommended — eliminate the risk entirely):** Require that only `.glb` (binary glTF) files are accepted in this session. `.glb` embeds the binary buffer as a chunk within the file — `cgltf_load_buffers` has nothing to resolve externally. The file size check (SEC-M2) then covers 100% of the data. Accepting `.glb` only is a reasonable scope restriction for the first 3D session; `.gltf` + external `.bin` can be added in a future ADR once path hardening for cgltf URIs is designed.

**Option B (if .gltf must be supported):** After `cgltf_parse_file` and before `cgltf_load_buffers`, iterate all `data.buffers` and `data.images` in the parsed cgltf_data and apply the same `isPathSafe()` check to each `buffer.uri` / `image.uri`. Reject any that are absolute, contain `../`, or are non-null and non-data-URI. Only then call `cgltf_load_buffers` with `base_path` set to the asset root (not the directory of the .gltf file — set it to `getAssetRoot()` to constrain resolution). After loading, call `realpath()` on each resolved buffer path and verify it is inside the asset root.

The ADR explicitly raises this as Q-M2. This review answers Q-M2: the risk is real and unmitigated in the current design. Implementation cannot proceed without one of these options being specified.

**Recommended fix for ADR-007:** Add a new SEC-M8 to Section 10.4:

> SEC-M8: External URI restriction. `loadMesh` must reject any file whose path does not have a `.glb` extension (case-insensitive). `.gltf` files with external `.bin` or image references are not accepted in this session. This eliminates the external URI resolution attack surface entirely. Future support for `.gltf` requires a separate security review.

---

### H-2 — HIGH: cgltf Accessor Bounds — cgltf_validate() Is Insufficient Alone

**Affected component:** `engine/renderer/mesh_loader.cpp` (Step 6 of Section 10.2 data flow)

**Description:**

The ADR specifies that `cgltf_validate()` is called after parsing and that accessor component types and element types are verified. However, `cgltf_validate()` validates the structural consistency of the glTF file (correct field types, correct enum values, no dangling references) — it does **not** guarantee that an accessor's `offset + stride * count` is within the bounds of its buffer view, because the validator trusts that the buffer has been loaded.

The concern is in Step 6b-6d: the data extraction loop reads from cgltf accessor data. cgltf provides `cgltf_accessor_read_float()` as a safe accessor that performs bounds checking internally — **however, this safety is only reliable if the buffer data is actually loaded and not corrupted**. cgltf does not prevent a crafted file from specifying an accessor with a `byteOffset` that, combined with `stride * count`, would point beyond the buffer view's `byteLength`.

Specifically:

1. `cgltf_validate()` checks that buffer views are within buffer bounds and that accessors are within buffer view bounds — **this check IS performed** against the declared `byteLength`, not the actual loaded data length.
2. **The actual risk is** that `cgltf_load_buffers` reads the binary data as-is. If a `.glb`'s embedded BIN chunk is shorter than the declared buffer `byteLength`, the loaded buffer data may be truncated, but cgltf will report `cgltf_result_success` because it loaded what was there. A subsequent accessor read using `cgltf_accessor_read_float` that is within declared bounds but beyond actual data is undefined behaviour.

**Required design change:**

Add to SEC-M3 in Section 10.4:

> After `cgltf_validate()` succeeds, verify that each buffer's actual loaded data length (`buffer.data` is non-null, `buffer.data_size == buffer.size`). Reject the mesh if `buffer.data_size < buffer.size` for any buffer. This catches the case of a truncated BIN chunk whose declared byteLength exceeds the actual bytes loaded.

Additionally, the implementation must use `cgltf_accessor_read_float()` and `cgltf_accessor_read_index()` (the safe bounds-checked accessors) rather than direct pointer arithmetic into `cgltf_buffer_view.data`. Direct pointer arithmetic bypasses cgltf's accessor validation.

This answers Q-M1: `cgltf_validate()` is necessary but not sufficient alone. The additional buffer.data_size check and use of the safe accessor API together close the gap.

---

### M-1 — MEDIUM: External .bin File Size Not Bounded

**Affected component:** `engine/renderer/mesh_loader.cpp` (Step 2 and Step 4)

**Description:**

SEC-M2 specifies that `stat()` is called before `cgltf_parse_file` and the file is rejected if it exceeds 64 MB. For `.glb` files this is complete — the entire data is in one file.

For `.gltf` text files with external `.bin` references, `cgltf_load_buffers()` opens and reads the `.bin` file independently. The 64 MB stat check covers only the `.gltf` JSON (typically kilobytes). The `.bin` could be arbitrarily large.

**Mitigation if H-1 Option A (.glb only) is adopted:** This finding is fully resolved — no `.bin` files are ever loaded.

**Mitigation if H-1 Option B (both formats) is adopted:** Require a separate `stat()` call on each buffer URI path (after path validation per SEC-M8 option B) before calling `cgltf_load_buffers`. Reject if any single `.bin` exceeds `MESH_FILE_SIZE_LIMIT`. This is per-file, not aggregate — the aggregate concern is addressed by `MAX_MESH_VERTICES` and `MAX_MESH_INDICES` which bound the useful data.

**Severity rationale:** MEDIUM (not HIGH) because: (a) the fix for H-1 Option A eliminates this entirely, and (b) even if unchecked, `cgltf_load_buffers` will read the file into memory, not into a fixed stack buffer — the risk is process memory exhaustion / slow DoS rather than remote code execution.

---

### M-2 — MEDIUM: Heap Fallback Allocation — Null/Exception Guard Not Specified

**Affected component:** `engine/renderer/mesh_loader.cpp` (Section 10.2 note on CPU-side vertex data)

**Description:**

Section 10.2 states: "If the mesh exceeds arena capacity, `loadMesh` falls back to a heap allocation that is freed immediately after GPU upload."

The C++ rules in CLAUDE.md Section 4 prohibit exceptions in engine core. The ADR does not specify whether the fallback uses `new` (which throws `std::bad_alloc` unless compiled with `-fno-exceptions`) or `new (std::nothrow)` / `malloc` which returns null on failure.

FFE compiles with no exceptions in engine core (`-fno-exceptions` is implied by CLAUDE.md Section 4). Depending on the compiler configuration, `new` may call `std::terminate` rather than throw on failure. In either case, if the heap allocation fails, `cgltf_free` must still be called (SEC-M6) and `MeshHandle{0}` returned. The ADR's Section 10.2 note does not explicitly require null-checking the fallback allocation.

The ADR raises this as Q-M4. This review answers Q-M4: the fallback path is unsafe as currently described because the null/terminate case is unspecified.

**Required remediation (Phase 4):** Implementation must use `new (std::nothrow)` for the heap fallback, check for null before proceeding, and call `cgltf_free` + return `MeshHandle{0}` if null. The ADR should note this.

**Severity rationale:** MEDIUM because the failure condition (OOM during a cold loading path with a 64 MB file cap) is rare in normal game operation. But it must be handled correctly to satisfy CLAUDE.md Section 5 ("Memory safety is paramount. No use-after-free, double-free, or uninitialised reads").

---

### M-3 — MEDIUM: glBufferData GPU OOM Not Detected

**Affected component:** `engine/renderer/mesh_loader.cpp` (Step 9), `engine/renderer/opengl/rhi_opengl.cpp`

**Description:**

The RHI's `createBuffer()` calls `glBufferData()` to upload vertex/index data to the GPU. `glBufferData()` can fail with `GL_OUT_OF_MEMORY` when VRAM is exhausted. In OpenGL 3.3, this generates a GL error accessible via `glGetError()` — but `createBuffer()` does not check `glGetError()` after `glBufferData()`.

The existing RHI has a `VRAM_BUDGET_BYTES` soft-limit (500 MB) tracked for textures, but there is no analogous budget tracking for vertex/index buffers. The mesh pool's estimated VRAM for 100 maximum-size meshes is ~44 MB per mesh * 100 = 4.4 GB, which trivially exceeds the LEGACY tier 1 GB VRAM budget.

Under VRAM exhaustion: `glBufferData` sets `GL_OUT_OF_MEMORY`, returns without uploading, the buffer GL ID is non-zero (the object exists but has no storage), and `createBuffer` returns a handle that appears valid. Subsequently, `meshRenderSystem` draws with this "empty" buffer — behaviour is implementation-defined (likely garbled geometry or driver crash).

**Required remediation (Phase 4):** `mesh_loader.cpp` must call `glGetError()` after each `glBufferData` call (inside the GPU upload step) and treat `GL_OUT_OF_MEMORY` as a fatal load failure, triggering `glDeleteBuffers` + `glDeleteVertexArrays` cleanup and returning `MeshHandle{0}`. This is not a hot path so the `glGetError()` call is acceptable.

**Severity rationale:** MEDIUM because: (a) the 100-slot cap at reasonable real-world mesh sizes keeps usage well within 1 GB; (b) the risk manifests as rendering corruption rather than a security breach. However, it violates the "memory safety is paramount" mandate.

---

### M-4 — MEDIUM: Zero LightDir NaN in Fragment Shader

**Affected component:** `engine/scripting/lua_bindings.cpp` (ffe.setLightDirection binding), GLSL fragment shader

**Description:**

The fragment shader in Section 9.3 contains:

```glsl
vec3 lightDir = normalize(-u_lightDir);
```

In GLSL, `normalize(vec3(0,0,0))` is undefined behaviour — it produces NaN or Inf on all real GPU implementations. If a Lua script calls:

```lua
ffe.setLightDirection(0, 0, 0)
```

the resulting fragment shader output will be NaN for every pixel of every mesh. On most GPUs this manifests as a black screen with no error. On some GPUs it can produce inf values that propagate through the depth buffer and corrupt subsequent renders.

The ADR specifies that `setLightDirection` normalises the input in the C++ binding layer. However, the normalisation of a zero-length vector in glm is also undefined — `glm::normalize(glm::vec3{0,0,0})` produces `{NaN, NaN, NaN}`.

The same NaN risk exists if a Lua script sets the default `SceneLighting3D.lightDir` to zero via in-memory manipulation — but that path is not user-accessible. The Lua binding is the primary vector.

**Required remediation (Phase 4):**

In the `ffe.setLightDirection` C++ binding:
1. Compute `float len = glm::length(inputVec)`.
2. If `len < 1e-6f`, log an error and reject the call (keep the previous lightDir value).
3. Only normalise and store if the vector has non-trivial length.

Additionally, `SceneLighting3D`'s default `lightDir` is initialised via `glm::normalize(glm::vec3{0.5f, -1.0f, 0.3f})` — this is a non-zero vector, so the default is safe. The test case `"SceneLighting3D: default lightDir is normalised"` in Section L already validates this. The guard in the Lua binding covers the dynamic case.

**Severity rationale:** MEDIUM because it requires active Lua code to trigger and produces rendering corruption (not a security breach), but it is reachable from an untrusted Lua script in a sandbox-escape scenario: a crafted game script could use this to produce rendering artifacts that mislead the user.

---

### L-1 — LOW: Missing Lua Argument Type Guard on meshHandle

**Affected component:** `engine/scripting/lua_bindings.cpp` (ffe.createEntity3D, ffe.setMeshColor, ffe.setTransform3D)

**Description:**

The existing Lua bindings in the engine consistently apply a type guard before calling `lua_tointeger` (see the pattern in `ffe.loadSound`: `if (lua_type(state, 1) != LUA_TSTRING) { ... }`). This prevents silent coercion of non-integer arguments (e.g., `ffe.createEntity3D("malicious", 0, 0, 0)` silently coercing the string `"malicious"` to 0 and triggering the "invalid mesh handle" log path rather than a type error).

The ADR's Section 11.3 specifies error handling for invalid entity IDs and mesh handles but does not explicitly require the `lua_type()` guard for the integer arguments. The existing code pattern for integer bindings should be extended here.

**Required remediation (Phase 4):** All new bindings that accept integer arguments (meshHandle, entityId) must check `lua_type(state, N) != LUA_TNUMBER` before calling `lua_tointeger`. Log an error and return 0 on type mismatch. This is consistent with the existing `loadSound` / `loadMusic` type guard pattern.

**Severity rationale:** LOW because: the ECS entity ID validation and mesh handle isValid() check that follow already provide the substantive safety net; the type guard is a belt-and-suspenders defence-in-depth measure.

---

### I-1 — INFO: cgltf CVE Status and JSON Parser Safety (Answers Q-M1 supplement)

**Component:** `third_party/cgltf.h`

**Assessment:**

As of this review (March 2026), cgltf v1.x has no published CVEs in the NVD or in the cgltf GitHub issue tracker. The library has been in production use in game engines (including Filament, O3DE, and numerous indie engines) since 2019.

**JSON parser:** cgltf uses its own minimal hand-written JSON tokenizer (`jsmn`-derived), not a recursive descent parser. JSON parsing is iterative. Deeply nested JSON will not cause a stack overflow. This is safer than most general-purpose JSON parsers for this use case.

**cgltf_validate():** Confirms structural validity (accessor component types, element types, buffer view ranges, accessor ranges). It does not load or inspect actual binary buffer content. The H-2 finding above identifies the gap that `buffer.data_size` verification closes.

**Memory model:** cgltf uses `malloc`/`free` via the `cgltf_options.memory.alloc_func` / `free_func` callbacks. With zero-initialised `cgltf_options`, it uses the system allocator. There are no fixed-size internal buffers that could overflow. The cgltf `options.file.read` callback defaults to `fopen`/`fread`/`fclose`.

**Recommended version:** cgltf v1.14 (latest as of 2025-06). Commit hash should be pinned in the `third_party/` vendor note comment at the top of `cgltf.h`.

---

## Answers to ADR-007 Open Questions

**Q-M1:** `cgltf_validate()` alone is not sufficient. It validates structural integrity but does not verify that loaded buffer data length matches declared buffer byte length. FFE must add a `buffer.data_size == buffer.size` check after `cgltf_load_buffers` succeeds and must use `cgltf_accessor_read_float()` / `cgltf_accessor_read_index()` (not direct pointer arithmetic) during vertex extraction. See H-2 above.

**Q-M2:** Yes, there is a real path traversal risk when `.gltf` files reference external `.bin` buffers. cgltf resolves buffer URIs relative to the caller-supplied `base_path` argument. The design does not currently constrain that resolution to the asset root, and it does not validate buffer URIs through `isPathSafe()`. The recommended fix is Option A from H-1: restrict to `.glb` only in this session, eliminating the external URI surface entirely.

**Q-M3:** The 64 MB limit does not apply to external `.bin` files in the current design. If `.gltf` support is added in a future ADR, separate `stat()` checks per buffer URI are required. This finding is resolved by adopting H-1 Option A (`.glb` only) for this session.

**Q-M4:** The heap fallback is potentially unsafe as described. The implementation must use `new (std::nothrow)` (not `new`), check for null before any use, and call `cgltf_free` + return `MeshHandle{0}` on null. See M-2 above.

---

## getGlBufferId — Missing from rhi_opengl.h

**Component:** `engine/renderer/opengl/rhi_opengl.h`

Section 10.2 of the ADR states: "If `rhi_opengl.h` does not expose `getGlBufferId`, engine-dev adds it."

Review of `rhi_opengl.h` confirms: the function **does not currently exist**. `rhi_opengl.h` only declares `ffe::rhi::detail` enum conversion functions and `bytesPerPixel`. There is no `getGlBufferId` or equivalent.

**Required implementation action:** `engine-dev` must add `GLuint getGlBufferId(rhi::BufferHandle handle)` to `rhi_opengl.h` (and its implementation to `rhi_opengl.cpp`) as part of this session. This is not a security finding — it is a clarification that the conditional in the ADR resolves to "must add it."

---

## Design Changes Required Before Implementation

The following changes to ADR-007 must be made (or confirmed by PM/architect) before `engine-dev` begins:

### Required: SEC-M8 Addition (resolves H-1 and implicitly M-1)

Add to Section 10.4:

> **SEC-M8: .glb-only restriction.** `loadMesh` rejects any path that does not end with `.glb` (case-insensitive check). `.gltf` files are not supported in this session. This eliminates the external URI resolution attack surface (buffer URIs, image URIs) from the cgltf pipeline. Future `.gltf` support requires a dedicated security design review for URI validation and path containment.

### Required: SEC-M3 Expansion (resolves H-2)

Expand SEC-M3 in Section 10.4 to add after the existing validation list:

> - After `cgltf_load_buffers` succeeds: for every buffer in `data.buffers`, verify `buffer.data != nullptr` and `buffer.data_size >= buffer.size`. Reject the mesh if any buffer has truncated data.
> - During vertex/index extraction (Step 6): use `cgltf_accessor_read_float()` and `cgltf_accessor_read_index()` exclusively. Do not use direct pointer arithmetic into buffer view data.

---

## Passing Design Elements

The following design decisions are explicitly confirmed as sound:

- **SEC-M1 (path traversal prevention):** The `isPathSafe()` pattern from `texture_loader.cpp` — including null/empty checks, absolute path rejection, `../` pattern rejection, and `realpath()` + prefix comparison — is the correct and proven approach. Applying it identically to `loadMesh` is correct.

- **SEC-M2 (file size limit):** 64 MB before any cgltf call is appropriate. Combined with SEC-M8 (`.glb` only), this comprehensively bounds input size.

- **SEC-M4 (vertex/index count limits before allocation):** Checking `accessor_count > MAX_MESH_VERTICES` and `index_count > MAX_MESH_INDICES` before memory allocation or GPU upload is the correct ordering. This follows the "fail fast before committing resources" principle.

- **SEC-M5 (u64 size arithmetic):** Using `static_cast<u64>(vertexCount) * sizeof(rhi::MeshVertex)` before comparing or passing to GL is correct and matches the existing RHI pattern in `rhi_opengl.cpp` (e.g., `vramBytes64 = static_cast<u64>(desc.width) * desc.height * bpp`).

- **SEC-M6 (cgltf_free on all paths):** Requiring `cgltf_free` on all exit paths is correct. The goto-cleanup or RAII recommendation is appropriate for a C-style API.

- **SEC-M7 (no per-frame loading):** Debug-build assertion and cold-path documentation are correct defensive measures.

- **Lua sandbox:** `loadMesh` obtains the path from Lua, applies `isPathSafe()` as SEC-M1, and calls `ffe::renderer::getAssetRoot()` (not a Lua-controllable value) as the root. Scripts cannot control the asset root — the same write-once protection from `texture_loader` applies. The 100-slot cap enforced before file I/O prevents loop-based VRAM exhaustion from Lua.

- **SceneLighting3D default initialisation:** The default `lightDir = glm::normalize(glm::vec3{0.5f, -1.0f, 0.3f})` is a non-zero vector and the normalisation is performed at compile time (constant expression). The default state is safe. The M-4 finding covers the dynamic case only.

- **Integer overflow in buffer size calculations (SEC-M5):** Design is correct. The u64 upcast before multiplication prevents 32-bit overflow.

- **MeshHandle null semantics:** `id == 0` as the invalid sentinel, `isValid()` helper, and checking before any GPU operation is the correct pattern (consistent with TextureHandle, BufferHandle, ShaderHandle).

---

## Overall Verdict

**HIGH ISSUES FOUND. Implementation is blocked until the two required design changes are confirmed:**

1. SEC-M8 added to ADR-007 Section 10.4 (`.glb`-only restriction)
2. SEC-M3 expanded in ADR-007 Section 10.4 (buffer data_size check + safe accessor API requirement)

Once PM or architect confirms those two additions to the ADR (they do not require redesign — they are additive constraints on the existing implementation notes), implementation may proceed. The MEDIUM and LOW findings are addressed in Phase 4 remediation after `engine-dev` implements.

The attack surface introduced by this ADR is well-understood and well-bounded. With the two HIGH findings resolved, this is a sound design that follows FFE's established security patterns.
