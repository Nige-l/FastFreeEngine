# ADR-005 Texture Loading — Security-Auditor Shift-Left Review

**Reviewer:** security-auditor
**Date:** 2026-03-05
**Document reviewed:** ADR-005-texture-loading.md v1.0
**Review type:** Shift-left design review — gates implementation start

---

## Verdict: PASS WITH CONDITIONS

**Implementation may begin: YES WITH CONDITIONS**

Three HIGH conditions and three MEDIUM conditions must all be satisfied before merge. The post-implementation security review will verify each one.

---

## Conditions for Implementation (must be met before merge)

### HIGH — Hard requirements

1. **SEC-4 before SEC-3 ordering (HIGH-1):** `channels in [1,4]`, `width > 0`, `height > 0` validation (SEC-4) must execute before the `static_cast<u64>` multiplication (SEC-3). Add a code comment making this ordering dependency explicit. A negative `int` from stb_image cast to `u64` produces `0xFFFFFFFFFFFFFFFF` — SEC-4 must run first to prevent this.

2. **Path concatenation buffer overflow (HIGH-2):** Before concatenating `assetRoot + "/" + path`, check that `strlen(assetRoot) + 1 + strlen(path) + 1 <= PATH_MAX`. The concatenation buffer must be `PATH_MAX + 1` bytes. Failure to check this allows a stack buffer overflow in the concatenation step before `realpath()`.

3. **Remove cwd default from setAssetRoot() (HIGH-3):** If `setAssetRoot()` has not been called, `loadTexture()` must return `TextureHandle{0}` with `FFE_LOG_ERROR("loadTexture called before setAssetRoot()")`. Do not default to the current working directory. Applications that want the cwd must call `setAssetRoot()` explicitly.

### MEDIUM — Required before merge

4. **Pre-decode file-size check (MEDIUM-2):** Before calling `stbi_load()`, call `stat()` on the canonical path and reject files larger than `MAX_TEXTURE_BYTES`. This prevents stb_image from spending CPU time on a large compressed payload with a legitimate-looking header.

5. **STBI_MAX_DIMENSIONS (MEDIUM-2):** Define `STBI_MAX_DIMENSIONS 8192` before including `stb_image.h` in the implementation `.cpp`. This enforces the dimension limit inside stb_image's own header parser as defence-in-depth.

6. **Replace null-byte scan with strnlen (MEDIUM-3):** Remove the "contains null byte" check from SEC-1 — it is unreachable via C string iteration. Replace with: `if (strnlen(path, PATH_MAX) >= PATH_MAX) { return TextureHandle{0}; }`. This bounds the path and protects against non-null-terminated buffers.

---

## Open Questions — Answered

| Q# | Question | Answer |
|----|----------|--------|
| Q1 | Extension allowlisting needed? | **No.** stb_image detects format from magic bytes, not extension. Allowlisting is security theatre — rename `.exe` to `.png` bypasses it trivially. Don't implement. |
| Q2 | stb_image safe for untrusted content? | **With conditions.** Add pre-decode file-size check and set `STBI_MAX_DIMENSIONS 8192` (see MEDIUM-2 above). |
| Q3 | Runtime vs compile-time asset root? | **Runtime is correct** for a shipped engine. Add write-once semantics: once set to a valid path, further `setAssetRoot()` calls return false and log a warning. Prevents Lua script from redirecting loads. |
| Q4 | TOCTOU between realpath and fopen acceptable? | **Yes, accept the risk.** FFE is a local game engine, not a multi-user server. An attacker with write access to the asset dir can place content there directly. Document in a code comment near the `realpath()` call. |
| Q5 | strlen vs strnlen for path? | **Use strnlen.** The null-byte scan is unreachable via C string APIs. Use `strnlen(path, PATH_MAX) >= PATH_MAX` to bound path length (see MEDIUM-3 above). |

---

## Advisory Findings (LOW — do not gate implementation)

- **LOW-1:** Do not implement extension allowlisting (see Q1).
- **LOW-2:** Add write-once semantics to `setAssetRoot()` — once set, reject further calls with a warning.
- **LOW-3:** stb_image version must be noted in a comment in the implementation file. Treat as mandatory, not advisory.
- **LOW-4:** Add UNC path rejection: `if (path[0] == '\\' && path[1] == '\\') return false;`. Document that the `realpath()` path is POSIX-only and requires a porting note for Windows.

---

## Full Findings Reference

| ID | Severity | Summary |
|----|----------|---------|
| HIGH-1 | HIGH | Negative stb_image int cast to u64 bypasses size check if SEC-4 runs after SEC-3 |
| HIGH-2 | HIGH | Path concatenation before realpath() can overflow stack buffer |
| HIGH-3 | HIGH | cwd default for unset asset root is a reliability and security risk |
| MEDIUM-1 | MEDIUM | TOCTOU race — accepted, document in code |
| MEDIUM-2 | MEDIUM | No pre-decode file size limit; STBI_MAX_DIMENSIONS unset |
| MEDIUM-3 | MEDIUM | Null-byte scan unreachable; needs strnlen+PATH_MAX |
| LOW-1 | LOW | Extension allowlisting — do not implement |
| LOW-2 | LOW | Asset root write-once semantics |
| LOW-3 | LOW | stb_image version pinning |
| LOW-4 | LOW | Windows UNC paths not explicitly rejected |
