# ADR-005: Texture Loading

**Status:** PROPOSED — awaiting security-auditor shift-left review before implementation begins
**Author:** architect
**Date:** 2026-03-05
**Tiers:** RETRO, LEGACY (primary), STANDARD, MODERN
**Security Review Required:** YES — this ADR touches file I/O (CLAUDE.md Section 5, attack surface)

---

## 1. Problem Statement

The existing RHI provides `createTexture(const TextureDesc& desc)` which accepts raw pixel data in memory. Game code currently has no supported path from a file on disk to a usable `TextureHandle`. Developers must supply their own pixel buffers, which is impractical for real game development.

This ADR introduces a `loadTexture()` function that reads an image file from disk, decodes it, uploads it to the GPU, and returns a `TextureHandle`. Because this operation touches the filesystem with a caller-supplied path, it is an attack surface and requires explicit security constraints.

---

## 2. Decision

Use **stb_image** as the image decoding library.

**Rationale for stb_image:**
- Single public-domain C header — zero licensing complications for an MIT-licensed engine
- Handles PNG, JPG, BMP, TGA without format-specific code paths in FFE (stb_image detects format from content, not extension)
- Already a common choice in the same ecosystem as this engine; engine-devs will be familiar
- No build system complexity — same integration pattern as `glad` (header in `third_party/`, `STB_IMAGE_IMPLEMENTATION` defined in exactly one `.cpp`)
- Explicitly NOT a vcpkg dependency; no `vcpkg.json` change required for stb_image itself

**Dependency note (must be flagged explicitly per CLAUDE.md):**
- `stb_image.h` is embedded in `third_party/stb_image.h`
- This is NOT added to `vcpkg.json` (same pattern as glad)
- The commit message must state: `feat(renderer): add texture loading via stb_image (header in third_party/)`
- No other new dependencies are introduced by this ADR

---

## 3. Public API

### 3.1 C++ API — `engine/renderer/texture_loader.h`

```cpp
#pragma once

#include "renderer/rhi_types.h"

namespace ffe::renderer {

// Set the asset root directory. Must be called before any loadTexture() call.
// The path must be an absolute path to an existing directory.
// Returns false if the path does not exist, is not a directory, or is not absolute.
// The root is stored as a module-level static set once at engine startup.
// Thread safety: call from the main thread only, at Application::startup() time.
bool setAssetRoot(const char* absolutePath);

// Query the current asset root (for diagnostics and test verification).
const char* getAssetRoot();

// Load a texture from a file path relative to the configured asset root.
//
// path must be a relative path with no path traversal sequences.
// Rejected: paths starting with '/', '\', or a drive letter (e.g., "C:").
// Rejected: paths containing "../", "..\", "/..", or "\..".
// Rejected: null or empty paths.
//
// On success: returns a valid TextureHandle (handle.id != 0).
// On failure: returns TextureHandle{0} and logs an error via FFE_LOG_ERROR.
//
// The caller owns the TextureHandle and must call rhi::destroyTexture() when done.
//
// Thread safety: NOT thread-safe. Call from the main thread only.
// Performance: synchronous file I/O and GPU upload. NOT a per-frame operation.
//              Call at scene load time or during Application::startup().
//              Hot path use will be flagged as a performance violation by performance-critic.
ffe::rhi::TextureHandle loadTexture(const char* path);

// Convenience overload — specifies an asset root per-call without changing the global root.
// Useful for loading from multiple asset directories (e.g., engine assets vs game assets).
// The same path validation rules apply.
// assetRoot must be an absolute path to an existing directory.
ffe::rhi::TextureHandle loadTexture(const char* path, const char* assetRoot);

// Destroy a GPU texture and free all associated resources.
// After this call, handle.id must not be used again.
// Safe to call with an invalid handle (handle.id == 0) — no-op.
void unloadTexture(ffe::rhi::TextureHandle handle);

} // namespace ffe::renderer
```

### 3.2 Constants

```cpp
// Maximum texture dimension (width or height) accepted by loadTexture.
// Matches the existing RHI rule. Enforced before passing to stb_image
// and again after stb_image returns decoded dimensions.
static constexpr u32 MAX_TEXTURE_DIMENSION = 8192u;

// Maximum decoded pixel data size accepted before GPU upload.
// Guards against pathological images that pass dimension checks individually
// but have an enormous combined size (e.g., 8192 x 8192 x 4 = 268 MB).
// Implementation uses u64 arithmetic to avoid overflow in the check itself.
static constexpr u64 MAX_TEXTURE_BYTES = 256ull * 1024ull * 1024ull; // 256 MB
```

### 3.3 Non-API: stb_image Integration

- `#define STB_IMAGE_IMPLEMENTATION` appears in exactly one `.cpp` file: `engine/renderer/texture_loader.cpp`
- No other `.cpp` file in the engine defines it
- stb_image is included via `#include "../../third_party/stb_image.h"` (relative to `engine/renderer/`)
- stb_image is treated as a C library: included inside an `extern "C"` block or wrapped with `#pragma GCC diagnostic` to suppress warnings it generates under `-Wall -Wextra`

---

## 4. Security Constraints

This section is the primary input for the security-auditor shift-left review. Every constraint listed here MUST be enforced in the implementation before any merge. Constraints are numbered for traceability in review findings.

### SEC-1: Path Traversal Prevention

Path validation is the FIRST operation in `loadTexture()`. No filesystem call (not even `stat`, `access`, or `realpath`) is made before the path passes validation.

```
bool isPathSafe(const char* path):
    if path is null                         → reject
    if path is empty string                 → reject
    if path[0] == '/' or path[0] == '\\'   → reject (absolute Unix path)
    if path[1] == ':'                       → reject (drive letter: "C:")
    if path contains "../"                  → reject
    if path contains "..\\"                → reject
    if path contains "/.."                  → reject
    if path contains "\\.."                → reject
    if path contains null byte ('\0' other than terminator) → reject
    return true
```

After the string check passes, the implementation constructs the full path by concatenating `assetRoot + "/" + path` and calls `realpath()` (POSIX) to canonicalise it (resolving any remaining symlinks or encoded traversal sequences the string check may have missed). If `realpath()` fails (file not found, permission denied), return failure immediately — do not proceed to `fopen`.

After `realpath()` succeeds, verify that the canonical path begins with the asset root prefix. If it does not (symlink escape), reject and return failure.

Only after both the string check and the `realpath()` boundary check pass does the implementation call `fopen` (or stb_image's file-open path).

### SEC-2: Dimension Bounds

After stb_image returns, validate the decoded dimensions before any allocation or GPU upload:

```
if decoded_width  == 0 or decoded_width  > MAX_TEXTURE_DIMENSION → reject
if decoded_height == 0 or decoded_height > MAX_TEXTURE_DIMENSION → reject
```

This check also runs if the header-reported dimensions differ from the decoded result (see SEC-4).

### SEC-3: Integer Overflow in Size Calculation

The product `width * height * channels` must be computed with 64-bit arithmetic before any comparison or use:

```cpp
const u64 requiredBytes = static_cast<u64>(decoded_width)
                        * static_cast<u64>(decoded_height)
                        * static_cast<u64>(channels);
if (requiredBytes > MAX_TEXTURE_BYTES) {
    stbi_image_free(pixelData);
    FFE_LOG_ERROR("texture too large: %" PRIu64 " bytes", requiredBytes);
    return TextureHandle{0};
}
```

`width`, `height`, and `channels` are all `int` from stb_image. The cast to `u64` must happen before multiplication — never after.

### SEC-4: stb_image Output Validation

stb_image is a third-party C library that processes untrusted binary data. Its outputs are treated as untrusted until validated:

- Check that the returned pixel pointer is non-null (failed decode)
- Check that decoded `width > 0` and `height > 0` (degenerate image)
- Check that `channels` is in the range `[1, 4]` — stb_image should return 4 (RGBA) since we pass `desired_channels = 4`, but validate it anyway
- Do NOT assume the returned dimensions match any header values — stb_image may crop or decode to different dimensions under some corruption scenarios

### SEC-5: Memory Cleanup on All Paths

`stbi_image_free()` must be called on every code path that reaches a successful `stbi_load()` call, including all error paths after that point:

- Dimension check fails after decode → call `stbi_image_free()` before returning
- Size overflow check fails → call `stbi_image_free()` before returning
- `createTexture()` fails → call `stbi_image_free()` before returning
- Success path → call `stbi_image_free()` after `createTexture()` uploads the data to GPU

No `return TextureHandle{0}` after a successful `stbi_load()` without a preceding `stbi_image_free()`.

### SEC-6: Asset Root Validation

The asset root set via `setAssetRoot()` is validated at the time of the call:

- Must be a non-null, non-empty string
- Must be an absolute path (starts with `/` on POSIX)
- Must refer to an existing directory (`stat()` returns success with `S_ISDIR`)
- Stored as a fixed-size buffer (e.g., `char[4096]`) — not heap-allocated, not a `std::string` (avoids allocation at load time)
- If `setAssetRoot()` is never called, the default is the current working directory at engine startup time (captured once, not re-queried each load)

The asset root is set at Application startup, before any game or scripting code runs. It is not user-controllable at runtime.

### SEC-7: No Per-Frame Use

`loadTexture()` is explicitly marked as a non-hot-path function in its documentation and implementation. The function performs:
- String operations (path validation)
- `realpath()` (syscall)
- `fopen` (syscall)
- `stbi_load()` (heap allocation, file I/O, decode)
- `createTexture()` (GPU upload)

None of these are acceptable per-frame. The implementation does not attempt to optimise for call frequency. If a profiling tool shows `loadTexture()` on a hot path, that is a caller bug, not an engine bug.

---

## 5. Tier Support

| Tier | Supported | Notes |
|------|-----------|-------|
| RETRO | Yes | OpenGL 2.1 texture upload; stb_image decode is CPU-side and tier-independent |
| LEGACY | Yes | Primary target; OpenGL 3.3 |
| STANDARD | Yes | OpenGL 4.5 / Vulkan; same CPU-side decode path |
| MODERN | Yes | Vulkan; same CPU-side decode path |

The decode path (stb_image) is entirely CPU-side and tier-independent. Only the `createTexture()` upload path is tier-dependent, and that function already exists and handles all tiers.

---

## 6. Format Support

PNG, JPG, BMP, TGA are supported via stb_image. Format detection is by file content (stb_image's internal magic-byte detection), not by file extension. The implementation does NOT check file extensions.

All formats are decoded to RGBA8 (4 channels) by passing `desired_channels = 4` to `stbi_load()`. This simplifies the RHI path — `createTexture()` always receives `TextureFormat::RGBA8` data. RGB and greyscale images are promoted to RGBA by stb_image. The memory cost of the promotion is acceptable.

Animated formats (e.g., animated GIF, APNG) are not supported. stb_image loads the first frame only.

---

## 7. Error Handling

All errors are handled as follows:
- Log via `FFE_LOG_ERROR` with a specific message identifying the failure reason (path rejection, file not found, decode failure, dimension rejected, size overflow, GPU upload failure)
- Return `TextureHandle{0}` to the caller
- No exceptions, no abort, no `assert` that fires in release builds

The caller distinguishes failure by checking `rhi::isValid(handle)`. The specific error reason is in the log — the caller does not receive a structured error code. If a more structured error type is required in a future session, `Result<TextureHandle>` can be introduced without breaking existing callers.

---

## 8. stb_image Known Issues and Mitigations

stb_image is a widely-used library with a large attack surface. The following mitigations reduce risk:

- **Dimension limits enforced before decode is trusted** (SEC-2, SEC-4)
- **Size limit enforced before GPU upload** (SEC-3)
- **stb_image's outputs are not trusted without validation** (SEC-4)
- **No STBI_NO_FAILURE_STRINGS** — keep failure strings enabled so error paths log useful messages

Pinning to a specific stb_image version is recommended. Document the version in a comment at the top of `third_party/stb_image.h` (e.g., `// stb_image v2.29 — https://github.com/nothings/stb`).

---

## 9. Open Questions for security-auditor

The following questions are explicitly raised for the shift-left review. security-auditor should answer each and may add findings beyond these.

**Q1: Extension allowlisting.** Should path validation include an allowlist of file extensions (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.tga`) in addition to the traversal check? Arguments for: limits the file types stb_image will attempt to decode, reducing attack surface. Arguments against: stb_image detects format by content anyway; a renamed file bypasses an extension check trivially. What is security-auditor's recommendation?

**Q2: stb_image safety for untrusted assets.** Is stb_image safe enough for processing asset files from untrusted sources (e.g., downloaded mod content), given the dimension and size limits in SEC-2 and SEC-3? Or does it require an additional wrapper (e.g., pre-reading and validating the header before calling `stbi_load()`, or enforcing a file-size limit before opening)? What is security-auditor's recommendation?

**Q3: assetRoot as compile-time vs runtime constant.** Currently designed as a runtime-validated path set at Application startup. Should it instead be a compile-time constant (`-DFFE_ASSET_ROOT="/path/to/assets"`) to prevent any runtime mutation? Trade-off: compile-time constant is more secure but less flexible for shipped games where the asset directory varies by install location.

**Q4: TOCTOU between realpath() and fopen().** There is an inherent race condition: the path passes validation at `realpath()` time, but by `fopen()` time the filesystem may have changed (a symlink could be created between the two calls). This is a TOCTOU (time-of-check time-of-use) risk. Is this acceptable for FFE's threat model (local game assets, not a multi-user server)? What mitigation, if any, does security-auditor recommend?

**Q5: Null byte injection.** The path validation checks for `'\\0'` within the string, but C string functions stop at the first null byte. Is there a scenario where a crafted path string passed through the API could contain an embedded null that the validation loop misses but `realpath()` or `fopen()` interprets differently? Should the path length be bounded with a `strnlen()` check?

---

## 10. Implementation Checklist

This checklist is for engine-dev to verify before marking implementation complete. security-auditor uses this as the review framework.

- [ ] `third_party/stb_image.h` present, version noted in comment at top of file
- [ ] `STB_IMAGE_IMPLEMENTATION` defined in exactly one `.cpp` file
- [ ] `isPathSafe()` is the first operation in `loadTexture()` — before any syscall
- [ ] `realpath()` called after string validation, before `fopen`/`stbi_load`
- [ ] Canonical path prefix-checked against asset root after `realpath()`
- [ ] `stbi_load()` called with `desired_channels = 4`
- [ ] Width, height, channels validated after `stbi_load()` returns
- [ ] Size check uses `u64` arithmetic: `(u64)w * (u64)h * (u64)ch`
- [ ] `stbi_image_free()` called on every exit path after `stbi_load()` succeeds
- [ ] `createTexture()` receives `TextureFormat::RGBA8`
- [ ] `unloadTexture()` delegates to `rhi::destroyTexture()` and is safe to call with id=0
- [ ] `setAssetRoot()` validates the path is absolute and the directory exists
- [ ] All errors logged via `FFE_LOG_ERROR` with specific messages
- [ ] Function is documented as non-hot-path in the header
- [ ] Zero warnings on Clang-18 and GCC-13 with `-Wall -Wextra`
- [ ] `tests/renderer/test_texture_loader.cpp` includes path traversal and overflow tests
- [ ] security-auditor has reviewed the implementation (post-Phase-2 review)

---

## 11. Dependencies

| Dependency | Source | vcpkg.json change? |
|------------|--------|--------------------|
| stb_image | `third_party/stb_image.h` (embedded) | **No** — same pattern as glad |
| `ffe::rhi` | `engine/renderer/rhi.h` (existing) | No |
| `ffe::core` logging macros | `engine/core/log.h` (existing) | No |

No new vcpkg dependencies. The commit message must state: `feat(renderer): add texture loading via stb_image (header in third_party/)`.

---

*ADR-005 v1.0 — awaiting security-auditor shift-left review. Implementation blocked until review returns no CRITICAL or HIGH findings.*
