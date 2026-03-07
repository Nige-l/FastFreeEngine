// texture_loader.cpp — stb_image-based texture loading for FFE.
//
// stb_image v2.30 — https://github.com/nothings/stb (LOW-3: version pinned here)
// stb_image is a public-domain single-header library embedded in third_party/stb/.
// STB_IMAGE_IMPLEMENTATION is defined in exactly this file and no other.
//
// Security conditions implemented (from ADR-005-security-review.md):
//   HIGH-1:   SEC-4 (stb output validation) runs BEFORE SEC-3 (u64 multiplication).
//             Prevents negative stb int cast to u64 producing 0xFFFFFFFFFFFFFFFF.
//   HIGH-2:   Path concat buffer overflow check before strncat/realpath.
//   HIGH-3:   No cwd default — loadTexture() errors if setAssetRoot() not called.
//   MEDIUM-2: Pre-decode stat() file-size check and STBI_MAX_DIMENSIONS=8192 set.
//   MEDIUM-3: strnlen(path, PATH_MAX) >= PATH_MAX guard replaces null-byte scan.
//   SEC-1:    isPathSafe() is the FIRST operation in loadTexture() — before any syscall.
//   SEC-2:    Decoded dimension validation (w,h in [1, MAX_TEXTURE_DIMENSION]).
//   SEC-3:    u64 arithmetic for requiredBytes — AFTER SEC-4 validates channel/dim values.
//   SEC-4:    stb_image output pointer, w, h, channels validated before any use.
//   SEC-5:    stbi_image_free() called on every exit path after stbi_load() succeeds.
//   SEC-6:    Asset root validated as absolute, existing directory; stored in fixed buffer.
//   LOW-2:    setAssetRoot() is write-once — rejects further calls once set.
//   LOW-4:    UNC path rejection ("\\...").
//   MEDIUM-1: TOCTOU between realpath() and fopen() is accepted for FFE's local threat
//             model. An attacker with write access to the asset dir can place files
//             there directly. Documented here per security-auditor guidance.

// STBI_MAX_DIMENSIONS must be defined before including stb_image.h (MEDIUM-2)
#define STBI_MAX_DIMENSIONS 8192

// Suppress warnings stb_image generates under -Wall -Wextra (it's a C library).
// We push diagnostics, include, then pop — never suppressing FFE code.
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
    #pragma clang diagnostic ignored "-Wcast-qual"
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wreserved-identifier"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wdouble-promotion"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include "renderer/texture_loader.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <glad/glad.h>

// Cubemap constants — core in OpenGL 3.3 but not included in our minimal GLAD header.
// Values per the OpenGL 3.3 spec (Table 3.17).
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP                 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X      0x8515
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R                   0x8072
#endif
#ifndef GL_RGBA8
#define GL_RGBA8                            0x8058
#endif

#include <array>
#include <climits>    // PATH_MAX
#include <string>
#include <cstring>    // strnlen, strstr, memcpy
#include <sys/stat.h> // stat(), S_ISDIR
#include <stdlib.h>   // size_t
#include "core/platform.h"
#include <cinttypes>  // PRIu64

namespace ffe::renderer {

// --- Module state ---
// Fixed-size static struct — no heap allocation (SEC-6).
// Initialised to zero (root[0] == '\0', isSet == false).
static struct {
    char root[PATH_MAX + 1];
    bool isSet;
} s_state = {};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// isPathSafe — SEC-1: path traversal prevention.
// MUST be the first operation called on any caller-supplied path.
// Returns true only if the path is safe to concatenate and pass to realpath().
static bool isPathSafe(const char* const path) {
    // MEDIUM-3: bound path length with strnlen — do not rely on strlen
    // (protects against non-null-terminated or oversized buffers)
    if (!path) {
        return false;
    }
    const size_t pathLen = strnlen(path, PATH_MAX);
    if (pathLen >= PATH_MAX) {
        return false;
    }
    if (path[0] == '\0') {
        return false;
    }
    // Reject absolute Unix paths
    if (path[0] == '/') {
        return false;
    }
    // Reject absolute Windows paths (backslash root)
    if (path[0] == '\\') {
        return false;
    }
    // Reject drive letters (e.g., "C:")
    if (pathLen >= 2 && path[1] == ':') {
        return false;
    }
    // LOW-4: reject UNC paths ("\\server\share")
    if (path[0] == '\\' && path[1] == '\\') {
        return false;
    }
    // Reject traversal sequences
    if (strstr(path, "../") != nullptr) { return false; }
    if (strstr(path, "..\\") != nullptr) { return false; }
    if (strstr(path, "/..") != nullptr) { return false; }
    if (strstr(path, "\\..") != nullptr) { return false; }
    // Reject Windows Alternate Data Streams (e.g. "textures/sprite.png:stream").
    // Legitimate relative asset paths never contain ':'.
    // Drive-letter absolute paths ("C:\...") are already rejected above by the
    // path[1]==':' check, so any remaining ':' is always an ADS or device path.
    if (strchr(path, ':') != nullptr) { return false; }
    return true;
}

// validateAssetRoot — SEC-6: validate an asset root string for use.
// Does NOT write to s_state — purely a check.
static bool validateAssetRoot(const char* const absPath) {
    if (!absPath || absPath[0] == '\0') {
        return false;
    }
    // Must be absolute (start with '/')
    if (absPath[0] != '/') {
        return false;
    }
    // Must refer to an existing directory
    struct stat st{};
    if (::stat(absPath, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return false;
    }
    return true;
}

// loadTextureImpl — shared implementation for all loadTexture() overloads.
// assetRoot is already validated by the caller.
// params controls filter and wrap mode; the defaults match the original behaviour.
static ffe::rhi::TextureHandle loadTextureImpl(const char* const path,
                                               const char* const assetRoot,
                                               const ffe::renderer::TextureLoadParams& params) {
    // SEC-1: path safety check is FIRST — before any syscall
    if (!isPathSafe(path)) {
        FFE_LOG_ERROR("texture_loader", "loadTexture: unsafe path rejected: \"%s\"",
                      path ? path : "(null)");
        return ffe::rhi::TextureHandle{0};
    }

    // HIGH-2: Check combined path length before concatenation to prevent
    // stack buffer overflow. Buffer is PATH_MAX+1 bytes.
    const size_t rootLen = strnlen(assetRoot, PATH_MAX);
    const size_t pathLen = strnlen(path, PATH_MAX);
    // rootLen + '/' + pathLen + '\0' must fit in PATH_MAX+1
    if (rootLen + 1u + pathLen + 1u > static_cast<size_t>(PATH_MAX) + 1u) {
        FFE_LOG_ERROR("texture_loader", "loadTexture: concatenated path too long");
        return ffe::rhi::TextureHandle{0};
    }

    // Build full path: assetRoot + "/" + path
    char fullPath[PATH_MAX + 1];
    memcpy(fullPath, assetRoot, rootLen);
    fullPath[rootLen] = '/';
    memcpy(fullPath + rootLen + 1u, path, pathLen);
    fullPath[rootLen + 1u + pathLen] = '\0';

    // SEC-1 (continued): Canonicalise path to resolve any remaining
    // encoded traversal sequences or symlinks the string check may have missed.
    // MEDIUM-1 (TOCTOU): There is an inherent race between canonicalizePath() and fopen().
    // This is accepted for FFE's local threat model — a local attacker with write
    // access to the asset directory can place content there directly. Not a server.
    char canonPath[PATH_MAX + 1];
    if (!ffe::canonicalizePath(fullPath, canonPath, sizeof(canonPath))) {
        FFE_LOG_ERROR("texture_loader", "loadTexture: canonicalizePath() failed for \"%s\"", fullPath);
        return ffe::rhi::TextureHandle{0};
    }

    // SEC-1 (continued): Verify the canonical path begins with the asset root.
    // This catches symlink escapes that realpath() resolved outside the root.
    if (strncmp(canonPath, assetRoot, rootLen) != 0) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: canonical path \"%s\" escapes asset root \"%s\"",
                      canonPath, assetRoot);
        return ffe::rhi::TextureHandle{0};
    }
    // Also ensure the canonical path is not equal to the root itself
    // (which would mean the path resolved to the directory, not a file)
    if (canonPath[rootLen] != '/' && canonPath[rootLen] != '\0') {
        // rootLen == strlen(canonPath) is fine — path may sit directly in root
        // but we need the separator to confirm containment
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: canonical path \"%s\" escapes asset root \"%s\"",
                      canonPath, assetRoot);
        return ffe::rhi::TextureHandle{0};
    }

    // MEDIUM-2: Pre-decode file-size check via stat().
    // Rejects oversized files before stb_image spends CPU decoding them.
    {
        struct stat fileStat{};
        if (::stat(canonPath, &fileStat) == 0) {
            const u64 fileSize = static_cast<u64>(fileStat.st_size);
            if (fileSize > MAX_TEXTURE_FILE_BYTES) {
                FFE_LOG_ERROR("texture_loader",
                              "loadTexture: file too large (%" PRIu64 " bytes, max %" PRIu64 "): \"%s\"",
                              fileSize, MAX_TEXTURE_FILE_BYTES, canonPath);
                return ffe::rhi::TextureHandle{0};
            }
        }
        // If stat fails here (e.g., race), stbi_load will fail and we handle it below.
    }

    // Decode image. Force RGBA8 (desired_channels = 4) for consistent GPU upload.
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* const pixels = stbi_load(canonPath, &w, &h, &channels, 4);

    if (pixels == nullptr) {
        FFE_LOG_ERROR("texture_loader", "loadTexture: stbi_load failed for \"%s\": %s",
                      canonPath, stbi_failure_reason());
        return ffe::rhi::TextureHandle{0};
    }

    // SEC-4 BEFORE SEC-3 (HIGH-1): validate stb_image outputs before any arithmetic.
    // A negative int cast to u64 produces 0xFFFFFFFF... — SEC-4 must gate SEC-3.
    // channels: stb was asked for 4 (RGBA8) but we validate defensively.
    if (channels < 1 || channels > 4) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: stbi_load returned invalid channel count %d for \"%s\"",
                      channels, canonPath);
        stbi_image_free(pixels); // SEC-5
        return ffe::rhi::TextureHandle{0};
    }
    if (w <= 0 || h <= 0) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: stbi_load returned invalid dimensions w=%d h=%d for \"%s\"",
                      w, h, canonPath);
        stbi_image_free(pixels); // SEC-5
        return ffe::rhi::TextureHandle{0};
    }

    // SEC-2: dimension bounds check (after SEC-4 confirms w,h > 0)
    if (static_cast<u32>(w) > MAX_TEXTURE_DIMENSION ||
        static_cast<u32>(h) > MAX_TEXTURE_DIMENSION) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: texture dimensions %dx%d exceed MAX_TEXTURE_DIMENSION=%u for \"%s\"",
                      w, h, MAX_TEXTURE_DIMENSION, canonPath);
        stbi_image_free(pixels); // SEC-5
        return ffe::rhi::TextureHandle{0};
    }

    // SEC-3: integer overflow guard in size calculation.
    // Uses u64 arithmetic. This runs AFTER SEC-4 (HIGH-1 ordering requirement).
    // We always decoded 4 channels (RGBA8) — use 4, not channels, for size.
    constexpr u64 DECODED_CHANNELS = 4u;
    const u64 requiredBytes = static_cast<u64>(w)
                            * static_cast<u64>(h)
                            * DECODED_CHANNELS;
    if (requiredBytes > MAX_TEXTURE_BYTES) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: decoded size %" PRIu64 " bytes exceeds MAX_TEXTURE_BYTES=%" PRIu64
                      " for \"%s\"",
                      requiredBytes, MAX_TEXTURE_BYTES, canonPath);
        stbi_image_free(pixels); // SEC-5
        return ffe::rhi::TextureHandle{0};
    }

    // Upload to GPU via the RHI. TextureFormat::RGBA8 matches our forced decode.
    // filter and wrap come from caller-supplied params (default: LINEAR + CLAMP_TO_EDGE).
    const ffe::rhi::TextureDesc desc{
        static_cast<u32>(w),
        static_cast<u32>(h),
        ffe::rhi::TextureFormat::RGBA8,
        params.filter,
        params.wrap,
        false,    // generateMipmaps — caller can add mip support later
        pixels
    };

    const ffe::rhi::TextureHandle handle = ffe::rhi::createTexture(desc);

    // SEC-5: free CPU-side pixel data after GPU upload, whether or not upload succeeded.
    stbi_image_free(pixels);

    if (!ffe::rhi::isValid(handle)) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: rhi::createTexture() failed for \"%s\"", canonPath);
        return ffe::rhi::TextureHandle{0};
    }

    FFE_LOG_INFO("texture_loader", "loadTexture: loaded \"%s\" (%dx%d RGBA8)", canonPath, w, h);
    return handle;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool setAssetRoot(const char* const absolutePath) {
    // LOW-2: write-once semantics — reject further calls once a valid root is set.
    if (s_state.isSet) {
        FFE_LOG_WARN("texture_loader",
                     "setAssetRoot: asset root already set to \"%s\"; call ignored",
                     s_state.root);
        return false;
    }

    if (!validateAssetRoot(absolutePath)) {
        FFE_LOG_ERROR("texture_loader",
                      "setAssetRoot: path is null, empty, not absolute, or not a directory: \"%s\"",
                      absolutePath ? absolutePath : "(null)");
        return false;
    }

    const size_t len = strnlen(absolutePath, PATH_MAX);
    if (len >= PATH_MAX) {
        FFE_LOG_ERROR("texture_loader", "setAssetRoot: path too long");
        return false;
    }

    memcpy(s_state.root, absolutePath, len);
    s_state.root[len] = '\0';
    s_state.isSet = true;

    FFE_LOG_INFO("texture_loader", "setAssetRoot: asset root set to \"%s\"", s_state.root);
    return true;
}

const char* getAssetRoot() {
    return s_state.root; // Returns empty string if isSet is false
}

ffe::rhi::TextureHandle loadTexture(const char* const path) {
    // HIGH-3: reject if setAssetRoot() was never called — no cwd default.
    if (!s_state.isSet || s_state.root[0] == '\0') {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: called before setAssetRoot() — no asset root configured");
        return ffe::rhi::TextureHandle{0};
    }
    return loadTextureImpl(path, s_state.root, TextureLoadParams{});
}

ffe::rhi::TextureHandle loadTexture(const char* const path, const char* const assetRoot) {
    if (!validateAssetRoot(assetRoot)) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: provided assetRoot is invalid: \"%s\"",
                      assetRoot ? assetRoot : "(null)");
        return ffe::rhi::TextureHandle{0};
    }
    return loadTextureImpl(path, assetRoot, TextureLoadParams{});
}

ffe::rhi::TextureHandle loadTexture(const char* const path, const TextureLoadParams& params) {
    // HIGH-3: reject if setAssetRoot() was never called — no cwd default.
    if (!s_state.isSet || s_state.root[0] == '\0') {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: called before setAssetRoot() — no asset root configured");
        return ffe::rhi::TextureHandle{0};
    }
    return loadTextureImpl(path, s_state.root, params);
}

ffe::rhi::TextureHandle loadTexture(const char* const path, const char* const assetRoot,
                                    const TextureLoadParams& params) {
    if (!validateAssetRoot(assetRoot)) {
        FFE_LOG_ERROR("texture_loader",
                      "loadTexture: provided assetRoot is invalid: \"%s\"",
                      assetRoot ? assetRoot : "(null)");
        return ffe::rhi::TextureHandle{0};
    }
    return loadTextureImpl(path, assetRoot, params);
}

u32 loadCubemap(const std::array<std::string, 6>& facePaths) {
    // Require asset root to be set (same as loadTexture).
    if (!s_state.isSet || s_state.root[0] == '\0') {
        FFE_LOG_ERROR("texture_loader",
                      "loadCubemap: called before setAssetRoot() — no asset root configured");
        return 0;
    }

    // Require a GL context (glGenTextures is NULL in headless mode).
    if (glad_glGenTextures == nullptr) {
        FFE_LOG_WARN("texture_loader", "loadCubemap: no GL context (headless mode)");
        return 0;
    }

    GLuint cubemapId = 0;
    glGenTextures(1, &cubemapId);
    if (cubemapId == 0) {
        FFE_LOG_ERROR("texture_loader", "loadCubemap: glGenTextures failed");
        return 0;
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapId);

    // GL_TEXTURE_CUBE_MAP_POSITIVE_X + i gives the face target for face i.
    // Order: +X, -X, +Y, -Y, +Z, -Z.
    int expectedW = 0;
    int expectedH = 0;

    for (u32 i = 0; i < 6; ++i) {
        const std::string& facePath = facePaths[i];

        // Path safety check
        if (!isPathSafe(facePath.c_str())) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: unsafe path rejected for face %u: \"%s\"",
                          i, facePath.c_str());
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        // Build full path: assetRoot + "/" + facePath
        const size_t rootLen = strnlen(s_state.root, PATH_MAX);
        const size_t pathLen = facePath.size();
        if (rootLen + 1u + pathLen + 1u > static_cast<size_t>(PATH_MAX) + 1u) {
            FFE_LOG_ERROR("texture_loader", "loadCubemap: concatenated path too long for face %u", i);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        char fullPath[PATH_MAX + 1];
        memcpy(fullPath, s_state.root, rootLen);
        fullPath[rootLen] = '/';
        memcpy(fullPath + rootLen + 1u, facePath.c_str(), pathLen);
        fullPath[rootLen + 1u + pathLen] = '\0';

        // Canonicalize and verify under asset root
        char canonPath[PATH_MAX + 1];
        if (!ffe::canonicalizePath(fullPath, canonPath, sizeof(canonPath))) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: canonicalizePath() failed for face %u: \"%s\"",
                          i, fullPath);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }
        if (strncmp(canonPath, s_state.root, rootLen) != 0) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: face %u path escapes asset root", i);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }
        // Separator check: confirm canonical path is truly under root, not a
        // prefix match (e.g., root="/assets" must not match "/assets_evil/").
        if (canonPath[rootLen] != '/' && canonPath[rootLen] != '\0') {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: face %u canonical path escapes asset root", i);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        // Pre-decode file-size check via stat() (mirrors loadTextureImpl MEDIUM-2).
        {
            struct stat fileStat{};
            if (::stat(canonPath, &fileStat) == 0) {
                const u64 fileSize = static_cast<u64>(fileStat.st_size);
                if (fileSize > MAX_TEXTURE_FILE_BYTES) {
                    FFE_LOG_ERROR("texture_loader",
                                  "loadCubemap: face %u file too large (%" PRIu64 " bytes, max %" PRIu64 "): \"%s\"",
                                  i, fileSize, MAX_TEXTURE_FILE_BYTES, canonPath);
                    glDeleteTextures(1, &cubemapId);
                    return 0;
                }
            }
            // If stat fails here, stbi_load will fail and we handle it below.
        }

        // Decode image via stb_image — force RGBA (4 channels)
        int w = 0;
        int h = 0;
        int channels = 0;
        stbi_uc* const pixels = stbi_load(canonPath, &w, &h, &channels, 4);
        if (pixels == nullptr) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: stbi_load failed for face %u (\"%s\"): %s",
                          i, canonPath, stbi_failure_reason());
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        // Validate dimensions
        if (w <= 0 || h <= 0 ||
            static_cast<u32>(w) > MAX_TEXTURE_DIMENSION ||
            static_cast<u32>(h) > MAX_TEXTURE_DIMENSION) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: face %u has invalid dimensions %dx%d", i, w, h);
            stbi_image_free(pixels);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        // Post-decode byte-count check (mirrors loadTextureImpl SEC-3).
        constexpr u64 DECODED_CHANNELS = 4u;
        const u64 requiredBytes = static_cast<u64>(w)
                                * static_cast<u64>(h)
                                * DECODED_CHANNELS;
        if (requiredBytes > MAX_TEXTURE_BYTES) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: face %u decoded size %" PRIu64 " bytes exceeds MAX_TEXTURE_BYTES=%" PRIu64,
                          i, requiredBytes, MAX_TEXTURE_BYTES);
            stbi_image_free(pixels);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        // All 6 faces must have the same dimensions.
        if (i == 0) {
            expectedW = w;
            expectedH = h;
        } else if (w != expectedW || h != expectedH) {
            FFE_LOG_ERROR("texture_loader",
                          "loadCubemap: face %u dimensions %dx%d differ from face 0 (%dx%d)",
                          i, w, h, expectedW, expectedH);
            stbi_image_free(pixels);
            glDeleteTextures(1, &cubemapId);
            return 0;
        }

        // Upload face to the cubemap texture
        glTexImage2D(
            static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i),
            0, GL_RGBA8,
            static_cast<GLsizei>(w), static_cast<GLsizei>(h),
            0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        stbi_image_free(pixels);
    }

    // Cubemap sampling parameters — LINEAR filter, CLAMP_TO_EDGE on all 3 axes.
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    FFE_LOG_INFO("texture_loader",
                 "loadCubemap: loaded 6 faces (%dx%d RGBA8), GL id=%u",
                 expectedW, expectedH, cubemapId);
    return cubemapId;
}

void unloadTexture(const ffe::rhi::TextureHandle handle) {
    if (!ffe::rhi::isValid(handle)) {
        return; // Safe no-op for invalid/null handles
    }
    ffe::rhi::destroyTexture(handle);
}

void unloadCubemap(const u32 textureId) {
    if (textureId == 0) return;
    glDeleteTextures(1, &textureId);
}

} // namespace ffe::renderer
