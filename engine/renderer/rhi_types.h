#pragma once

#include "core/types.h"
#include <glm/glm.hpp>

namespace ffe::rhi {

// --- Opaque resource handles ---
// Integer IDs, not pointers. Handle value 0 is always invalid (null handle).
struct BufferHandle  { u32 id = 0; };
struct TextureHandle { u32 id = 0; };
struct ShaderHandle  { u32 id = 0; };

// Validity check
inline bool isValid(const BufferHandle h)  { return h.id != 0; }
inline bool isValid(const TextureHandle h) { return h.id != 0; }
inline bool isValid(const ShaderHandle h)  { return h.id != 0; }

// --- Result enum for RHI operations ---
enum class RhiResult : u8 {
    OK = 0,
    ERROR_OUT_OF_MEMORY,
    ERROR_INVALID_HANDLE,
    ERROR_SHADER_COMPILE_FAILED,
    ERROR_SHADER_LINK_FAILED,
    ERROR_TEXTURE_TOO_LARGE,
    ERROR_UNSUPPORTED,
    ERROR_CONTEXT_LOST
};

// --- RHI Configuration ---
struct RhiConfig {
    i32 viewportWidth  = 1280;
    i32 viewportHeight = 720;
    bool headless      = false;
    bool vsync         = true;
    bool debugGL       = false;
};

// --- Buffer types ---
enum class BufferType : u8 {
    VERTEX,
    INDEX,
    UNIFORM
};

enum class BufferUsage : u8 {
    STATIC,    // GL_STATIC_DRAW
    DYNAMIC,   // GL_DYNAMIC_DRAW
    STREAM     // GL_STREAM_DRAW
};

struct BufferDesc {
    BufferType type       = BufferType::VERTEX;
    BufferUsage usage     = BufferUsage::STATIC;
    const void* data      = nullptr;
    u32 sizeBytes         = 0;
};

// --- Texture types ---
enum class TextureFormat : u8 {
    RGBA8,
    RGB8,
    R8,
    RGBA16F,
};

enum class TextureFilter : u8 {
    NEAREST,
    LINEAR,
    NEAREST_MIPMAP_NEAREST,
    LINEAR_MIPMAP_LINEAR,
};

enum class TextureWrap : u8 {
    REPEAT,
    CLAMP_TO_EDGE,
    MIRRORED_REPEAT
};

struct TextureDesc {
    u32 width             = 0;
    u32 height            = 0;
    TextureFormat format  = TextureFormat::RGBA8;
    TextureFilter filter  = TextureFilter::LINEAR;
    TextureWrap wrap      = TextureWrap::CLAMP_TO_EDGE;
    bool generateMipmaps  = false;
    const void* pixelData = nullptr;

    // When true, applies a swizzle mask so that GL_RED maps to alpha and
    // RGB reads as (1,1,1). Used for single-channel font atlases rendered
    // through the standard RGBA sprite shader.
    // Only meaningful when format is R8. Requires OpenGL 3.3 (LEGACY tier).
    bool swizzleRedToAlpha = false;
};

// --- Shader ---
struct ShaderDesc {
    const char* vertexSource   = nullptr;
    const char* fragmentSource = nullptr;
    const char* debugName      = nullptr;
};

// --- Pipeline state ---
enum class BlendMode : u8 {
    NONE,
    ALPHA,
    ADDITIVE,
    PREMULTIPLIED
};

enum class DepthFunc : u8 {
    NONE,
    LESS,
    LESS_EQUAL,
    ALWAYS
};

enum class CullMode : u8 {
    NONE,
    BACK,
    FRONT
};

struct PipelineState {
    BlendMode blend     = BlendMode::NONE;
    DepthFunc depth     = DepthFunc::LESS;
    CullMode cull       = CullMode::BACK;
    bool depthWrite     = true;
    bool wireframe      = false;
};

// --- Vertex layouts ---
// Sprite vertex: position(2f) + texcoord(2f) + color(4f) = 32 bytes
struct SpriteVertex {
    f32 x, y;        // Position (8 bytes)
    f32 u, v;        // Texcoord (8 bytes)
    f32 r, g, b, a;  // Color    (16 bytes)
};
static_assert(sizeof(SpriteVertex) == 32);

// Mesh vertex: position(3f) + normal(3f) + texcoord(2f) = 32 bytes
struct MeshVertex {
    f32 px, py, pz;  // Position (12 bytes)
    f32 nx, ny, nz;  // Normal   (12 bytes)
    f32 u, v;        // Texcoord (8 bytes)
};
static_assert(sizeof(MeshVertex) == 32);

} // namespace ffe::rhi
