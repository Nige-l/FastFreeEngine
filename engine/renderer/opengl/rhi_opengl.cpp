#include "renderer/rhi.h"
#include "renderer/opengl/rhi_opengl.h"
#include "renderer/opengl/gl_debug.h"
#include "core/logging.h"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <tracy/Tracy.hpp>

#include <cstring>

// GL 3.3 texture swizzle constants — missing from our GLAD generation.
// These are core GL 3.3 enums (ARB_texture_swizzle), values per the OpenGL spec.
#ifndef GL_TEXTURE_SWIZZLE_R
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#endif
#ifndef GL_TEXTURE_SWIZZLE_G
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#endif
#ifndef GL_TEXTURE_SWIZZLE_B
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif

namespace ffe::rhi {

// --- Internal state ---
static bool s_headless = false;
static bool s_softwareRenderer = false;
static bool s_initialized = false;
static i32  s_viewportWidth  = 0;
static i32  s_viewportHeight = 0;

// Cached view-projection matrix
static glm::mat4 s_viewProjection{1.0f};

// Current pipeline state for redundant state change elimination
static PipelineState s_currentPipeline;

// Global VAO (GL 3.3 requires a VAO to be bound)
static GLuint s_globalVao = 0;

// Sprite VAO — captures vertex attribute state so we set attribs once, not per draw call
static GLuint s_spriteVao = 0;
static BufferHandle s_spriteVaoBoundVbo{}; // Which VBO the sprite VAO was configured for

// --- Buffer pool ---
struct GlBuffer {
    GLuint glId       = 0;
    BufferType type   = BufferType::VERTEX;
    BufferUsage usage = BufferUsage::STATIC;
    u32 sizeBytes     = 0;
    bool alive        = false;
};

static constexpr u32 MAX_BUFFERS = 4096;
static GlBuffer s_buffers[MAX_BUFFERS];
static u32 s_nextBufferSlot = 1; // Slot 0 is reserved (null handle)

// --- Texture pool ---
struct GlTexture {
    GLuint glId        = 0;
    u32 width          = 0;
    u32 height         = 0;
    TextureFormat fmt  = TextureFormat::RGBA8;
    u32 vramBytes      = 0;
    bool alive         = false;
};

static constexpr u32 MAX_TEXTURES = 2048;
static GlTexture s_textures[MAX_TEXTURES];
static u32 s_nextTextureSlot = 1;
static u32 s_totalTextureVram = 0;

static constexpr u32 VRAM_BUDGET_BYTES = 500 * 1024 * 1024;
static constexpr u32 VRAM_WARNING_THRESHOLD = static_cast<u32>(static_cast<u64>(VRAM_BUDGET_BYTES) * 80 / 100);

// --- Shader pool ---
struct GlShader {
    GLuint programId     = 0;
    GLuint vertexId      = 0;
    GLuint fragmentId    = 0;
    bool alive           = false;
    static constexpr u32 MAX_CACHED_UNIFORMS = 64;
    struct UniformEntry {
        u32 nameHash = 0;
        GLint location = -1;
    };
    UniformEntry uniformCache[MAX_CACHED_UNIFORMS];
    u32 uniformCount = 0;
};

static constexpr u32 MAX_SHADERS = 256;
static GlShader s_shaders[MAX_SHADERS];
static u32 s_nextShaderSlot = 1;

// Currently bound shader (for uniform caching)
static ShaderHandle s_currentShader{};

// --- Headless handle counters ---
static u32 s_headlessBufferNext = 1;
static u32 s_headlessTextureNext = 1;
static u32 s_headlessShaderNext = 1;

// --- Internal helpers ---

namespace detail {

GLenum toGlBufferTarget(const BufferType type) {
    switch (type) {
        case BufferType::VERTEX:  return GL_ARRAY_BUFFER;
        case BufferType::INDEX:   return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::UNIFORM: return GL_UNIFORM_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

GLenum toGlBufferUsage(const BufferUsage usage) {
    switch (usage) {
        case BufferUsage::STATIC:  return GL_STATIC_DRAW;
        case BufferUsage::DYNAMIC: return GL_DYNAMIC_DRAW;
        case BufferUsage::STREAM:  return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

GLenum toGlTextureFormat(const TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:   return GL_RGBA;
        case TextureFormat::RGB8:    return GL_RGB;
        case TextureFormat::R8:      return GL_RED;
        case TextureFormat::RGBA16F: return GL_RGBA;
    }
    return GL_RGBA;
}

GLenum toGlTextureInternalFormat(const TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:   return GL_RGBA8;
        case TextureFormat::RGB8:    return GL_RGB8;
        case TextureFormat::R8:      return GL_R8;
        case TextureFormat::RGBA16F: return GL_RGBA16F;
    }
    return GL_RGBA8;
}

GLenum toGlTextureDataType(const TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:   return GL_UNSIGNED_BYTE;
        case TextureFormat::RGB8:    return GL_UNSIGNED_BYTE;
        case TextureFormat::R8:      return GL_UNSIGNED_BYTE;
        case TextureFormat::RGBA16F: return GL_HALF_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}

GLint toGlTextureFilter(const TextureFilter filter) {
    switch (filter) {
        case TextureFilter::NEAREST:                return GL_NEAREST;
        case TextureFilter::LINEAR:                 return GL_LINEAR;
        case TextureFilter::NEAREST_MIPMAP_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
        case TextureFilter::LINEAR_MIPMAP_LINEAR:   return GL_LINEAR_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

GLint toGlTextureWrap(const TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap::REPEAT:          return GL_REPEAT;
        case TextureWrap::CLAMP_TO_EDGE:   return GL_CLAMP_TO_EDGE;
        case TextureWrap::MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

u32 bytesPerPixel(const TextureFormat fmt) {
    switch (fmt) {
        case TextureFormat::RGBA8:   return 4;
        case TextureFormat::RGB8:    return 3;
        case TextureFormat::R8:      return 1;
        case TextureFormat::RGBA16F: return 8;
    }
    return 4;
}

} // namespace detail

// --- Uniform location caching ---
static GLint getCachedUniformLocation(GlShader& shader, const char* name) {
    // FNV-1a hash
    u32 hash = 2166136261u;
    for (const char* p = name; *p; ++p) {
        hash ^= static_cast<u32>(static_cast<u8>(*p));
        hash *= 16777619u;
    }

    // Search cache
    for (u32 i = 0; i < shader.uniformCount; ++i) {
        if (shader.uniformCache[i].nameHash == hash) {
            return shader.uniformCache[i].location;
        }
    }

    // Cache miss
    const GLint loc = glGetUniformLocation(shader.programId, name);
    if (shader.uniformCount < GlShader::MAX_CACHED_UNIFORMS) {
        shader.uniformCache[shader.uniformCount] = {hash, loc};
        shader.uniformCount++;
    } else {
        FFE_LOG_WARN("Renderer", "Uniform cache full for shader — consider increasing MAX_CACHED_UNIFORMS");
    }

    return loc;
}

// --- Find a free slot (linear scan for simplicity — only done at creation time, not per frame) ---
template<typename T, u32 N>
static u32 findFreeSlot(T (&pool)[N], u32& nextSlot) {
    // Try the fast path: next sequential slot
    if (nextSlot < N && !pool[nextSlot].alive) {
        return nextSlot++;
    }
    // Linear scan for a free slot
    for (u32 i = 1; i < N; ++i) {
        if (!pool[i].alive) {
            if (i >= nextSlot) nextSlot = i + 1;
            return i;
        }
    }
    return 0; // No free slots
}

// ==================== RHI Implementation ====================

RhiResult init(const RhiConfig& config) {
    s_headless = config.headless;
    s_viewportWidth  = config.viewportWidth;
    s_viewportHeight = config.viewportHeight;

    if (s_headless) {
        FFE_LOG_INFO("Renderer", "Headless mode — no GPU operations");
        s_initialized = true;
        return RhiResult::OK;
    }

    // glad should already be loaded by Application before calling rhi::init()
    const auto* rendererStr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    FFE_LOG_INFO("Renderer", "OpenGL %d.%d loaded — %s",
                 GLVersion_major, GLVersion_minor,
                 rendererStr ? rendererStr : "unknown");

    // Detect software renderers (Mesa llvmpipe, swrast, softpipe, etc.)
    // These have limited FBO / floating-point texture support, so advanced
    // effects (HDR post-processing, SSAO, shadow mapping) may produce
    // black or corrupted output. Games can query isSoftwareRenderer() to
    // gracefully skip effects that would fail.
    s_softwareRenderer = false;
    if (rendererStr != nullptr) {
        // Case-insensitive substring checks for known software renderer strings
        // Simple inline search — avoid pulling in <algorithm> for one check
        auto contains = [](const char* haystack, const char* needle) -> bool {
            for (const char* h = haystack; *h != '\0'; ++h) {
                const char* hi = h;
                const char* ni = needle;
                while (*ni != '\0' && *hi != '\0') {
                    // Lowercase compare
                    char a = *hi;
                    char b = *ni;
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                    if (a != b) break;
                    ++hi;
                    ++ni;
                }
                if (*ni == '\0') return true;
            }
            return false;
        };
        if (contains(rendererStr, "llvmpipe") || contains(rendererStr, "softpipe") ||
            contains(rendererStr, "swrast") || contains(rendererStr, "software")) {
            s_softwareRenderer = true;
            FFE_LOG_WARN("Renderer", "Software renderer detected — advanced effects may be degraded");
        }
    }

    // Setup debug output if requested
    if (config.debugGL) {
        gl::setupDebugOutput();
    }

    // Create global VAO
    glGenVertexArrays(1, &s_globalVao);
    glBindVertexArray(s_globalVao);

    // Set default viewport
    glViewport(0, 0, config.viewportWidth, config.viewportHeight);

    // Default state
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Reset pipeline state tracking
    s_currentPipeline = PipelineState{};
    s_currentPipeline.blend = BlendMode::ALPHA;
    s_currentPipeline.depth = DepthFunc::NONE;
    s_currentPipeline.cull = CullMode::NONE;
    s_currentPipeline.depthWrite = true;
    s_currentPipeline.wireframe = false;

    s_initialized = true;
    FFE_LOG_INFO("Renderer", "RHI initialized (OpenGL 3.3 backend)");
    return RhiResult::OK;
}

void shutdown() {
    if (!s_initialized) return;

    if (!s_headless) {
        // Destroy any remaining resources and log warnings
        for (u32 i = 1; i < MAX_SHADERS; ++i) {
            if (s_shaders[i].alive) {
                FFE_LOG_WARN("Renderer", "Shader %u leaked — destroying in shutdown", i);
                glDeleteProgram(s_shaders[i].programId);
                glDeleteShader(s_shaders[i].vertexId);
                glDeleteShader(s_shaders[i].fragmentId);
                s_shaders[i].alive = false;
            }
        }

        for (u32 i = 1; i < MAX_TEXTURES; ++i) {
            if (s_textures[i].alive) {
                FFE_LOG_WARN("Renderer", "Texture %u leaked — destroying in shutdown", i);
                glDeleteTextures(1, &s_textures[i].glId);
                s_textures[i].alive = false;
            }
        }

        for (u32 i = 1; i < MAX_BUFFERS; ++i) {
            if (s_buffers[i].alive) {
                FFE_LOG_WARN("Renderer", "Buffer %u leaked — destroying in shutdown", i);
                glDeleteBuffers(1, &s_buffers[i].glId);
                s_buffers[i].alive = false;
            }
        }

        if (s_spriteVao != 0) {
            glDeleteVertexArrays(1, &s_spriteVao);
            s_spriteVao = 0;
            s_spriteVaoBoundVbo = {};
        }

        if (s_globalVao != 0) {
            glDeleteVertexArrays(1, &s_globalVao);
            s_globalVao = 0;
        }
    }

    // Reset all state
    s_nextBufferSlot = 1;
    s_nextTextureSlot = 1;
    s_nextShaderSlot = 1;
    s_totalTextureVram = 0;
    s_headlessBufferNext = 1;
    s_headlessTextureNext = 1;
    s_headlessShaderNext = 1;
    s_currentShader = {};
    s_viewportWidth  = 0;
    s_viewportHeight = 0;
    s_initialized = false;

    FFE_LOG_INFO("Renderer", "RHI shutdown complete");
}

// --- Buffers ---

BufferHandle createBuffer(const BufferDesc& desc) {
    if (s_headless) {
        return BufferHandle{s_headlessBufferNext++};
    }

    const u32 slot = findFreeSlot(s_buffers, s_nextBufferSlot);
    if (slot == 0) {
        FFE_LOG_ERROR("Renderer", "Buffer pool exhausted (max %u)", MAX_BUFFERS);
        return BufferHandle{0};
    }

    GlBuffer& buf = s_buffers[slot];
    buf.type = desc.type;
    buf.usage = desc.usage;
    buf.sizeBytes = desc.sizeBytes;
    buf.alive = true;

    glGenBuffers(1, &buf.glId);

    const GLenum target = detail::toGlBufferTarget(desc.type);
    glBindBuffer(target, buf.glId);
    glBufferData(target, desc.sizeBytes, desc.data, detail::toGlBufferUsage(desc.usage));
    glBindBuffer(target, 0);

    return BufferHandle{slot};
}

RhiResult updateBuffer(const BufferHandle handle, const void* data, const u32 sizeBytes, const u32 offset) {
    if (data == nullptr) return RhiResult::ERROR_INVALID_HANDLE;

    if (s_headless) return RhiResult::OK;

    if (handle.id == 0 || handle.id >= MAX_BUFFERS || !s_buffers[handle.id].alive) {
        return RhiResult::ERROR_INVALID_HANDLE;
    }

    const GlBuffer& buf = s_buffers[handle.id];
    // M-7: cast to u64 before addition to prevent u32 overflow
    if (static_cast<u64>(offset) + static_cast<u64>(sizeBytes) > static_cast<u64>(buf.sizeBytes)) {
        FFE_LOG_ERROR("Renderer", "Buffer update exceeds buffer size");
        return RhiResult::ERROR_INVALID_HANDLE;
    }

    const GLenum target = detail::toGlBufferTarget(buf.type);
    glBindBuffer(target, buf.glId);
    glBufferSubData(target, offset, sizeBytes, data);
    glBindBuffer(target, 0);

    return RhiResult::OK;
}

void destroyBuffer(const BufferHandle handle) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_BUFFERS) return;

    GlBuffer& buf = s_buffers[handle.id];
    if (!buf.alive) return;

    glDeleteBuffers(1, &buf.glId);
    buf = GlBuffer{};
}

// --- Textures ---

// Conservative max texture dimension for LEGACY tier (8192x8192)
static constexpr u32 MAX_TEXTURE_DIMENSION = 8192;

TextureHandle createTexture(const TextureDesc& desc) {
    // Validate dimensions regardless of headless mode
    if (desc.width == 0 || desc.height == 0 ||
        desc.width > MAX_TEXTURE_DIMENSION || desc.height > MAX_TEXTURE_DIMENSION) {
        FFE_LOG_ERROR("Renderer", "Invalid texture dimensions %ux%u (max %u)",
                      desc.width, desc.height, MAX_TEXTURE_DIMENSION);
        return TextureHandle{0};
    }

    if (s_headless) {
        // In headless mode, still populate s_textures for dimension queries
        // (used by the texture atlas for packing math). Skip all GL calls.
        const u32 slot = s_headlessTextureNext++;
        if (slot < MAX_TEXTURES) {
            GlTexture& tex = s_textures[slot];
            tex.glId   = 0;
            tex.width  = desc.width;
            tex.height = desc.height;
            tex.fmt    = desc.format;
            tex.alive  = true;
            tex.vramBytes = 0;
        }
        return TextureHandle{slot};
    }

    const u32 slot = findFreeSlot(s_textures, s_nextTextureSlot);
    if (slot == 0) {
        FFE_LOG_ERROR("Renderer", "Texture pool exhausted (max %u)", MAX_TEXTURES);
        return TextureHandle{0};
    }

    GlTexture& tex = s_textures[slot];
    tex.width = desc.width;
    tex.height = desc.height;
    tex.fmt = desc.format;
    tex.alive = true;

    const u32 bpp = detail::bytesPerPixel(desc.format);
    const u64 vramBytes64 = static_cast<u64>(desc.width) * desc.height * bpp;
    const u64 vramWithMips = desc.generateMipmaps ? (vramBytes64 * 4 / 3) : vramBytes64;

    // Guard against overflow and exceeding VRAM budget before touching the GPU
    if (vramWithMips > VRAM_BUDGET_BYTES ||
        (static_cast<u64>(s_totalTextureVram) + vramWithMips) > VRAM_BUDGET_BYTES) {
        FFE_LOG_ERROR("Renderer", "Texture VRAM would exceed budget (%u MB), rejecting",
                      VRAM_BUDGET_BYTES / (1024 * 1024));
        tex = GlTexture{};
        return TextureHandle{0};
    }

    tex.vramBytes = static_cast<u32>(vramWithMips);
    s_totalTextureVram += tex.vramBytes;
    if (s_totalTextureVram > VRAM_WARNING_THRESHOLD) {
        FFE_LOG_WARN("Renderer", "Texture VRAM usage at %u MB (budget: %u MB)",
                     s_totalTextureVram / (1024 * 1024), VRAM_BUDGET_BYTES / (1024 * 1024));
    }

    glGenTextures(1, &tex.glId);
    glBindTexture(GL_TEXTURE_2D, tex.glId);

    // Set unpack alignment for non-RGBA formats
    if (desc.format == TextureFormat::R8) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    } else if (desc.format == TextureFormat::RGB8) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    glTexImage2D(GL_TEXTURE_2D, 0,
                 static_cast<GLint>(detail::toGlTextureInternalFormat(desc.format)),
                 static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
                 0,
                 detail::toGlTextureFormat(desc.format),
                 detail::toGlTextureDataType(desc.format),
                 desc.pixelData);

    // Restore default alignment
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, detail::toGlTextureFilter(desc.filter));

    // Mag filter: strip mipmap from filter for mag
    GLint magFilter = GL_LINEAR;
    if (desc.filter == TextureFilter::NEAREST || desc.filter == TextureFilter::NEAREST_MIPMAP_NEAREST) {
        magFilter = GL_NEAREST;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, detail::toGlTextureWrap(desc.wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, detail::toGlTextureWrap(desc.wrap));

    // Apply R-to-alpha swizzle for single-channel font atlases.
    // Maps RGBA sampling to (1, 1, 1, red_channel) so the standard RGBA
    // sprite shader can render coverage-based glyphs with tint color.
    if (desc.swizzleRedToAlpha && desc.format == TextureFormat::R8) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
    }

    if (desc.generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    return TextureHandle{slot};
}

void bindTexture(const TextureHandle handle, const u32 unitIndex) {
    if (s_headless) return;

    glActiveTexture(GL_TEXTURE0 + unitIndex);
    if (handle.id == 0 || handle.id >= MAX_TEXTURES || !s_textures[handle.id].alive) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, s_textures[handle.id].glId);
}

void destroyTexture(const TextureHandle handle) {
    if (handle.id == 0 || handle.id >= MAX_TEXTURES) return;

    GlTexture& tex = s_textures[handle.id];
    if (!tex.alive) return;

    if (s_headless) {
        // In headless mode, just clear the metadata (no GL resources to free)
        tex = GlTexture{};
        return;
    }

    s_totalTextureVram -= tex.vramBytes;
    glDeleteTextures(1, &tex.glId);
    tex = GlTexture{};
}

u32 textureVramUsed() {
    return s_totalTextureVram;
}

u32 getTextureWidth(const TextureHandle handle) {
    if (handle.id == 0 || handle.id >= MAX_TEXTURES) return 0;
    const GlTexture& tex = s_textures[handle.id];
    if (!tex.alive) return 0;
    return tex.width;
}

u32 getTextureHeight(const TextureHandle handle) {
    if (handle.id == 0 || handle.id >= MAX_TEXTURES) return 0;
    const GlTexture& tex = s_textures[handle.id];
    if (!tex.alive) return 0;
    return tex.height;
}

void updateTextureSubImage(const TextureHandle handle, const u32 x, const u32 y,
                           const u32 width, const u32 height, const void* pixelData) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_TEXTURES) return;
    const GlTexture& tex = s_textures[handle.id];
    if (!tex.alive) return;
    if (pixelData == nullptr) return;
    if (x + width > tex.width || y + height > tex.height) return;

    glBindTexture(GL_TEXTURE_2D, tex.glId);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    static_cast<GLint>(x), static_cast<GLint>(y),
                    static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                    GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool readTexturePixels(const TextureHandle handle, void* outBuffer, const u32 bufferSize) {
    if (s_headless) return false;
    if (handle.id == 0 || handle.id >= MAX_TEXTURES) return false;
    const GlTexture& tex = s_textures[handle.id];
    if (!tex.alive) return false;
    if (outBuffer == nullptr) return false;

    // Compute required buffer size (RGBA8 = 4 bytes per pixel)
    const u64 required = static_cast<u64>(tex.width) * static_cast<u64>(tex.height) * 4u;
    if (static_cast<u64>(bufferSize) < required) return false;

    glBindTexture(GL_TEXTURE_2D, tex.glId);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, outBuffer);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// --- Shaders ---

static GLuint compileShaderStage(const GLenum stage, const char* source, const char* debugName) {
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        FFE_LOG_ERROR("Renderer", "Shader compile error (%s): %s", debugName ? debugName : "unknown", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

ShaderHandle createShader(const ShaderDesc& desc) {
    if (desc.vertexSource == nullptr || desc.fragmentSource == nullptr) {
        FFE_LOG_ERROR("Renderer", "createShader called with null source");
        return ShaderHandle{0};
    }

    if (s_headless) {
        return ShaderHandle{s_headlessShaderNext++};
    }

    const u32 slot = findFreeSlot(s_shaders, s_nextShaderSlot);
    if (slot == 0) {
        FFE_LOG_ERROR("Renderer", "Shader pool exhausted (max %u)", MAX_SHADERS);
        return ShaderHandle{0};
    }

    const char* name = desc.debugName ? desc.debugName : "unnamed";

    const GLuint vertId = compileShaderStage(GL_VERTEX_SHADER, desc.vertexSource, name);
    if (vertId == 0) return ShaderHandle{0};

    const GLuint fragId = compileShaderStage(GL_FRAGMENT_SHADER, desc.fragmentSource, name);
    if (fragId == 0) {
        glDeleteShader(vertId);
        return ShaderHandle{0};
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertId);
    glAttachShader(program, fragId);
    glLinkProgram(program);

    GLint linkStatus = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus == GL_FALSE) {
        char infoLog[1024];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        FFE_LOG_ERROR("Renderer", "Shader link error (%s): %s", name, infoLog);
        glDeleteProgram(program);
        glDeleteShader(vertId);
        glDeleteShader(fragId);
        return ShaderHandle{0};
    }

    GlShader& shader = s_shaders[slot];
    shader.programId = program;
    shader.vertexId = vertId;
    shader.fragmentId = fragId;
    shader.alive = true;
    shader.uniformCount = 0;

    FFE_LOG_INFO("Renderer", "Shader '%s' compiled (handle=%u)", name, slot);
    return ShaderHandle{slot};
}

void bindShader(const ShaderHandle handle) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;

    glUseProgram(s_shaders[handle.id].programId);
    s_currentShader = handle;

    // Re-upload cached view-projection matrix to the new program
    GlShader& shader = s_shaders[handle.id];
    const GLint vpLoc = getCachedUniformLocation(shader, "u_viewProjection");
    if (vpLoc >= 0) {
        glUniformMatrix4fv(vpLoc, 1, GL_FALSE, glm::value_ptr(s_viewProjection));
    }
}

void destroyShader(const ShaderHandle handle) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS) return;

    GlShader& shader = s_shaders[handle.id];
    if (!shader.alive) return;

    glDeleteProgram(shader.programId);
    glDeleteShader(shader.vertexId);
    glDeleteShader(shader.fragmentId);
    shader = GlShader{};
}

// --- Uniforms ---

void setUniformInt(const ShaderHandle handle, const char* name, const i32 value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniform1i(loc, value);
}

void setUniformFloat(const ShaderHandle handle, const char* name, const f32 value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniform1f(loc, value);
}

void setUniformVec2(const ShaderHandle handle, const char* name, const glm::vec2& value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniform2fv(loc, 1, glm::value_ptr(value));
}

void setUniformVec3(const ShaderHandle handle, const char* name, const glm::vec3& value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniform3fv(loc, 1, glm::value_ptr(value));
}

void setUniformVec4(const ShaderHandle handle, const char* name, const glm::vec4& value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniform4fv(loc, 1, glm::value_ptr(value));
}

void setUniformMat3(const ShaderHandle handle, const char* name, const glm::mat3& value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void setUniformMat4(const ShaderHandle handle, const char* name, const glm::mat4& value) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void setUniformMat4Array(const ShaderHandle handle, const char* name, const glm::mat4* values, const u32 count) {
    if (s_headless) return;
    if (handle.id == 0 || handle.id >= MAX_SHADERS || !s_shaders[handle.id].alive) return;
    if (values == nullptr || count == 0) return;
    const GLint loc = getCachedUniformLocation(s_shaders[handle.id], name);
    if (loc >= 0) glUniformMatrix4fv(loc, static_cast<GLsizei>(count), GL_FALSE, glm::value_ptr(values[0]));
}

// --- Pipeline state ---

void applyPipelineState(const PipelineState& desired) {
    if (s_headless) return;

    if (desired.blend != s_currentPipeline.blend) {
        switch (desired.blend) {
            case BlendMode::NONE:
                glDisable(GL_BLEND);
                break;
            case BlendMode::ALPHA:
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case BlendMode::ADDITIVE:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
            case BlendMode::PREMULTIPLIED:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
        }
    }

    if (desired.depth != s_currentPipeline.depth) {
        if (desired.depth == DepthFunc::NONE) {
            glDisable(GL_DEPTH_TEST);
        } else {
            glEnable(GL_DEPTH_TEST);
            switch (desired.depth) {
                case DepthFunc::LESS:       glDepthFunc(GL_LESS); break;
                case DepthFunc::LESS_EQUAL: glDepthFunc(GL_LEQUAL); break;
                case DepthFunc::ALWAYS:     glDepthFunc(GL_ALWAYS); break;
                default: break;
            }
        }
    }

    if (desired.depthWrite != s_currentPipeline.depthWrite) {
        glDepthMask(desired.depthWrite ? GL_TRUE : GL_FALSE);
    }

    if (desired.cull != s_currentPipeline.cull) {
        if (desired.cull == CullMode::NONE) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(desired.cull == CullMode::BACK ? GL_BACK : GL_FRONT);
        }
    }

    if (desired.wireframe != s_currentPipeline.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, desired.wireframe ? GL_LINE : GL_FILL);
    }

    s_currentPipeline = desired;
}

// --- Frame ---

void beginFrame(const glm::vec4& clearColor) {
    if (s_headless) return;

    ZoneScopedN("RHI::beginFrame");

    // glClear(GL_DEPTH_BUFFER_BIT) is a no-op when glDepthMask is GL_FALSE.
    // The 2D pipeline restores depthWrite=false after the 3D pass each frame,
    // so without this call the depth buffer would never actually be cleared,
    // leaving stale depth values that clip 3D geometry on subsequent frames.
    // Always restore the depth mask before clearing, and sync the pipeline tracker.
    if (!s_currentPipeline.depthWrite) {
        glDepthMask(GL_TRUE);
        s_currentPipeline.depthWrite = true;
    }

    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void endFrame(GLFWwindow* window) {
    if (s_headless) return;
    if (window == nullptr) return;

    ZoneScopedN("RHI::endFrame");
    glfwSwapBuffers(window);
}

void setViewport(const i32 x, const i32 y, const i32 width, const i32 height) {
    if (s_headless) return;
    glViewport(x, y, width, height);
}

void setScissor(const i32 x, const i32 y, const i32 width, const i32 height) {
    if (s_headless) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

void setViewProjection(const glm::mat4& vp) {
    s_viewProjection = vp;

    if (s_headless) return;

    // If a shader is currently bound, update its uniform immediately
    if (s_currentShader.id != 0 && s_currentShader.id < MAX_SHADERS &&
        s_shaders[s_currentShader.id].alive) {
        GlShader& shader = s_shaders[s_currentShader.id];
        const GLint loc = getCachedUniformLocation(shader, "u_viewProjection");
        if (loc >= 0) {
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(vp));
        }
    }
}

// --- Draw calls ---

// Ensure the sprite VAO exists and is configured for the given VBO.
// The VAO captures vertex attribute state so we avoid redundant glVertexAttribPointer calls.
static void bindSpriteVao(const BufferHandle vertexBuffer) {
    if (s_spriteVao == 0) {
        glGenVertexArrays(1, &s_spriteVao);
    }

    glBindVertexArray(s_spriteVao);

    // Only reconfigure attribs when the VBO changes
    if (s_spriteVaoBoundVbo.id != vertexBuffer.id) {
        glBindBuffer(GL_ARRAY_BUFFER, s_buffers[vertexBuffer.id].glId);

        // position: location 0, 2 floats
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              reinterpret_cast<const void*>(0));

        // texcoord: location 1, 2 floats
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              reinterpret_cast<const void*>(2 * sizeof(f32)));

        // color: location 2, 4 floats
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                              reinterpret_cast<const void*>(4 * sizeof(f32)));

        s_spriteVaoBoundVbo = vertexBuffer;
    }
}

void drawArrays(const BufferHandle vertexBuffer, const u32 vertexCount, const u32 vertexOffset) {
    if (s_headless) return;
    if (vertexBuffer.id == 0 || vertexBuffer.id >= MAX_BUFFERS || !s_buffers[vertexBuffer.id].alive) return;

    bindSpriteVao(vertexBuffer);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(vertexOffset), static_cast<GLsizei>(vertexCount));
}

void drawIndexed(const BufferHandle vertexBuffer, const BufferHandle indexBuffer,
                 const u32 indexCount, const u32 vertexOffset, const u32 indexOffset) {
    if (s_headless) return;
    if (vertexBuffer.id == 0 || vertexBuffer.id >= MAX_BUFFERS || !s_buffers[vertexBuffer.id].alive) return;
    if (indexBuffer.id == 0 || indexBuffer.id >= MAX_BUFFERS || !s_buffers[indexBuffer.id].alive) return;

    (void)vertexOffset; // For GL 3.3, vertex offset is handled by buffer binding

    bindSpriteVao(vertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_buffers[indexBuffer.id].glId);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_SHORT,
                   reinterpret_cast<const void*>(static_cast<uintptr_t>(indexOffset)));
}

// --- Raw GL ID accessor for mesh_loader ---

GLuint getGlBufferId(const BufferHandle handle) {
    if (s_headless) return 0;
    if (handle.id == 0 || handle.id >= MAX_BUFFERS) return 0;
    if (!s_buffers[handle.id].alive) return 0;
    return s_buffers[handle.id].glId;
}

// --- Query functions ---

bool isHeadless() {
    return s_headless;
}

bool isSoftwareRenderer() {
    return s_softwareRenderer;
}

i32 getViewportWidth() {
    return s_viewportWidth;
}

i32 getViewportHeight() {
    return s_viewportHeight;
}

void setViewportSize(const i32 width, const i32 height) {
    s_viewportWidth  = width;
    s_viewportHeight = height;
}

} // namespace ffe::rhi
