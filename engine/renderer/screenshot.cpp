// screenshot.cpp — Framebuffer capture to PNG via stb_image_write.
//
// stb_image_write.h is provided by the vcpkg 'stb' package (listed in vcpkg.json).
// STB_IMAGE_WRITE_IMPLEMENTATION is defined in exactly this file and no other.
//
// Cold path only — this function allocates on the heap and calls glReadPixels.
// Never call it per-frame.

// Suppress warnings stb_image_write generates under -Wall -Wextra.
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

#define STB_IMAGE_WRITE_IMPLEMENTATION
// stb_image_write.h is provided by the vcpkg 'stb' package (vcpkg.json).
// It is not vendored in third_party/stb/ — include via the vcpkg include root.
#include <stb_image_write.h>

#ifdef __clang__
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include "renderer/screenshot.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <glad/glad.h>

#include <cstring> // memcpy
#include <vector>  // pixel buffers (cold path — heap alloc is acceptable here)

namespace ffe::renderer {

bool captureFramebuffer(const char* path, int width, int height) {
    if (path == nullptr || path[0] == '\0') {
        FFE_LOG_ERROR("screenshot", "captureFramebuffer: path is null or empty");
        return false;
    }
    if (width <= 0 || height <= 0) {
        FFE_LOG_ERROR("screenshot", "captureFramebuffer: invalid dimensions %dx%d", width, height);
        return false;
    }

    // headless mode: rhi::s_headless is internal state. We detect it by checking
    // whether glReadPixels would be a no-op. The RHI exposes isHeadless() for this.
    if (rhi::isHeadless()) {
        FFE_LOG_WARN("screenshot", "captureFramebuffer: skipped in headless mode");
        return false;
    }

    const int stride = width * 4; // RGBA, 4 bytes per pixel

    // Cold path — heap allocation is acceptable here (this is never called per-frame).
    std::vector<unsigned char> pixels(static_cast<size_t>(stride) * static_cast<size_t>(height));
    std::vector<unsigned char> flipped(static_cast<size_t>(stride) * static_cast<size_t>(height));

    // Read framebuffer. glReadPixels reads from bottom-left (OpenGL convention).
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically: OpenGL row 0 is the bottom; PNG row 0 is the top.
    for (int row = 0; row < height; ++row) {
        const int srcRow = height - 1 - row;
        memcpy(flipped.data() + row * stride,
               pixels.data() + srcRow * stride,
               static_cast<size_t>(stride));
    }

    const int result = stbi_write_png(path, width, height, 4, flipped.data(), stride);
    if (result == 0) {
        FFE_LOG_ERROR("screenshot", "captureFramebuffer: stbi_write_png failed for \"%s\"", path);
        return false;
    }

    FFE_LOG_INFO("screenshot", "captureFramebuffer: wrote %dx%d PNG to \"%s\"", width, height, path);
    return true;
}

} // namespace ffe::renderer
