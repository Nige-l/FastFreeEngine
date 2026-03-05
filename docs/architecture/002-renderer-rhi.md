# ADR-002: Renderer Hardware Interface (RHI) Abstraction

**Status:** APPROVED
**Author:** architect
**Date:** 2026-03-05
**Tiers:** LEGACY (primary), STANDARD and MODERN (designed for, not implemented here)

This document defines the Renderer Hardware Interface (RHI) for FastFreeEngine. It is the complete specification for the LEGACY-tier OpenGL 3.3 backend. renderer-specialist should be able to implement the entire backend from this document alone, without clarifying questions.

ADR-001 defined the core skeleton, main loop, ECS, and the `Application::render(alpha)` hook. This ADR fills in what happens inside that hook and the systems that feed it.

---

## 1. Design Philosophy

### 1.1 Thin Abstraction, Not an Engine-Within-an-Engine

The RHI is a thin translation layer between FFE's rendering concepts and the underlying graphics API. It translates, it does not invent. There is no "RHI command buffer" that gets "compiled" into GL calls through three layers of indirection. There is a struct that describes what to draw and a function that draws it.

The RHI hides API differences (OpenGL vs Vulkan calling conventions, resource creation patterns, state management quirks) but does NOT hide GPU concepts. Draw calls are real. State changes are real. Buffers and shaders are real things with real VRAM costs. The user should understand these concepts because they directly affect performance on a machine with 1 GB of VRAM and a CPU from 2012.

### 1.2 LEGACY Tier Is First and Default

OpenGL 3.3 core profile is the first backend. It runs on Intel HD 4000 (2012), GeForce GT 630, Radeon HD 7000 series, and anything newer. This covers every laptop and desktop manufactured in the last 13 years. It covers the cheapest hardware a student might own.

OpenGL 3.3 gives us: VAOs, VBOs, FBOs, GLSL 330 core, texture arrays (2D only), instanced rendering (via `glDrawArraysInstanced`), and uniform buffer objects. It does NOT give us: compute shaders, shader storage buffers, bindless textures, or direct state access. Every design decision in this document respects these constraints.

### 1.3 No Virtual Dispatch in the Render Hot Path

The backend is selected at compile time via `FFE_TIER_VALUE`. All RHI functions are concrete implementations compiled directly into the binary. There are no abstract base classes, no vtables, and no `std::function` anywhere in the rendering pipeline.

For the LEGACY tier, the OpenGL 3.3 backend is the only backend compiled. The RHI header (`rhi.h`) defines concrete types and free functions. The OpenGL implementation files provide the bodies. If a future Vulkan backend is added, it provides alternative bodies selected by `#if` / CMake at compile time. One binary, one backend, zero indirection.

```cpp
// This is how backend selection works. No virtual dispatch. No runtime polymorphism.
// rhi.h defines the interface as concrete types and free functions.
// Exactly one backend provides the implementation, selected at build time.

// In engine/renderer/rhi.h:
namespace ffe::rhi {
    RhiResult init(const RhiConfig& config);
    void shutdown();
    BufferHandle createBuffer(const BufferDesc& desc);
    // ...
}

// In engine/renderer/opengl/rhi_opengl.cpp:
// Implements all functions declared in rhi.h using GL calls.

// In engine/renderer/vulkan/rhi_vulkan.cpp (future):
// Implements the same functions using Vulkan calls.
// CMake compiles one or the other, never both.
```

### 1.4 Memory Budget Awareness

A LEGACY machine has 1 GB of VRAM shared with the system on integrated GPUs. The OS, window manager, and other applications consume 200-400 MB. The driver and GL context consume another 50-100 MB. Our realistic budget is **500 MB of VRAM** for all engine resources: textures, buffers, shaders, framebuffers.

Every resource type in this document specifies its expected VRAM footprint. renderer-specialist must track total allocated VRAM and log warnings when usage exceeds 80% of budget (400 MB).

---

## 2. File Layout

```
engine/renderer/
    CMakeLists.txt              # Builds ffe_renderer, selects backend
    rhi.h                       # Core RHI types, handles, free function declarations
    rhi_types.h                 # Enums, config structs, descriptor structs
    render_queue.h              # DrawCommand struct and render queue
    render_queue.cpp            # Sorting and submission
    camera.h                    # Camera struct, view/projection computation
    camera.cpp                  # Camera implementation
    sprite_batch.h              # Sprite batching system
    sprite_batch.cpp            # Sprite batch implementation
    render_system.h             # ECS render system (SystemUpdateFn)
    render_system.cpp           # Collects renderables, builds draw commands
    shader_library.h            # Predefined shader loading
    shader_library.cpp          # Shader compilation and caching
    .context.md                 # AI-native documentation
    opengl/
        rhi_opengl.h            # OpenGL-specific internal helpers (not public)
        rhi_opengl.cpp          # RHI function implementations for GL 3.3
        gl_debug.h              # GL error checking utilities
        gl_debug.cpp            # GL debug callback setup
    vulkan/                     # Future — placeholder only
        README.md               # "Not yet implemented — see ADR for design notes"

shaders/
    legacy/
        sprite.vert             # Sprite vertex shader (GLSL 330 core)
        sprite.frag             # Sprite fragment shader
        solid.vert              # Solid color vertex shader
        solid.frag              # Solid color fragment shader
        textured.vert           # Textured mesh vertex shader
        textured.frag           # Textured mesh fragment shader
```

### 2.1 CMake for the Renderer

```cmake
# engine/renderer/CMakeLists.txt

# GL loader — we use glad, loaded via vcpkg
find_package(glad CONFIG REQUIRED)

# GLFW — system package (libglfw3-dev), found via pkg-config or CMake
find_package(glfw3 3.3 REQUIRED)

# stb — for image loading (already in vcpkg.json)
find_package(Stb REQUIRED)

set(RENDERER_SOURCES
    render_queue.cpp
    camera.cpp
    sprite_batch.cpp
    render_system.cpp
    shader_library.cpp
)

# Backend selection based on tier
if(FFE_TIER STREQUAL "MODERN")
    # Future: Vulkan backend
    message(FATAL_ERROR "Vulkan backend not yet implemented")
else()
    # RETRO, LEGACY, STANDARD all use OpenGL (different versions)
    list(APPEND RENDERER_SOURCES
        opengl/rhi_opengl.cpp
        opengl/gl_debug.cpp
    )
endif()

add_library(ffe_renderer STATIC ${RENDERER_SOURCES})

target_include_directories(ffe_renderer
    PUBLIC ${CMAKE_SOURCE_DIR}/engine
)

target_link_libraries(ffe_renderer
    PUBLIC
        ffe_core
        glm::glm
    PRIVATE
        glad::glad
        glfw
        Tracy::TracyClient
)

# Copy shader files to build output for runtime loading
file(COPY ${CMAKE_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})
```

---

## 3. Window and Context

### 3.1 GLFW Window Creation

GLFW handles window creation and OpenGL context setup. The window is created during `Application::startup()` (step 4 in ADR-001's startup sequence) and destroyed during `Application::shutdown()` (step 4 in reverse).

The renderer does NOT own the GLFW window. `Application` owns the window handle and passes it to the renderer during initialization. This keeps the ownership hierarchy clean and ensures the window outlives the renderer during shutdown.

```cpp
// Inside Application — NOT inside the renderer
// Application::startup() creates the window:

GLFWwindow* createWindow(const ApplicationConfig& config) {
    glfwInit();

    // OpenGL 3.3 core profile for LEGACY tier
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    // No MSAA on LEGACY — it is expensive on integrated GPUs.
    // STANDARD tier can enable 4x MSAA.
    glfwWindowHint(GLFW_SAMPLES, 0);

    // Disable window resize for now — avoids framebuffer resize complexity.
    // Revisit in a later ADR.
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        config.windowWidth,
        config.windowHeight,
        config.windowTitle,
        nullptr,   // No fullscreen
        nullptr    // No shared context
    );

    if (!window) {
        return nullptr;
    }

    glfwMakeContextCurrent(window);

    // VSync ON by default — prevents tearing and limits GPU power draw on laptops.
    // Can be toggled via config for benchmarking.
    glfwSwapInterval(1);

    return window;
}
```

### 3.2 GL Loading via glad

After GLFW creates the context, glad loads the function pointers:

```cpp
// Called immediately after glfwMakeContextCurrent()
if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    FFE_LOG_FATAL("Renderer", "Failed to load OpenGL function pointers via glad");
    return Result::fail("glad init failed");
}

// Verify we got at least GL 3.3
if (GLVersion.major < 3 || (GLVersion.major == 3 && GLVersion.minor < 3)) {
    FFE_LOG_FATAL("Renderer", "OpenGL 3.3 required, got {}.{}", GLVersion.major, GLVersion.minor);
    return Result::fail("OpenGL version too low");
}

FFE_LOG_INFO("Renderer", "OpenGL {}.{} loaded — {}", GLVersion.major, GLVersion.minor,
    reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
```

### 3.3 Headless Mode

For CI (GitHub Actions, xvfb) and unit tests, `ApplicationConfig::headless = true` skips window creation entirely. The renderer initializes in a "null" mode where:

- All `rhi::create*` functions return valid handles but perform no GPU operations.
- All `rhi::draw*` functions are no-ops.
- The render queue still sorts and processes commands (for testing the sorting logic) but submission is skipped.

This is implemented as an early-return check at the top of each GL-calling function:

```cpp
// In rhi_opengl.cpp:
static bool s_headless = false;

RhiResult init(const RhiConfig& config) {
    s_headless = config.headless;
    if (s_headless) {
        FFE_LOG_INFO("Renderer", "Headless mode — no GPU operations");
        return RhiResult::OK;
    }
    // ... actual GL init ...
}

BufferHandle createBuffer(const BufferDesc& desc) {
    if (s_headless) {
        // Return a monotonically increasing fake handle
        static u32 s_nextHandle = 1;
        return BufferHandle{s_nextHandle++};
    }
    // ... actual GL buffer creation ...
}
```

### 3.4 Integration with Application Main Loop

The main loop from ADR-001 calls `render(alpha)` each frame. Here is how the renderer integrates:

```cpp
void Application::render(const float alpha) {
    ZoneScopedN("Render");

    if (m_config.headless) return;

    // 1. The render system (a SystemUpdateFn at priority 500+) has already run
    //    during tick() and populated the render queue with DrawCommands.

    // 2. Sort the render queue (shader -> texture -> depth)
    ffe::renderer::sortRenderQueue(m_renderQueue);

    // 3. Begin frame — clear, set viewport
    ffe::rhi::beginFrame(m_clearColor);

    // 4. Set camera matrices
    ffe::rhi::setViewProjection(m_camera.viewMatrix(), m_camera.projectionMatrix());

    // 5. Submit all draw commands
    ffe::renderer::submitRenderQueue(m_renderQueue);

    // 6. End frame — swap buffers
    ffe::rhi::endFrame(m_window);

    // 7. Reset the render queue for next frame
    m_renderQueue.clear();
}
```

---

## 4. Core RHI Types

All RHI types live in the `ffe::rhi` namespace. They are concrete structs, not classes with virtual functions. GPU resources are identified by opaque integer handles, not pointers.

### 4.1 Resource Handles

```cpp
// engine/renderer/rhi_types.h
#pragma once

#include "core/types.h"
#include <glm/glm.hpp>

namespace ffe::rhi {

// --- Opaque resource handles ---
// These are integer IDs, not pointers. The backend maintains internal arrays
// that map handle -> actual GPU resource (GL name / Vulkan handle).
// Handle value 0 is always invalid (null handle).

struct BufferHandle  { u32 id = 0; };
struct TextureHandle { u32 id = 0; };
struct ShaderHandle  { u32 id = 0; };

// Validity check
inline bool isValid(BufferHandle h)  { return h.id != 0; }
inline bool isValid(TextureHandle h) { return h.id != 0; }
inline bool isValid(ShaderHandle h)  { return h.id != 0; }

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
```

### 4.2 RHI Configuration

```cpp
struct RhiConfig {
    i32 viewportWidth  = 1280;
    i32 viewportHeight = 720;
    bool headless      = false;
    bool vsync         = true;
    bool debugGL       = false;  // Enable GL debug output in debug builds
};
```

### 4.3 Buffer

Buffers hold vertex data, index data, or uniform data on the GPU. On LEGACY hardware, every buffer creation is a `glGenBuffers` + `glBufferData` call. There is no persistent mapping, no buffer orphaning tricks — those require GL 4.4+.

```cpp
// --- Buffer types ---
enum class BufferType : u8 {
    VERTEX,
    INDEX,
    UNIFORM
};

enum class BufferUsage : u8 {
    STATIC,   // Upload once, draw many times (GL_STATIC_DRAW)
    DYNAMIC,  // Upload frequently (GL_DYNAMIC_DRAW) — for sprite batching
    STREAM    // Upload every frame (GL_STREAM_DRAW) — for per-frame data
};

struct BufferDesc {
    BufferType type       = BufferType::VERTEX;
    BufferUsage usage     = BufferUsage::STATIC;
    const void* data      = nullptr;   // Initial data (can be nullptr for DYNAMIC/STREAM)
    u32 sizeBytes         = 0;         // Total buffer size in bytes
};
```

**Backend internal storage (OpenGL):**

```cpp
// Inside opengl/rhi_opengl.cpp — NOT exposed in the public header

struct GlBuffer {
    GLuint glId       = 0;       // OpenGL buffer name
    BufferType type   = BufferType::VERTEX;
    BufferUsage usage = BufferUsage::STATIC;
    u32 sizeBytes     = 0;
    bool alive        = false;   // Slot in use?
};

// Fixed-size pool. No heap allocation for buffer metadata.
// 4096 buffers * 16 bytes = 64 KB — fits in L1 cache.
static constexpr u32 MAX_BUFFERS = 4096;
static GlBuffer s_buffers[MAX_BUFFERS];
static u32 s_nextBufferSlot = 1; // Slot 0 is reserved (null handle)
```

**RHI functions for buffers:**

```cpp
namespace ffe::rhi {

// Create a GPU buffer. Returns a handle. The handle is valid until destroyBuffer() is called.
// desc.data may be nullptr for DYNAMIC/STREAM buffers — call updateBuffer() to upload data later.
// VRAM cost: desc.sizeBytes (1:1 — no hidden overhead on GL 3.3).
BufferHandle createBuffer(const BufferDesc& desc);

// Upload new data to a DYNAMIC or STREAM buffer.
// offset + sizeBytes must not exceed the buffer's original size.
// On LEGACY, this calls glBufferSubData(). It is a synchronous GPU stall if the
// buffer is in use by a pending draw call. Avoid calling this mid-frame for STATIC buffers.
RhiResult updateBuffer(BufferHandle handle, const void* data, u32 sizeBytes, u32 offset = 0);

// Destroy a buffer and free its VRAM.
// The handle is invalid after this call. Using it is undefined behavior.
void destroyBuffer(BufferHandle handle);

} // namespace ffe::rhi
```

**VRAM budget note:** A typical 2D game with sprite batching needs:
- 1 dynamic vertex buffer for sprite batch: 64 KB (enough for ~1000 sprites per batch)
- 1 static index buffer for sprite quads: 24 KB (enough for 1000 quads, indices are reusable)
- A few static VBOs for UI and debug geometry: 16 KB total
- Total buffer VRAM: ~104 KB. Negligible.

A 3D game with moderate meshes might use 10-50 MB of vertex/index buffers. Still well within the 500 MB budget.

### 4.4 Texture2D

```cpp
// --- Texture types ---
enum class TextureFormat : u8 {
    RGBA8,       // 4 bytes per pixel, standard
    RGB8,        // 3 bytes per pixel, no alpha
    R8,          // 1 byte per pixel, grayscale / SDF fonts
    RGBA16F,     // HDR — STANDARD tier and above only
};

enum class TextureFilter : u8 {
    NEAREST,     // Pixel art, no interpolation
    LINEAR,      // Smooth filtering
    NEAREST_MIPMAP_NEAREST,
    LINEAR_MIPMAP_LINEAR,   // Trilinear — best quality, costs 33% more VRAM for mipmaps
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
    const void* pixelData = nullptr;   // Row-major, top-left origin
};
```

**Backend internal storage (OpenGL):**

```cpp
struct GlTexture {
    GLuint glId        = 0;
    u32 width          = 0;
    u32 height         = 0;
    TextureFormat fmt  = TextureFormat::RGBA8;
    u32 vramBytes      = 0;   // Tracked for budget warnings
    bool alive         = false;
};

static constexpr u32 MAX_TEXTURES = 2048;
static GlTexture s_textures[MAX_TEXTURES];
static u32 s_nextTextureSlot = 1;
static u32 s_totalTextureVram = 0;  // Running total for budget tracking

static constexpr u32 VRAM_BUDGET_BYTES = 500 * 1024 * 1024; // 500 MB
static constexpr u32 VRAM_WARNING_THRESHOLD = static_cast<u32>(VRAM_BUDGET_BYTES * 0.8f);
```

**RHI functions for textures:**

```cpp
namespace ffe::rhi {

// Create a 2D texture from pixel data.
// pixelData must be width * height * bytesPerPixel(format) bytes.
// If generateMipmaps is true, mipmaps are generated immediately via glGenerateMipmap().
// VRAM cost: width * height * bpp, plus 33% if mipmaps are enabled.
// Maximum texture size on LEGACY: 4096x4096 (GL_MAX_TEXTURE_SIZE is at least 4096 for GL 3.3).
TextureHandle createTexture(const TextureDesc& desc);

// Bind a texture to a texture unit for sampling in shaders.
// unitIndex is 0-15 on LEGACY (GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS is at least 16 for GL 3.3).
// The shader's sampler uniform must be set to the same unit index.
void bindTexture(TextureHandle handle, u32 unitIndex);

// Destroy a texture and free its VRAM.
void destroyTexture(TextureHandle handle);

// Query current VRAM usage for textures (bytes).
u32 textureVramUsed();

} // namespace ffe::rhi
```

**VRAM budget example:** A 1024x1024 RGBA8 texture is 4 MB. With mipmaps, 5.33 MB. A sprite-based 2D game might have 20 sprite sheets at 1024x1024 = 80 MB. A 3D game might have 50 textures at 512x512 = 50 MB. Both fit comfortably in the 500 MB budget. A game that loads 100 uncompressed 2048x2048 textures (1.6 GB) will NOT fit. This is the developer's responsibility — we warn, we do not crash.

### 4.5 Shader

```cpp
struct ShaderDesc {
    const char* vertexSource   = nullptr;  // GLSL source string
    const char* fragmentSource = nullptr;  // GLSL source string
    const char* debugName      = nullptr;  // For logging / Tracy — not sent to GPU
};
```

**Backend internal storage (OpenGL):**

```cpp
struct GlShader {
    GLuint programId     = 0;    // Linked program
    GLuint vertexId      = 0;    // Vertex shader object (kept for error reporting)
    GLuint fragmentId    = 0;    // Fragment shader object
    bool alive           = false;
    // Uniform location cache — avoids glGetUniformLocation() per frame.
    // We cache up to 16 uniforms per shader. If a shader needs more, revisit.
    static constexpr u32 MAX_CACHED_UNIFORMS = 16;
    struct UniformEntry {
        u32 nameHash = 0;       // FNV-1a hash of the uniform name
        GLint location = -1;
    };
    UniformEntry uniformCache[MAX_CACHED_UNIFORMS];
    u32 uniformCount = 0;
};

static constexpr u32 MAX_SHADERS = 256;
static GlShader s_shaders[MAX_SHADERS];
static u32 s_nextShaderSlot = 1;
```

**RHI functions for shaders:**

```cpp
namespace ffe::rhi {

// Compile and link a shader program from GLSL source strings.
// Returns a valid handle on success, or a null handle on failure.
// Compilation errors are logged via FFE_LOG_ERROR with the full GLSL info log.
// VRAM cost: minimal (a few KB per shader program).
ShaderHandle createShader(const ShaderDesc& desc);

// Bind a shader program for subsequent draw calls.
void bindShader(ShaderHandle handle);

// Set uniforms on the currently bound shader.
// These use cached uniform locations internally — the first call per uniform name
// does a glGetUniformLocation() and caches the result. Subsequent calls are a hash lookup.
void setUniformInt(ShaderHandle handle, const char* name, i32 value);
void setUniformFloat(ShaderHandle handle, const char* name, f32 value);
void setUniformVec2(ShaderHandle handle, const char* name, const glm::vec2& value);
void setUniformVec3(ShaderHandle handle, const char* name, const glm::vec3& value);
void setUniformVec4(ShaderHandle handle, const char* name, const glm::vec4& value);
void setUniformMat3(ShaderHandle handle, const char* name, const glm::mat3& value);
void setUniformMat4(ShaderHandle handle, const char* name, const glm::mat4& value);

// Destroy a shader program.
void destroyShader(ShaderHandle handle);

} // namespace ffe::rhi
```

**Uniform location caching implementation:**

```cpp
// Inside rhi_opengl.cpp
static GLint getCachedUniformLocation(GlShader& shader, const char* name) {
    // FNV-1a hash of the name
    u32 hash = 2166136261u;
    for (const char* p = name; *p; ++p) {
        hash ^= static_cast<u32>(*p);
        hash *= 16777619u;
    }

    // Search cache
    for (u32 i = 0; i < shader.uniformCount; ++i) {
        if (shader.uniformCache[i].nameHash == hash) {
            return shader.uniformCache[i].location;
        }
    }

    // Cache miss — query GL and store
    const GLint loc = glGetUniformLocation(shader.programId, name);
    if (shader.uniformCount < GlShader::MAX_CACHED_UNIFORMS) {
        shader.uniformCache[shader.uniformCount] = {hash, loc};
        shader.uniformCount++;
    } else {
        FFE_LOG_WARN("Renderer", "Uniform cache full for shader — consider increasing MAX_CACHED_UNIFORMS");
    }

    return loc;
}
```

### 4.6 Pipeline State

OpenGL 3.3 does not have pipeline state objects (that is a Vulkan / GL 4.5+ concept). We emulate them with a struct that we compare-and-set before each draw call. This avoids redundant GL state changes.

```cpp
// --- Pipeline state ---
enum class BlendMode : u8 {
    NONE,             // No blending — opaque geometry
    ALPHA,            // Standard alpha blending: srcAlpha, oneMinusSrcAlpha
    ADDITIVE,         // Additive: one, one
    PREMULTIPLIED     // Premultiplied alpha: one, oneMinusSrcAlpha
};

enum class DepthFunc : u8 {
    NONE,             // Depth test disabled
    LESS,             // Standard 3D depth test
    LESS_EQUAL,       // For skybox / last-drawn objects
    ALWAYS            // Always pass — for 2D overlays
};

enum class CullMode : u8 {
    NONE,             // No culling — double-sided (sprites)
    BACK,             // Cull back faces — standard 3D
    FRONT             // Cull front faces — shadow maps
};

struct PipelineState {
    BlendMode blend     = BlendMode::NONE;
    DepthFunc depth     = DepthFunc::LESS;
    CullMode cull       = CullMode::BACK;
    bool depthWrite     = true;
    bool wireframe      = false;   // For debug rendering
};
// sizeof(PipelineState) = 5 bytes. Fits in a register.
```

**Applying pipeline state (OpenGL):**

```cpp
// Inside rhi_opengl.cpp
static PipelineState s_currentPipeline; // Tracks current GL state to avoid redundant calls

void applyPipelineState(const PipelineState& desired) {
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
```

### 4.7 Frame Functions

```cpp
namespace ffe::rhi {

// Initialize the RHI. Must be called after the GL context is current.
RhiResult init(const RhiConfig& config);

// Shutdown the RHI. Destroys all remaining GPU resources.
// Must be called before the GL context is destroyed.
void shutdown();

// Begin a frame. Clears the color and depth buffers.
void beginFrame(const glm::vec4& clearColor);

// End a frame. Calls glfwSwapBuffers().
// The GLFWwindow* is passed because Application owns it, not the renderer.
void endFrame(GLFWwindow* window);

// Set the viewport dimensions. Called on init and if the window is resized.
void setViewport(i32 x, i32 y, i32 width, i32 height);

// Set the scissor rectangle. Pass (0, 0, viewportWidth, viewportHeight) to disable.
void setScissor(i32 x, i32 y, i32 width, i32 height);

} // namespace ffe::rhi
```

---

## 5. Draw Call Submission

### 5.1 The DrawCommand Struct

Every visible thing in a frame becomes a `DrawCommand`. The render system (Section 9) generates these. The renderer sorts them and submits them to the GPU.

The struct must be small for cache-friendly sorting. Target: 64 bytes or less (one cache line).

```cpp
// engine/renderer/render_queue.h
#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

struct DrawCommand {
    // --- Sort key (8 bytes) ---
    // Packed into a single u64 for fast radix/comparison sort.
    // Bit layout (MSB to LSB):
    //   [63..60]  4 bits: layer (0-15, for render order: background, world, UI, debug)
    //   [59..52]  8 bits: shader ID (0-255, sorts by shader to minimize program switches)
    //   [51..40] 12 bits: texture ID (0-4095, sorts by texture to minimize bind calls)
    //   [39..16] 24 bits: depth (quantized to 24-bit unsigned int, front-to-back for opaques)
    //   [15..0]  16 bits: sub-order (for stable sort tiebreaking)
    u64 sortKey = 0;

    // --- GPU resource references (8 bytes) ---
    ShaderHandle shader;         // 4 bytes
    TextureHandle texture;       // 4 bytes

    // --- Geometry (12 bytes) ---
    BufferHandle vertexBuffer;   // 4 bytes
    u32 vertexOffset = 0;        // Byte offset into the vertex buffer
    u32 vertexCount  = 0;        // Number of vertices (or indices if indexBuffer is valid)

    // --- Index buffer (8 bytes) ---
    BufferHandle indexBuffer;    // 4 bytes (0 = no index buffer, use glDrawArrays)
    u32 indexOffset = 0;         // Byte offset into the index buffer

    // --- Transform (16 bytes) ---
    // For 2D sprites, this is a 2D transform packed into 16 bytes:
    //   position.xy (8 bytes) + scale.xy (4 bytes) + rotation angle (4 bytes)
    // For 3D meshes, this is a handle/index into a transform buffer.
    // We use a union to keep the struct at 64 bytes.
    f32 posX = 0.0f;
    f32 posY = 0.0f;
    f32 scaleX = 1.0f;
    f32 scaleY = 1.0f;

    // --- Pipeline state (4 bytes) ---
    // Packed: blend(2 bits) | depth(2 bits) | cull(2 bits) | depthWrite(1 bit) | wireframe(1 bit)
    u8 pipelineBits = 0;

    // --- Padding to 64 bytes (4 bytes) ---
    u8 reserved[3] = {};
};

static_assert(sizeof(DrawCommand) == 64, "DrawCommand must be exactly one cache line");
```

**Sort key encoding helpers:**

```cpp
inline u64 makeSortKey(u8 layer, u8 shaderId, u16 textureId, f32 depth, u16 subOrder = 0) {
    // Quantize depth [0.0, 1.0] to 24-bit unsigned integer.
    // For opaque geometry, we sort front-to-back (smaller depth = drawn first)
    // to maximize early-z rejection.
    const u32 depthBits = static_cast<u32>(glm::clamp(depth, 0.0f, 1.0f) * 16777215.0f);

    return (static_cast<u64>(layer & 0xF) << 60)
         | (static_cast<u64>(shaderId)    << 52)
         | (static_cast<u64>(textureId & 0xFFF) << 40)
         | (static_cast<u64>(depthBits & 0xFFFFFF) << 16)
         | static_cast<u64>(subOrder);
}

inline PipelineState unpackPipelineBits(u8 bits) {
    PipelineState ps;
    ps.blend      = static_cast<BlendMode>((bits >> 6) & 0x3);
    ps.depth      = static_cast<DepthFunc>((bits >> 4) & 0x3);
    ps.cull       = static_cast<CullMode>((bits >> 2) & 0x3);
    ps.depthWrite = (bits >> 1) & 0x1;
    ps.wireframe  = bits & 0x1;
    return ps;
}

inline u8 packPipelineBits(const PipelineState& ps) {
    return (static_cast<u8>(ps.blend) << 6)
         | (static_cast<u8>(ps.depth) << 4)
         | (static_cast<u8>(ps.cull)  << 2)
         | (static_cast<u8>(ps.depthWrite) << 1)
         | static_cast<u8>(ps.wireframe);
}
```

### 5.2 The Render Queue

The render queue is a flat array of `DrawCommand` structs. It is allocated from the frame arena each frame (zero allocation cost, perfect cache locality for sorting).

```cpp
// engine/renderer/render_queue.h (continued)

struct RenderQueue {
    DrawCommand* commands = nullptr;
    u32 count    = 0;
    u32 capacity = 0;
};

// Maximum draw commands per frame on LEGACY tier.
// 8192 commands * 64 bytes = 512 KB — fits in L2 cache on most 2012 CPUs.
inline constexpr u32 MAX_DRAW_COMMANDS_LEGACY   = 8192;
inline constexpr u32 MAX_DRAW_COMMANDS_STANDARD  = 32768;
inline constexpr u32 MAX_DRAW_COMMANDS_MODERN    = 131072;
```

**Allocation and usage pattern:**

```cpp
// In Application::startup() or beginning of each frame:
RenderQueue queue;
queue.capacity = MAX_DRAW_COMMANDS_LEGACY; // Based on tier
queue.commands = m_frameAllocator.allocateArray<DrawCommand>(queue.capacity);
queue.count = 0;

// Game systems push draw commands:
inline void pushDrawCommand(RenderQueue& queue, const DrawCommand& cmd) {
    if (queue.count < queue.capacity) {
        queue.commands[queue.count++] = cmd;
    } else {
        FFE_LOG_WARN("Renderer", "Render queue full — dropping draw command");
    }
}
```

### 5.3 Sorting

Sort the render queue by `sortKey` using `std::sort`. The sort key encodes layer, shader, texture, and depth so a single integer comparison produces the correct draw order.

```cpp
// engine/renderer/render_queue.cpp

void sortRenderQueue(RenderQueue& queue) {
    ZoneScopedN("SortRenderQueue");
    std::sort(queue.commands, queue.commands + queue.count,
        [](const DrawCommand& a, const DrawCommand& b) {
            return a.sortKey < b.sortKey;
        });
}
```

**Why `std::sort` and not radix sort:** For up to 8192 elements (LEGACY), `std::sort` (introsort) is faster than a radix sort due to lower constant overhead and better cache behavior on small arrays. If MODERN tier needs 100K+ draw commands, radix sort on the 8-byte key becomes worthwhile. That is a future optimization.

### 5.4 Submission

```cpp
// engine/renderer/render_queue.cpp

void submitRenderQueue(const RenderQueue& queue) {
    ZoneScopedN("SubmitRenderQueue");

    ShaderHandle currentShader   = {};
    TextureHandle currentTexture = {};
    u8 currentPipeline           = 0xFF; // Force first apply

    for (u32 i = 0; i < queue.count; ++i) {
        const DrawCommand& cmd = queue.commands[i];

        // Change pipeline state only if different
        if (cmd.pipelineBits != currentPipeline) {
            rhi::applyPipelineState(unpackPipelineBits(cmd.pipelineBits));
            currentPipeline = cmd.pipelineBits;
        }

        // Change shader only if different (expensive — glUseProgram)
        if (cmd.shader.id != currentShader.id) {
            rhi::bindShader(cmd.shader);
            currentShader = cmd.shader;
            // Re-upload view/projection uniforms — they are per-program state in GL
            // (This is done internally by bindShader if the matrices are cached)
        }

        // Change texture only if different (moderate cost — glBindTexture)
        if (cmd.texture.id != currentTexture.id) {
            rhi::bindTexture(cmd.texture, 0); // Always unit 0 for main texture
            currentTexture = cmd.texture;
        }

        // Issue draw call
        if (isValid(cmd.indexBuffer)) {
            rhi::drawIndexed(cmd.vertexBuffer, cmd.indexBuffer,
                             cmd.vertexCount, cmd.vertexOffset, cmd.indexOffset);
        } else {
            rhi::drawArrays(cmd.vertexBuffer, cmd.vertexCount, cmd.vertexOffset);
        }
    }
}
```

**RHI draw functions:**

```cpp
namespace ffe::rhi {

// Draw using a vertex buffer directly (glDrawArrays).
void drawArrays(BufferHandle vertexBuffer, u32 vertexCount, u32 vertexOffset);

// Draw using an index buffer (glDrawElements).
void drawIndexed(BufferHandle vertexBuffer, BufferHandle indexBuffer,
                 u32 indexCount, u32 vertexOffset, u32 indexOffset);

} // namespace ffe::rhi
```

**VAO management:** OpenGL 3.3 requires a VAO to be bound for all draw calls. We use a single global VAO configured during `rhi::init()`. Vertex attribute pointers are set per-draw-call based on the shader's expected layout. For the LEGACY tier with only 3 shader programs (solid, textured, sprite), this means at most 3 different vertex layouts. We define them statically.

```cpp
// Vertex layouts — defined in rhi_opengl.cpp

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
    f32 u, v;         // Texcoord (8 bytes)
};
static_assert(sizeof(MeshVertex) == 32);

// Both vertex types are 32 bytes — half a cache line. Two vertices per cache line.
// This is intentional. Larger vertices waste bandwidth; smaller vertices require
// more draw calls for the same geometry.
```

---

## 6. Shader Management

### 6.1 GLSL 330 Core for LEGACY Tier

All shaders for the LEGACY tier use `#version 330 core`. No extensions. No compatibility profile. This ensures they run on every GL 3.3 implementation without driver-specific behavior.

### 6.2 Shader File Loading

Shaders are loaded from the `shaders/legacy/` directory at startup. They are compiled and linked into shader programs, then cached by the shader library.

```cpp
// engine/renderer/shader_library.h
#pragma once

#include "renderer/rhi_types.h"

namespace ffe::renderer {

// Pre-defined shader IDs — these match the sort key encoding.
// Adding a new shader means adding an enum value and loading it in init().
enum class BuiltinShader : u8 {
    SOLID     = 0,   // Flat color, no texture
    TEXTURED  = 1,   // Single texture with vertex color modulation
    SPRITE    = 2,   // Sprite batching shader — 2D, texture + vertex color
    COUNT
};

struct ShaderLibrary {
    rhi::ShaderHandle handles[static_cast<u32>(BuiltinShader::COUNT)];
};

// Load all built-in shaders from shaders/legacy/. Returns false if any fail.
bool initShaderLibrary(ShaderLibrary& library, const char* shaderDir);

// Destroy all built-in shaders.
void shutdownShaderLibrary(ShaderLibrary& library);

// Get a shader handle by enum. No bounds checking — caller must use valid enum.
inline rhi::ShaderHandle getShader(const ShaderLibrary& library, BuiltinShader id) {
    return library.handles[static_cast<u32>(id)];
}

} // namespace ffe::renderer
```

**Loading implementation:**

```cpp
// engine/renderer/shader_library.cpp

#include "renderer/shader_library.h"
#include "renderer/rhi.h"
#include "core/logging.h"

#include <cstdio>

namespace ffe::renderer {

// Read an entire file into a static buffer. No heap allocation.
// Maximum shader source size: 16 KB. If your shader is larger, simplify it.
static constexpr u32 MAX_SHADER_SOURCE = 16 * 1024;

static bool readFile(const char* path, char* buffer, u32 bufferSize) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        FFE_LOG_ERROR("Shader", "Cannot open file: %s", path);
        return false;
    }
    const size_t bytesRead = fread(buffer, 1, bufferSize - 1, f);
    buffer[bytesRead] = '\0';
    fclose(f);
    return bytesRead > 0;
}

bool initShaderLibrary(ShaderLibrary& library, const char* shaderDir) {
    struct ShaderPair {
        const char* vertFile;
        const char* fragFile;
    };

    static const ShaderPair PAIRS[] = {
        {"solid.vert",    "solid.frag"},
        {"textured.vert", "textured.frag"},
        {"sprite.vert",   "sprite.frag"},
    };
    static_assert(sizeof(PAIRS) / sizeof(PAIRS[0]) == static_cast<u32>(BuiltinShader::COUNT));

    char vertPath[256];
    char fragPath[256];
    char vertSource[MAX_SHADER_SOURCE];
    char fragSource[MAX_SHADER_SOURCE];

    for (u32 i = 0; i < static_cast<u32>(BuiltinShader::COUNT); ++i) {
        snprintf(vertPath, sizeof(vertPath), "%s/%s", shaderDir, PAIRS[i].vertFile);
        snprintf(fragPath, sizeof(fragPath), "%s/%s", shaderDir, PAIRS[i].fragFile);

        if (!readFile(vertPath, vertSource, MAX_SHADER_SOURCE) ||
            !readFile(fragPath, fragSource, MAX_SHADER_SOURCE)) {
            return false;
        }

        rhi::ShaderDesc desc;
        desc.vertexSource   = vertSource;
        desc.fragmentSource = fragSource;
        desc.debugName      = PAIRS[i].vertFile;

        library.handles[i] = rhi::createShader(desc);
        if (!rhi::isValid(library.handles[i])) {
            FFE_LOG_ERROR("Shader", "Failed to compile shader: %s", PAIRS[i].vertFile);
            return false;
        }
    }

    FFE_LOG_INFO("Shader", "Loaded %u built-in shaders", static_cast<u32>(BuiltinShader::COUNT));
    return true;
}

void shutdownShaderLibrary(ShaderLibrary& library) {
    for (u32 i = 0; i < static_cast<u32>(BuiltinShader::COUNT); ++i) {
        if (rhi::isValid(library.handles[i])) {
            rhi::destroyShader(library.handles[i]);
            library.handles[i] = {};
        }
    }
}

} // namespace ffe::renderer
```

### 6.3 Uniform Binding Strategy

For the LEGACY tier, we use individual uniforms (`glUniform*`), NOT uniform buffer objects (UBOs). Rationale: the LEGACY tier has at most 3 shader programs with at most 5-10 uniforms each. UBOs add complexity (buffer management, std140 layout rules, binding points) for zero performance benefit at this scale.

Per-frame uniforms set on every shader bind:
- `u_viewProjection` (mat4) — camera view-projection matrix
- `u_time` (float) — elapsed time for animations

Per-draw uniforms set per draw command:
- `u_model` (mat4) — model/world transform (for 3D)
- `u_spriteTransform` (vec4) — position.xy, scale.xy (for 2D sprites)
- `u_color` (vec4) — tint/modulation color

Per-material uniforms set when texture/material changes:
- `u_texture0` (sampler2D) — main texture (always unit 0)

**Future tier note:** STANDARD tier should switch to UBOs for the per-frame and per-draw data. MODERN tier should use push constants (Vulkan). The shader uniform API in Section 4.5 is the right abstraction boundary — changing the backing storage from individual uniforms to UBOs does not change the calling code.

### 6.4 Built-in Shader Source

**shaders/legacy/sprite.vert:**

```glsl
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_color;

uniform mat4 u_viewProjection;

out vec2 v_texcoord;
out vec4 v_color;

void main() {
    v_texcoord = a_texcoord;
    v_color = a_color;
    gl_Position = u_viewProjection * vec4(a_position, 0.0, 1.0);
}
```

**shaders/legacy/sprite.frag:**

```glsl
#version 330 core

in vec2 v_texcoord;
in vec4 v_color;

uniform sampler2D u_texture0;

out vec4 fragColor;

void main() {
    vec4 texel = texture(u_texture0, v_texcoord);
    fragColor = texel * v_color;
    if (fragColor.a < 0.01) discard; // Alpha cutoff — avoid writing transparent fragments to depth
}
```

**shaders/legacy/solid.vert:**

```glsl
#version 330 core

layout(location = 0) in vec3 a_position;

uniform mat4 u_viewProjection;
uniform mat4 u_model;

void main() {
    gl_Position = u_viewProjection * u_model * vec4(a_position, 1.0);
}
```

**shaders/legacy/solid.frag:**

```glsl
#version 330 core

uniform vec4 u_color;

out vec4 fragColor;

void main() {
    fragColor = u_color;
}
```

**shaders/legacy/textured.vert:**

```glsl
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_viewProjection;
uniform mat4 u_model;

out vec2 v_texcoord;
out vec3 v_normal;

void main() {
    v_texcoord = a_texcoord;
    v_normal = mat3(u_model) * a_normal;
    gl_Position = u_viewProjection * u_model * vec4(a_position, 1.0);
}
```

**shaders/legacy/textured.frag:**

```glsl
#version 330 core

in vec2 v_texcoord;
in vec3 v_normal;

uniform sampler2D u_texture0;
uniform vec4 u_color;

out vec4 fragColor;

void main() {
    vec4 texel = texture(u_texture0, v_texcoord);
    fragColor = texel * u_color;
}
```

### 6.5 Future Tier Shader Path

- **STANDARD tier (OpenGL 4.5):** Same GLSL source files but with `#version 450 core`. Add compute shaders for particle systems and frustum culling. Keep the same uniform API but back it with UBOs.
- **MODERN tier (Vulkan):** Shaders pre-compiled to SPIR-V using `glslangValidator` as a build step. Stored in `shaders/modern/` as `.spv` files. The RHI `createShader()` function accepts SPIR-V bytecode instead of GLSL source. This is a different code path inside the Vulkan backend, but the public API remains the same: pass shader data, get a handle back.

---

## 7. Camera and Projection

### 7.1 Camera Struct

```cpp
// engine/renderer/camera.h
#pragma once

#include "core/types.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ffe::renderer {

enum class ProjectionType : u8 {
    PERSPECTIVE,
    ORTHOGRAPHIC
};

struct Camera {
    // Position and orientation
    glm::vec3 position = {0.0f, 0.0f, 5.0f};
    glm::vec3 target   = {0.0f, 0.0f, 0.0f};
    glm::vec3 up       = {0.0f, 1.0f, 0.0f};

    // Projection parameters
    ProjectionType projType = ProjectionType::ORTHOGRAPHIC;

    // Perspective params (used when projType == PERSPECTIVE)
    f32 fovDegrees = 60.0f;
    f32 nearPlane  = 0.1f;
    f32 farPlane   = 100.0f;

    // Orthographic params (used when projType == ORTHOGRAPHIC)
    // Default: viewport-sized in pixels, origin at center.
    // This means a sprite at position (100, 200) is 100 pixels right and 200 pixels up from center.
    f32 orthoLeft   = -640.0f;
    f32 orthoRight  =  640.0f;
    f32 orthoBottom = -360.0f;
    f32 orthoTop    =  360.0f;

    // Viewport dimensions (set from ApplicationConfig)
    f32 viewportWidth  = 1280.0f;
    f32 viewportHeight = 720.0f;
};

// sizeof(Camera) = 84 bytes. Not a hot-path struct — computed once per frame.

// Compute the view matrix from camera position and target.
glm::mat4 computeViewMatrix(const Camera& cam);

// Compute the projection matrix based on projection type.
glm::mat4 computeProjectionMatrix(const Camera& cam);

// Convenience: view * projection (the combined matrix uploaded to shaders).
glm::mat4 computeViewProjectionMatrix(const Camera& cam);

} // namespace ffe::renderer
```

### 7.2 Implementation

```cpp
// engine/renderer/camera.cpp

#include "renderer/camera.h"

namespace ffe::renderer {

glm::mat4 computeViewMatrix(const Camera& cam) {
    return glm::lookAt(cam.position, cam.target, cam.up);
}

glm::mat4 computeProjectionMatrix(const Camera& cam) {
    if (cam.projType == ProjectionType::PERSPECTIVE) {
        const f32 aspect = cam.viewportWidth / cam.viewportHeight;
        return glm::perspective(
            glm::radians(cam.fovDegrees),
            aspect,
            cam.nearPlane,
            cam.farPlane
        );
    } else {
        return glm::ortho(
            cam.orthoLeft, cam.orthoRight,
            cam.orthoBottom, cam.orthoTop,
            cam.nearPlane, cam.farPlane
        );
    }
}

glm::mat4 computeViewProjectionMatrix(const Camera& cam) {
    return computeProjectionMatrix(cam) * computeViewMatrix(cam);
}

} // namespace ffe::renderer
```

### 7.3 Camera Integration with Rendering

The camera is stored in `Application` (or as an ECS singleton component). Each frame, `Application::render(alpha)` computes the view-projection matrix and uploads it to the currently bound shader:

```cpp
// In Application::render():
const glm::mat4 vp = ffe::renderer::computeViewProjectionMatrix(m_camera);
// This matrix is cached in rhi_opengl.cpp and re-uploaded whenever bindShader() is called.
ffe::rhi::setViewProjection(vp);
```

For 2D games (the first playable milestone), the camera is orthographic with the viewport sized in pixels. A sprite at world position (100, 200) renders at pixel (100, 200) relative to the screen center. This is the simplest mental model for beginners.

---

## 8. Sprite/2D Rendering

This is the most important section for the first playable milestone. A 2D game that cannot draw thousands of sprites at 60 fps on LEGACY hardware is a failure.

### 8.1 The Problem

Naive sprite rendering issues one draw call per sprite. Each draw call involves: bind texture, set uniforms, glDrawElements. On a 2012 GPU, the driver overhead is ~5 microseconds per draw call. At 60 fps, one frame is 16.67 ms. That gives us a budget of ~3300 draw calls per frame before we blow the entire frame time on driver overhead alone — and we have not done any actual rendering yet.

A typical 2D game has 500-2000 visible sprites. With naive rendering, that is 500-2000 draw calls. Possible on LEGACY, but leaves no budget for anything else.

**Solution: sprite batching.** Group sprites that share the same texture into a single draw call by writing all their vertices into one dynamic vertex buffer.

### 8.2 SpriteBatch

```cpp
// engine/renderer/sprite_batch.h
#pragma once

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glm/glm.hpp>

namespace ffe::renderer {

// Maximum sprites per batch. Each sprite is 4 vertices * 32 bytes = 128 bytes.
// 2048 sprites * 128 bytes = 256 KB of vertex data per batch.
// This fits in L2 cache and is a comfortable GPU upload size.
inline constexpr u32 MAX_SPRITES_PER_BATCH = 2048;

// Maximum vertices and indices per batch.
inline constexpr u32 MAX_BATCH_VERTICES = MAX_SPRITES_PER_BATCH * 4;  // 4 vertices per quad
inline constexpr u32 MAX_BATCH_INDICES  = MAX_SPRITES_PER_BATCH * 6;  // 6 indices per quad (2 tris)

// A single sprite instance to be batched. This is the input from game code.
struct SpriteInstance {
    glm::vec2 position;     // World position (center of sprite)
    glm::vec2 size;         // Width, height in world units
    glm::vec2 uvMin;        // Top-left UV (for atlas sub-regions)
    glm::vec2 uvMax;        // Bottom-right UV
    glm::vec4 color;        // Tint color (multiplied with texture)
    f32 rotation;           // Radians, counter-clockwise
    f32 depth;              // Z-order (0.0 = back, 1.0 = front)
};
// sizeof(SpriteInstance) = 48 bytes

struct SpriteBatch {
    // CPU-side vertex staging buffer. Written each frame, uploaded to GPU.
    SpriteVertex* vertices  = nullptr;   // Points into frame arena
    u32 vertexCount         = 0;

    // GPU resources — created once, reused every frame.
    rhi::BufferHandle vertexBuffer;   // DYNAMIC usage
    rhi::BufferHandle indexBuffer;    // STATIC usage (indices never change)
    rhi::ShaderHandle shader;         // The sprite shader

    // State tracking
    rhi::TextureHandle currentTexture;
    u32 spriteCount         = 0;       // Sprites in current batch
    u32 drawCallCount       = 0;       // Total draw calls this frame (for profiling)
};

// Initialize the sprite batch. Creates GPU buffers.
// vertexStaging must point to at least MAX_BATCH_VERTICES * sizeof(SpriteVertex) bytes
// in the frame arena.
void initSpriteBatch(SpriteBatch& batch, rhi::ShaderHandle spriteShader);

// Shutdown — destroy GPU buffers.
void shutdownSpriteBatch(SpriteBatch& batch);

// Begin a new frame of sprite rendering. Resets counters.
// vertexStaging must be freshly allocated from the frame arena.
void beginSpriteBatch(SpriteBatch& batch, SpriteVertex* vertexStaging);

// Add a sprite to the batch. If the texture changes or the batch is full,
// the current batch is flushed (uploaded + drawn) automatically.
void addSprite(SpriteBatch& batch, rhi::TextureHandle texture, const SpriteInstance& sprite);

// Flush any remaining sprites. Must be called after all sprites are added.
void endSpriteBatch(SpriteBatch& batch);

} // namespace ffe::renderer
```

### 8.3 SpriteBatch Implementation

```cpp
// engine/renderer/sprite_batch.cpp (pseudocode — shows the algorithm, not every line)

void initSpriteBatch(SpriteBatch& batch, rhi::ShaderHandle spriteShader) {
    batch.shader = spriteShader;

    // Create the dynamic vertex buffer — uploaded every frame
    rhi::BufferDesc vbDesc;
    vbDesc.type      = rhi::BufferType::VERTEX;
    vbDesc.usage     = rhi::BufferUsage::DYNAMIC;
    vbDesc.sizeBytes = MAX_BATCH_VERTICES * sizeof(SpriteVertex);
    vbDesc.data      = nullptr;
    batch.vertexBuffer = rhi::createBuffer(vbDesc);

    // Create the static index buffer — same quad indices every frame
    // Pattern: 0,1,2, 2,3,0, 4,5,6, 6,7,4, ...
    u16 indices[MAX_BATCH_INDICES];
    for (u32 i = 0; i < MAX_SPRITES_PER_BATCH; ++i) {
        const u32 base = i * 4;
        const u32 idx  = i * 6;
        indices[idx + 0] = static_cast<u16>(base + 0);
        indices[idx + 1] = static_cast<u16>(base + 1);
        indices[idx + 2] = static_cast<u16>(base + 2);
        indices[idx + 3] = static_cast<u16>(base + 2);
        indices[idx + 4] = static_cast<u16>(base + 3);
        indices[idx + 5] = static_cast<u16>(base + 0);
    }

    rhi::BufferDesc ibDesc;
    ibDesc.type      = rhi::BufferType::INDEX;
    ibDesc.usage     = rhi::BufferUsage::STATIC;
    ibDesc.sizeBytes = sizeof(indices);
    ibDesc.data      = indices;
    batch.indexBuffer = rhi::createBuffer(ibDesc);
}

void beginSpriteBatch(SpriteBatch& batch, SpriteVertex* vertexStaging) {
    batch.vertices    = vertexStaging;
    batch.vertexCount = 0;
    batch.spriteCount = 0;
    batch.drawCallCount = 0;
    batch.currentTexture = {};
}

void addSprite(SpriteBatch& batch, rhi::TextureHandle texture, const SpriteInstance& sprite) {
    // If the texture changed or the batch is full, flush
    if ((texture.id != batch.currentTexture.id && batch.spriteCount > 0) ||
        batch.spriteCount >= MAX_SPRITES_PER_BATCH) {
        flushBatch(batch);
    }

    batch.currentTexture = texture;

    // Compute rotated quad corners
    const f32 hw = sprite.size.x * 0.5f;
    const f32 hh = sprite.size.y * 0.5f;
    const f32 cosR = cosf(sprite.rotation);
    const f32 sinR = sinf(sprite.rotation);

    // Corners relative to center: TL, TR, BR, BL
    const glm::vec2 offsets[4] = {
        {-hw, -hh}, { hw, -hh}, { hw,  hh}, {-hw,  hh}
    };

    const u32 base = batch.vertexCount;
    for (u32 j = 0; j < 4; ++j) {
        SpriteVertex& v = batch.vertices[base + j];
        // Rotate around center
        v.x = sprite.position.x + offsets[j].x * cosR - offsets[j].y * sinR;
        v.y = sprite.position.y + offsets[j].x * sinR + offsets[j].y * cosR;
        v.r = sprite.color.r;
        v.g = sprite.color.g;
        v.b = sprite.color.b;
        v.a = sprite.color.a;
    }

    // UVs: TL, TR, BR, BL
    batch.vertices[base + 0].u = sprite.uvMin.x;
    batch.vertices[base + 0].v = sprite.uvMin.y;
    batch.vertices[base + 1].u = sprite.uvMax.x;
    batch.vertices[base + 1].v = sprite.uvMin.y;
    batch.vertices[base + 2].u = sprite.uvMax.x;
    batch.vertices[base + 2].v = sprite.uvMax.y;
    batch.vertices[base + 3].u = sprite.uvMin.x;
    batch.vertices[base + 3].v = sprite.uvMax.y;

    batch.vertexCount += 4;
    batch.spriteCount++;
}

// Internal: upload current vertex data and issue draw call
static void flushBatch(SpriteBatch& batch) {
    if (batch.spriteCount == 0) return;

    // Upload vertex data to GPU
    const u32 uploadBytes = batch.vertexCount * sizeof(SpriteVertex);
    rhi::updateBuffer(batch.vertexBuffer, batch.vertices, uploadBytes, 0);

    // Bind resources
    rhi::bindShader(batch.shader);
    rhi::bindTexture(batch.currentTexture, 0);

    // Draw
    rhi::drawIndexed(batch.vertexBuffer, batch.indexBuffer,
                     batch.spriteCount * 6, 0, 0);

    batch.drawCallCount++;

    // Reset for next batch (reuse the same staging memory)
    batch.vertexCount = 0;
    batch.spriteCount = 0;
}

void endSpriteBatch(SpriteBatch& batch) {
    flushBatch(batch);
}
```

### 8.4 Performance Analysis

On LEGACY hardware, sprite batching gives us:

- **2048 sprites per draw call** (one texture, one shader).
- A typical 2D game with 1000 sprites and 4 texture atlases = **4 draw calls** (worst case, if sprites alternate textures randomly — sort by texture to avoid this).
- Vertex upload: 2048 sprites * 4 vertices * 32 bytes = 256 KB per batch. On PCI-E 2.0 (LEGACY era), upload bandwidth is ~4 GB/s. 256 KB takes ~64 microseconds. Acceptable.
- CPU vertex generation: 2048 sprites * 4 vertices * ~10 instructions per vertex = ~82K instructions. At 2 GHz with 1 IPC (pessimistic for 2012 CPU), that is ~41 microseconds. Acceptable.

**Total frame time for 2000 sprites:** ~4 draw calls * 5 us driver overhead + ~130 us vertex generation + ~128 us upload + GPU rasterization. Well under 1 ms total. We have 15 ms of budget remaining for game logic, physics, and audio.

### 8.5 Texture Atlas Support

A texture atlas packs multiple sprite images into a single large texture. Each sprite references a sub-region via UV coordinates (`uvMin`, `uvMax` in `SpriteInstance`). This is critical for batching — sprites from the same atlas share a texture and can be drawn in a single batch.

The atlas itself is just a `TextureHandle`. FFE does not provide an atlas packing tool — the developer uses external tools (TexturePacker, free alternatives) and provides the atlas image plus a JSON descriptor file mapping sprite names to UV rects.

```cpp
// Atlas descriptor format (loaded from JSON):
// {
//   "texture": "sprites.png",
//   "sprites": {
//     "player_idle": { "x": 0, "y": 0, "w": 64, "h": 64 },
//     "player_run_1": { "x": 64, "y": 0, "w": 64, "h": 64 },
//     ...
//   }
// }
//
// UV computation from pixel coordinates:
// uvMin = { x / atlasWidth, y / atlasHeight }
// uvMax = { (x + w) / atlasWidth, (y + h) / atlasHeight }
```

Atlas loading and sprite lookup will be handled by a higher-level `SpriteSheet` component in the asset system (future ADR). For now, the renderer only cares about UVs — how those UVs were computed is not its concern.

### 8.6 Layer/Z-Ordering

Sprites have two ordering mechanisms:

1. **Layer** (4 bits in the sort key, values 0-15): Coarse ordering. Background = 0, world = 1, foreground = 2, UI = 8, debug overlay = 15. Layers are opaque to the renderer — game code defines what each layer number means.

2. **Depth** (24 bits in the sort key, values 0-16777215): Fine ordering within a layer. Sprites with lower depth values are drawn first (farther from camera). This is the z-order for things like "character in front of tree."

For 2D rendering, depth testing is **disabled** (DepthFunc::NONE). Ordering is entirely determined by the sort key. Sprites are drawn back-to-front (painter's algorithm) within each layer. Alpha blending is enabled (BlendMode::ALPHA).

The default pipeline state for 2D sprite rendering:

```cpp
PipelineState spritePipeline;
spritePipeline.blend      = BlendMode::ALPHA;
spritePipeline.depth      = DepthFunc::NONE;
spritePipeline.cull       = CullMode::NONE;
spritePipeline.depthWrite = false;
spritePipeline.wireframe  = false;
```

---

## 9. Integration with ECS

### 9.1 Renderable Components

```cpp
// These components are defined in renderer/render_system.h, not in core/types.h.
// They are renderer-specific and should not pollute the core.

namespace ffe {

// Sprite component — for 2D rendering.
// This is the same Sprite from ADR-001 Section 4.3, now with full detail.
struct Sprite {
    rhi::TextureHandle texture;  // 4 bytes — handle to texture (or atlas)
    glm::vec2 size;              // 8 bytes — width, height in world units
    glm::vec2 uvMin;             // 8 bytes — atlas sub-region
    glm::vec2 uvMax;             // 8 bytes — atlas sub-region
    glm::vec4 color;             // 16 bytes — tint color
    i16 layer;                   // 2 bytes — render layer (0-15 used, i16 for headroom)
    i16 sortOrder;               // 2 bytes — sub-order within layer
    // Total: 48 bytes — fits in one cache line
};

// MeshRenderer component — for 3D rendering (future).
struct MeshRenderer {
    rhi::BufferHandle vertexBuffer;  // 4 bytes
    rhi::BufferHandle indexBuffer;   // 4 bytes
    rhi::TextureHandle texture;      // 4 bytes
    rhi::ShaderHandle shader;        // 4 bytes
    u32 indexCount;                   // 4 bytes
    // Total: 20 bytes
};

} // namespace ffe
```

### 9.2 The Render System

The render system is a `SystemUpdateFn` (as defined in ADR-001) that runs at priority 500. It iterates all entities with renderable components and generates `DrawCommand` entries in the render queue.

The render system does NOT issue GL calls. It only prepares data. The actual GL calls happen in `Application::render(alpha)`, which sorts the queue and submits it.

```cpp
// engine/renderer/render_system.h
#pragma once

#include "core/system.h"
#include "renderer/render_queue.h"
#include "renderer/sprite_batch.h"

namespace ffe::renderer {

// The render preparation system. Registered as a SystemUpdateFn at priority 500.
// It collects all renderable entities and builds the render queue.
void renderPrepareSystem(World& world, float dt);

// Priority constant for registration.
inline constexpr i32 RENDER_PREPARE_PRIORITY = 500;

} // namespace ffe::renderer
```

```cpp
// engine/renderer/render_system.cpp

#include "renderer/render_system.h"
#include "core/ecs.h"
#include "core/types.h"

// Components
#include "renderer/camera.h"  // For Sprite, MeshRenderer, Transform

namespace ffe::renderer {

void renderPrepareSystem(World& world, float dt) {
    ZoneScopedN("RenderPrepare");
    (void)dt; // Unused — render prep does not depend on delta time

    // Get the render queue from the world (stored as a singleton/resource).
    // This is a pointer to the arena-allocated RenderQueue in Application.
    auto* queue = world.registry().ctx().find<RenderQueue*>();
    if (!queue || !*queue) return;
    RenderQueue& rq = **queue;

    // Iterate all entities with Transform + Sprite
    auto spriteView = world.view<const Transform, const Sprite>();
    for (auto [entity, transform, sprite] : spriteView.each()) {
        if (rq.count >= rq.capacity) break;

        DrawCommand& cmd = rq.commands[rq.count++];
        cmd.sortKey = makeSortKey(
            static_cast<u8>(glm::clamp(static_cast<i32>(sprite.layer), 0, 15)),
            static_cast<u8>(BuiltinShader::SPRITE),
            sprite.texture.id & 0xFFF,
            transform.position.z, // Use Z as depth for 2D z-ordering
            static_cast<u16>(sprite.sortOrder & 0xFFFF)
        );

        cmd.shader  = getShader(*world.registry().ctx().find<ShaderLibrary>(), BuiltinShader::SPRITE);
        cmd.texture = sprite.texture;

        // Sprite-specific transform data packed into the DrawCommand
        cmd.posX   = transform.position.x;
        cmd.posY   = transform.position.y;
        cmd.scaleX = transform.scale.x * sprite.size.x;
        cmd.scaleY = transform.scale.y * sprite.size.y;

        // Sprite pipeline: alpha blend, no depth test, no culling
        PipelineState ps;
        ps.blend      = BlendMode::ALPHA;
        ps.depth      = DepthFunc::NONE;
        ps.cull       = CullMode::NONE;
        ps.depthWrite = false;
        cmd.pipelineBits = packPipelineBits(ps);
    }

    // Future: iterate MeshRenderer entities for 3D objects
}

} // namespace ffe::renderer
```

### 9.3 Registration

During `Application::startup()`, the render system is registered:

```cpp
// In Application::startup():
world.registerSystem({
    "RenderPrepare",
    ffe::renderer::renderPrepareSystem,
    ffe::renderer::RENDER_PREPARE_PRIORITY
});
```

### 9.4 Data Flow Summary

```
tick()
  -> renderPrepareSystem() [priority 500]
     -> iterates ECS entities with Sprite/MeshRenderer + Transform
     -> writes DrawCommands into RenderQueue (arena-allocated)

render(alpha)
  -> sortRenderQueue()          [sort by u64 sortKey]
  -> beginFrame()               [glClear]
  -> setViewProjection()        [upload camera matrix]
  -> submitRenderQueue()        [iterate commands, bind state, issue GL draw calls]
     -> for sprites: SpriteBatch collects commands with same texture, flushes in batches
  -> endFrame()                 [glfwSwapBuffers]
  -> queue.clear()              [reset count, arena memory reused next frame]
```

**Note on sprite batching vs draw command submission:** There are two paths here, and renderer-specialist should implement both:

1. **DrawCommand path** (general purpose): For 3D meshes and any non-batched geometry, `submitRenderQueue()` processes commands one at a time with state change tracking.

2. **SpriteBatch path** (optimized 2D): For sprites, the render system can bypass the DrawCommand queue entirely and feed sprites directly into the `SpriteBatch`. This avoids the overhead of generating and sorting DrawCommands when all sprites use the same shader and just need texture sorting.

For the first implementation, use the SpriteBatch path for all 2D rendering. The DrawCommand queue is there for 3D meshes and mixed 2D/3D scenes.

---

## 10. Resource Lifecycle

### 10.1 No RAII for GPU Resources

GPU resources (buffers, textures, shaders) must NOT use RAII (destructor-based cleanup). The reason: destructors run in an unpredictable order during shutdown. If the OpenGL context is destroyed before a buffer's destructor runs, the `glDeleteBuffers()` call is undefined behavior — it may crash, corrupt memory, or silently leak.

Instead, all GPU resources use explicit `init()` / `shutdown()` functions called in a deterministic order.

### 10.2 Ownership Rules

- `Application` owns the GLFW window and OpenGL context.
- `Application` calls `rhi::init()` after context creation and `rhi::shutdown()` before context destruction.
- `rhi::shutdown()` destroys ALL remaining GPU resources (buffers, textures, shaders) and logs warnings for any resources that were not explicitly destroyed by their owners.
- The `ShaderLibrary` is initialized after `rhi::init()` and shut down before `rhi::shutdown()`.
- The `SpriteBatch` is initialized after the `ShaderLibrary` and shut down before it.

**Shutdown order:**

```
Application::shutdown()
  1. shutdownSpriteBatch()     — destroys batch VBO/IBO
  2. shutdownShaderLibrary()   — destroys shader programs
  3. rhi::shutdown()           — destroys any leaked resources, cleans up GL state
  4. glfwDestroyWindow()       — destroys the GL context
  5. glfwTerminate()           — cleans up GLFW
```

### 10.3 Resource Handles Are Not Owning

A `BufferHandle`, `TextureHandle`, or `ShaderHandle` is a plain integer. Copying it does not increment a reference count. Destroying the underlying resource invalidates all copies of the handle. Using an invalidated handle is undefined behavior (the backend may reuse the slot).

This is the correct design for a game engine. Reference counting adds overhead to every handle copy and makes resource lifetime non-deterministic. Game resources have clear, well-defined lifetimes (level load/unload, application startup/shutdown). The developer is responsible for managing those lifetimes explicitly.

### 10.4 Resource Leak Detection

In debug builds, `rhi::shutdown()` iterates all resource pools and logs any resources that are still alive:

```cpp
void shutdown() {
    // Check for leaked buffers
    for (u32 i = 1; i < MAX_BUFFERS; ++i) {
        if (s_buffers[i].alive) {
            FFE_LOG_WARN("RHI", "Leaked buffer handle %u (size: %u bytes)", i, s_buffers[i].sizeBytes);
            glDeleteBuffers(1, &s_buffers[i].glId);
            s_buffers[i].alive = false;
        }
    }
    // Same for textures and shaders...
}
```

---

## 11. Dependencies

### 11.1 New vcpkg Packages

**glad** must be added to `vcpkg.json`:

```json
{
  "name": "fastfreeengine",
  "version-string": "0.1.0",
  "dependencies": [
    "entt",
    "glad",
    "joltphysics",
    "sol2",
    "glm",
    "imgui",
    "stb",
    "nlohmann-json",
    "tracy",
    "catch2",
    "vulkan-memory-allocator"
  ]
}
```

### 11.2 System Packages

GLFW is a system package, not a vcpkg package. It is already installed as `libglfw3-dev`. No changes needed.

For CI headless rendering: `xvfb` (`xvfb-run`) is required. The CI pipeline wraps test execution with:

```bash
xvfb-run -a cmake --build build --target test
```

Renderer tests that create a GL context must check for `ApplicationConfig::headless` and skip GL validation in that mode.

### 11.3 glad Configuration

glad must be configured for OpenGL 3.3 core profile. When installed via vcpkg, it generates a loader for the requested GL version. The vcpkg port for glad supports custom configurations via features. We need:

- Profile: core
- API: gl=3.3
- No extensions required for LEGACY (extensions are opt-in for higher tiers)

If the vcpkg glad port does not support fine-grained version selection, generate the glad loader manually from https://glad.dav1d.de/ and commit the generated files to `engine/renderer/opengl/glad/`. This is a common and acceptable approach.

---

## 12. Tier Support Matrix

| Feature | LEGACY (GL 3.3) | STANDARD (GL 4.5) | MODERN (Vulkan) |
|---------|-----|------|------|
| Sprite batching | Yes | Yes | Yes |
| Texture atlases | Yes | Yes | Yes |
| Basic 3D meshes | Yes (no instancing) | Yes (instanced) | Yes (instanced) |
| Basic lighting | Directional only, per-vertex | Per-pixel, point + spot | PBR, multiple lights |
| Shadow maps | No | Yes (1 cascade) | Yes (4 cascades) |
| Post-processing | No | Bloom, tonemap | Full chain |
| Compute shaders | No | Yes | Yes |
| Particle systems | CPU only, sprite batch | GPU compute | GPU compute |
| Max draw calls/frame | 8,192 | 32,768 | 131,072 |
| Max textures loaded | 2,048 | 4,096 | 16,384 |
| Max texture size | 4096x4096 | 8192x8192 | 16384x16384 |
| MSAA | No | 4x | Optional (TAA preferred) |
| VSync | On (default) | Configurable | Configurable |
| Render target / FBO | 1 (backbuffer only) | Multiple (for post-FX) | Multiple |
| Instanced rendering | No | Yes | Yes |
| Bindless textures | No | No | Yes |
| Ray tracing | No | No | Optional (Vulkan RT) |

**Key constraint for LEGACY:** No framebuffer objects beyond the default backbuffer. This means no render-to-texture, no post-processing, no shadow maps. All rendering goes directly to the screen. This is the single biggest capability gap between LEGACY and STANDARD. It keeps the renderer simple and avoids FBO-related driver bugs that plagued GL 3.x era integrated GPUs.

**Note:** LEGACY OpenGL 3.3 technically supports FBOs and instanced rendering. We deliberately exclude them from the LEGACY tier because:
- FBOs on Intel HD 4000 have well-documented performance cliffs and driver bugs.
- Instanced rendering on GL 3.3 integrated GPUs is often slower than batching due to poor driver implementation.
- Excluding them keeps the LEGACY renderer dead simple and predictable.

---

## 13. Implementation Checklist

This is the exact set of files renderer-specialist must create. Each file includes its purpose and approximate line count.

| File | Purpose | ~Lines |
|------|---------|--------|
| `engine/renderer/CMakeLists.txt` | Build target, backend selection, link deps | 35 |
| `engine/renderer/rhi_types.h` | All enums, descriptors, handles from Section 4 | 120 |
| `engine/renderer/rhi.h` | RHI free function declarations from Sections 4-5 | 80 |
| `engine/renderer/opengl/rhi_opengl.h` | Internal GL helpers, resource pool structs | 80 |
| `engine/renderer/opengl/rhi_opengl.cpp` | All RHI function implementations for GL 3.3 | 450 |
| `engine/renderer/opengl/gl_debug.h` | GL debug callback declaration | 20 |
| `engine/renderer/opengl/gl_debug.cpp` | GL debug output setup and callback | 50 |
| `engine/renderer/render_queue.h` | DrawCommand struct, RenderQueue, sort key helpers | 100 |
| `engine/renderer/render_queue.cpp` | sortRenderQueue(), submitRenderQueue() | 120 |
| `engine/renderer/camera.h` | Camera struct, projection helpers from Section 7 | 50 |
| `engine/renderer/camera.cpp` | View/projection matrix computation | 40 |
| `engine/renderer/sprite_batch.h` | SpriteBatch struct, SpriteInstance, SpriteVertex | 70 |
| `engine/renderer/sprite_batch.cpp` | Batching, flushing, vertex generation | 180 |
| `engine/renderer/render_system.h` | Render preparation system declaration | 20 |
| `engine/renderer/render_system.cpp` | ECS iteration, DrawCommand generation | 80 |
| `engine/renderer/shader_library.h` | ShaderLibrary struct, BuiltinShader enum | 40 |
| `engine/renderer/shader_library.cpp` | Shader loading from files, compilation | 100 |
| `engine/renderer/.context.md` | AI-native documentation for the renderer | 150 |
| `shaders/legacy/sprite.vert` | Sprite vertex shader | 18 |
| `shaders/legacy/sprite.frag` | Sprite fragment shader | 15 |
| `shaders/legacy/solid.vert` | Solid color vertex shader | 12 |
| `shaders/legacy/solid.frag` | Solid color fragment shader | 10 |
| `shaders/legacy/textured.vert` | Textured mesh vertex shader | 20 |
| `shaders/legacy/textured.frag` | Textured mesh fragment shader | 14 |

**Total:** ~1,854 lines of code.

Additionally, the following existing files need modifications:

| File | Change | ~Lines Changed |
|------|--------|----------------|
| `vcpkg.json` | Add `"glad"` dependency | 1 |
| `engine/CMakeLists.txt` | Uncomment `ffe_renderer` link | 1 |
| `engine/core/application.h` | Add renderer-related members (window, camera, render queue, sprite batch) | 15 |
| `engine/core/application.cpp` | Implement startup steps 4-5, shutdown steps 4-5, `render()` body | 60 |

---

## 14. What This Prevents (and Why That Is OK)

### 14.1 No Render-to-Texture on LEGACY

The LEGACY tier cannot render to offscreen framebuffers. This means no shadow maps, no post-processing (bloom, blur, tonemapping), no picture-in-picture, no water reflections.

**Why that is OK:** A 2012 integrated GPU struggles with FBO-related driver bugs and the bandwidth cost of multiple render passes. The first playable target is a 2D game — it does not need any of these features. STANDARD tier adds FBO support.

### 14.2 No Custom Shader Support on LEGACY

Game developers cannot write custom shaders on the LEGACY tier. They must use the three built-in shaders (solid, textured, sprite).

**Why that is OK:** Custom shaders are the #1 source of "it works on my machine but not on the player's machine" bugs, especially on old integrated GPUs with buggy GLSL compilers. By shipping only tested, known-good shaders on LEGACY, we guarantee visual correctness. STANDARD tier exposes custom shader support.

### 14.3 No Instanced Rendering on LEGACY

Each mesh is drawn with a separate `glDrawElements` call. There is no hardware instancing.

**Why that is OK:** On LEGACY-era integrated GPUs, instanced rendering is often no faster than batched rendering due to driver overhead. The sprite batcher already eliminates the need for instancing in 2D. For 3D on LEGACY, the entity count is capped at ~20,000 (ADR-001) and most scenes have far fewer drawable meshes than entities. The 8,192 draw call budget is sufficient.

### 14.4 No Multi-Threaded Rendering

All GL calls happen on the main thread. There is no render thread, no command recording from worker threads.

**Why that is OK:** OpenGL contexts are single-threaded by design. Multi-threaded GL requires `GL_ARB_parallel_shader_compile` (not available on GL 3.3) or manual context sharing (complex and bug-prone). Vulkan natively supports multi-threaded command recording — that is the MODERN tier path. For LEGACY and STANDARD, single-threaded rendering is correct.

### 14.5 No Compressed Texture Formats

The LEGACY tier loads RGBA8/RGB8/R8 uncompressed textures only. No DXT/BC, no ETC, no ASTC.

**Why that is OK:** Compressed texture support on GL 3.3 requires extensions (`GL_EXT_texture_compression_s3tc`) that are not universally available on all LEGACY-era GPUs. Uncompressed RGBA8 is universally supported and fast to load. The VRAM budget (500 MB) is sufficient for a 2D game's texture needs. STANDARD tier adds compressed format support.

### 14.6 Fixed Vertex Formats

The two vertex formats (SpriteVertex and MeshVertex) are hardcoded at 32 bytes each. Game developers cannot define custom vertex formats on LEGACY.

**Why that is OK:** Custom vertex formats require careful VAO management, vertex attribute configuration, and matching shader input layouts — all of which are common sources of bugs. Two well-optimized formats cover 95% of use cases: 2D sprites and 3D textured meshes. If a game needs a custom format (e.g., for skeletal animation with bone weights), it targets STANDARD tier.

### 14.7 No Runtime Shader Compilation After Init

All shaders are compiled during `Application::startup()`. There is no facility to compile new shaders after the application is running.

**Why that is OK:** Shader compilation causes visible hitches (50-200 ms on LEGACY hardware). Compiling shaders at startup means all hitches happen behind a loading screen, not during gameplay. The shader library is fixed and small (3 programs). If a future system needs dynamic shader compilation (e.g., material permutations), it must pre-compile all permutations at startup or use SPIR-V caching (MODERN tier).
