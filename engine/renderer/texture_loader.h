#pragma once

#include "renderer/rhi_types.h"
#include "core/types.h"

#include <array>
#include <string>

// texture_loader.h — load image files from disk and upload to GPU as textures.
//
// NOT a per-frame API. All functions perform file I/O and/or GPU uploads.
// Call at scene load time or Application::startup(). Never call per-frame.
// Calling loadTexture() from a hot path is a performance violation.
//
// Thread safety: single-threaded. All functions must be called from the render thread.
//
// Tier support: RETRO, LEGACY (primary), STANDARD, MODERN.
// The decode path (stb_image) is CPU-side and tier-independent.
// The upload path delegates to rhi::createTexture(), which handles all tiers.

namespace ffe::renderer {

// Maximum texture dimension (width or height). Matches RHI limit.
// Enforced before and after stb_image decoding.
static constexpr u32 MAX_TEXTURE_DIMENSION = 8192u;

// Maximum decoded pixel data size. Guards against pathological images
// that pass per-dimension checks but have an enormous combined size.
// u64 arithmetic is used in the check to avoid overflow (SEC-3).
static constexpr u64 MAX_TEXTURE_BYTES = 256ull * 1024ull * 1024ull;

// Maximum file size accepted before calling stb_image. Pre-decode guard (MEDIUM-2).
static constexpr u64 MAX_TEXTURE_FILE_BYTES = 64ull * 1024ull * 1024ull;

// --- Load Parameters ---

// Optional parameters for loadTexture(). All fields have defaults that match
// the behaviour of the original zero-parameter overloads (LINEAR + CLAMP_TO_EDGE).
//
// filter: how the GPU samples the texture when it is scaled.
//   NEAREST — hard pixels, no blurring. Use for pixel art.
//   LINEAR  — smooth interpolation. Use for photographs and smooth art.
//
// wrap: what happens when UV coordinates fall outside [0, 1].
//   CLAMP_TO_EDGE — UV is clamped to the edge texel. Default; no repetition.
//   REPEAT        — UV wraps around; texture tiles.
struct TextureLoadParams {
    ffe::rhi::TextureFilter filter = ffe::rhi::TextureFilter::LINEAR;
    ffe::rhi::TextureWrap   wrap   = ffe::rhi::TextureWrap::CLAMP_TO_EDGE;
};

// --- Asset Root ---

// Set the root directory for all texture loads. Must be called before loadTexture().
//
// path must be an absolute, existing directory.
// Write-once: once set to a valid path, further calls are rejected (returns false).
// This prevents runtime redirection of asset loads from untrusted code (LOW-2).
//
// Returns true on success.
// Returns false if: already set, path is null/empty, path is not absolute,
//                   or path does not refer to an existing directory.
bool setAssetRoot(const char* absolutePath);

// Query the current asset root (for diagnostics and test verification).
// Returns an empty string if setAssetRoot() has not been called successfully.
const char* getAssetRoot();

// --- Texture Loading ---

// Load an image file and upload it to the GPU as a texture.
//
// path must be a RELATIVE path with no traversal sequences.
// Rejected paths: starting with '/' or '\', containing drive letters ("C:"),
//                 containing "../", "..\", "/..", "\..".
// Rejected if strnlen(path, PATH_MAX) >= PATH_MAX.
//
// Format is detected from file content (magic bytes), not extension.
// Supported: PNG, JPG, BMP, TGA. All decoded to RGBA8 (4 channels).
//
// On success: returns a valid TextureHandle (handle.id != 0).
//             Caller owns the handle and must call unloadTexture() when done.
// On failure: returns TextureHandle{0} and logs via FFE_LOG_ERROR.
//
// Performance note: performs realpath(), fopen(), stbi_load(), and GPU upload.
//                   NOT safe for per-frame use. Calling per-frame is a bug.
ffe::rhi::TextureHandle loadTexture(const char* path);

// Overload: load from a specific asset root without changing the global root.
// Useful when loading from multiple asset directories (e.g., engine vs game assets).
// assetRoot must be an absolute path to an existing directory.
// All path validation rules apply to path.
ffe::rhi::TextureHandle loadTexture(const char* path, const char* assetRoot);

// Overloads with explicit TextureLoadParams.
// These allow caller control over filter and wrap mode.
// Examples:
//   // Pixel-art sprite — no blurring
//   auto tex = loadTexture("hero.png", {ffe::rhi::TextureFilter::NEAREST,
//                                       ffe::rhi::TextureWrap::CLAMP_TO_EDGE});
//   // Tiling terrain texture
//   auto tex = loadTexture("grass.png", {ffe::rhi::TextureFilter::LINEAR,
//                                        ffe::rhi::TextureWrap::REPEAT});
ffe::rhi::TextureHandle loadTexture(const char* path, const TextureLoadParams& params);
ffe::rhi::TextureHandle loadTexture(const char* path, const char* assetRoot,
                                    const TextureLoadParams& params);

// --- Cubemap Loading ---

// Load 6 face images into a GL_TEXTURE_CUBE_MAP.
// Face order: +X (right), -X (left), +Y (top), -Y (bottom), +Z (front), -Z (back).
// All 6 faces must be the same dimensions and decode as RGBA8.
// Returns the raw GL texture ID (GLuint) — NOT a TextureHandle (the RHI does not
// track cubemaps). Returns 0 on failure (missing file, dimension mismatch, etc.).
// Uses the global asset root set by setAssetRoot(). NOT a per-frame API.
//
// Caller must delete the returned texture via unloadCubemap() when done.
// Thread safety: single-threaded, render thread only.
//
// Tier support: LEGACY (OpenGL 3.3 core). GL_TEXTURE_CUBE_MAP is core in 3.3.
u32 loadCubemap(const std::array<std::string, 6>& facePaths);

// --- Texture Destruction ---

// Destroy a GPU texture created by loadTexture().
// Safe to call with an invalid handle (handle.id == 0) — no-op.
// After this call, handle.id must not be used again.
void unloadTexture(ffe::rhi::TextureHandle handle);

// Destroy a cubemap texture created by loadCubemap().
// Safe to call with textureId == 0 — no-op.
// After this call, textureId must not be used again.
// This exists so that callers outside the renderer (e.g. scripting, application)
// do not need to include glad/GL headers to delete cubemap textures.
void unloadCubemap(u32 textureId);

} // namespace ffe::renderer
