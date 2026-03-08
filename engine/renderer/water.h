#pragma once

// water.h — Phase 9 M6: Animated water plane with Fresnel blending + planar reflection
// Tier: LEGACY (animated Fresnel, no reflection FBO)
//       STANDARD+ (+ planar reflection FBO, half-res)
//
// Overview:
//   WaterManager renders up to MAX_WATER_SURFACES flat water planes.
//   On LEGACY it animates UV scrolling and computes per-vertex Fresnel with a sine
//   distortion term. On STANDARD+ it additionally renders a half-resolution planar
//   reflection FBO and blends the reflected scene into the water surface.
//
// Constraints:
//   - No heap allocation per frame. All GPU buffers allocated at createWater() time.
//   - No virtual functions in hot paths.
//   - No RTTI, no exceptions.
//   - Function pointer used for the scene render callback (no std::function, no heap).
//   - GLSL 330 core for LEGACY tier; uses WATER shader (BuiltinShader::WATER = 20).
//
// Thread safety: single-threaded. All methods must be called from the render thread.

#include "core/types.h"
#include "renderer/rhi_types.h"

#include <glm/glm.hpp>

// Forward declarations for the old flat-function API used by application.cpp.
namespace ffe {
    class World;
}
namespace ffe::renderer {
    struct Camera;
    struct FogParams;
}

namespace ffe::renderer {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Maximum simultaneous water surfaces (WaterManager).
// Each surface: 1 VAO + 1 VBO (quad verts) + optional reflection FBO + colour/depth RBOs.
// 8 surfaces x ~4 MB reflection FBO (half-res RGBA16F) = 32 MB VRAM -- acceptable on LEGACY.
static constexpr u32 MAX_WATER_SURFACES = 8;

// ---------------------------------------------------------------------------
// Constants for the legacy flat-function API (used by application.cpp + tests)
// ---------------------------------------------------------------------------

static constexpr f32 DEFAULT_WATER_LEVEL          = 0.0f;
static constexpr f32 DEFAULT_WAVE_SPEED           = 0.03f;
static constexpr f32 DEFAULT_WAVE_SCALE           = 0.02f;
static constexpr f32 DEFAULT_FRESNEL_POWER        = 2.0f;
static constexpr f32 DEFAULT_FRESNEL_BIAS         = 0.1f;
static constexpr f32 DEFAULT_MAX_DEPTH            = 10.0f;
static constexpr f32 DEFAULT_REFLECTION_DISTORTION = 0.02f;
static constexpr f32 DEFAULT_WATER_EXTENT         = 1000.0f;

// ---------------------------------------------------------------------------
// WaterConfig — legacy ECS singleton used by application.cpp and Lua bindings.
// ---------------------------------------------------------------------------

struct WaterConfig {
    bool        enabled              = false;
    f32         waterLevel           = DEFAULT_WATER_LEVEL;
    glm::vec4   shallowColor         = {0.1f, 0.4f, 0.6f, 0.6f};
    glm::vec4   deepColor            = {0.0f, 0.1f, 0.3f, 0.9f};
    f32         maxDepth             = DEFAULT_MAX_DEPTH;
    f32         waveSpeed            = DEFAULT_WAVE_SPEED;
    f32         waveScale            = DEFAULT_WAVE_SCALE;
    f32         fresnelPower         = DEFAULT_FRESNEL_POWER;
    f32         fresnelBias          = DEFAULT_FRESNEL_BIAS;
    f32         reflectionDistortion = DEFAULT_REFLECTION_DISTORTION;
};

// ---------------------------------------------------------------------------
// Water — ECS tag component (legacy, one-plane design).
// Presence triggers water rendering in the render loop.
// ---------------------------------------------------------------------------

struct Water {
    u32 _tag = 1; // tag component — presence triggers water rendering
};
static_assert(sizeof(Water) == 4, "Water must be 4 bytes");

// ---------------------------------------------------------------------------
// WaterVertex — vertex layout for the water quad (legacy VAO).
// layout: vec3 position (12 bytes) + vec2 texCoord (8 bytes) = 20 bytes.
// slot 0: vec3 position, slot 1: vec2 texCoord.
// ---------------------------------------------------------------------------

struct WaterVertex {
    glm::vec3 position; // 12 bytes
    glm::vec2 texCoord; //  8 bytes
};
static_assert(sizeof(WaterVertex) == 20, "WaterVertex must be 20 bytes");
static_assert(offsetof(WaterVertex, position) == 0,  "WaterVertex::position must be at offset 0");
static_assert(offsetof(WaterVertex, texCoord) == 12, "WaterVertex::texCoord must be at offset 12");

// ---------------------------------------------------------------------------
// Legacy flat-function API (used by application.cpp, shader_library.cpp)
// ---------------------------------------------------------------------------

// Initialise the water rendering pipeline (reflection FBO + water quad VAO).
// Safe to call if GL is not yet available; returns false in that case.
bool initWater(i32 width, i32 height);

// Destroy all water GPU resources. Safe to call even if initWater() failed.
void shutdownWater();

// Resize the reflection FBO to match a new framebuffer size.
void resizeWaterFBOs(i32 width, i32 height);

// Render the scene into the reflection FBO (only if waterCfg.enabled).
void renderWaterReflection(World& world, const Camera& camera3d,
                           const FogParams& fog, const WaterConfig& waterCfg);

// Draw the water quad using alpha blending (only if waterCfg.enabled).
void renderWater(World& world, const Camera& camera3d,
                 const FogParams& fog, const WaterConfig& waterCfg, f32 time);

// Inject the compiled WATER shader handle (called by initShaderLibrary).
void setWaterShader(rhi::ShaderHandle water);

// Compute a reflected camera across the given waterLevel (used by tests + reflection pass).
Camera computeReflectionCamera(const Camera& camera, f32 waterLevel);

// ---------------------------------------------------------------------------
// Handle
// ---------------------------------------------------------------------------

// Opaque handle to a water surface. Value 0 (default) is always invalid.
// Same pattern as TerrainHandle, VegetationHandle.
struct WaterHandle {
    u32 id = 0;
    bool isValid() const { return id != 0; }
};
static_assert(sizeof(WaterHandle) == 4);

// ---------------------------------------------------------------------------
// WaterPlane -- world-space position and extents of the water surface
// ---------------------------------------------------------------------------

struct WaterPlane {
    float x;     // World-space centre X
    float y;     // World-space Y (surface elevation)
    float z;     // World-space centre Z
    float width; // Extent along the X axis (total, not half-extent)
    float depth; // Extent along the Z axis (total, not half-extent)
};
static_assert(sizeof(WaterPlane) == 20);

// ---------------------------------------------------------------------------
// WaterSurfaceConfig -- per-surface rendering parameters (WaterManager API)
// ---------------------------------------------------------------------------

struct WaterSurfaceConfig {
    // UV animation
    float waveSpeed     = 0.3f;  // UV scroll speed (world UVs per second)
    float waveScale     = 2.0f;  // UV tiling frequency (higher = more ripples)
    float waveAmplitude = 0.05f; // Sine distortion amplitude applied to UV lookup

    // Fresnel
    float fresnelPower  = 3.0f;  // Schlick Fresnel exponent (higher = sharper grazing highlight)

    // Reflection blend
    float reflectionStrength = 0.6f; // [0,1] blend weight of planar reflection over water color

    // Water tint
    glm::vec3 waterColor = {0.1f, 0.4f, 0.6f}; // Shallow / tinted surface color
    glm::vec3 deepColor  = {0.02f, 0.1f, 0.2f}; // Deep water color (blended by depth/angle)

    // Feature flags
    // reflectionEnabled is respected on STANDARD+ only; silently ignored on LEGACY.
    bool reflectionEnabled = true;
};

// ---------------------------------------------------------------------------
// WaterManager
// ---------------------------------------------------------------------------

// Manages a fixed pool of water surfaces with GPU buffers allocated at
// createWater() time. No per-frame heap allocation.
//
// Lifetime: construct as a member of the render coordinator (e.g. MeshRenderer).
// Call init() once after the OpenGL context exists.
// Call shutdown() before the context is destroyed.
//
// NOT a global singleton.
class WaterManager {
public:
    // --- Lifecycle ---

    // Allocate shared resource slots and record screen dimensions.
    // screenWidth/screenHeight are used to size half-resolution reflection FBOs
    // (STANDARD+ only). No GPU allocation occurs here -- GPU work happens in
    // createWater(). No heap allocation after this call.
    void init(int screenWidth, int screenHeight);

    // Free all GPU resources (VAOs, VBOs, FBOs, RBOs) for every active surface.
    // Safe to call even if init() was never called.
    void shutdown();

    // --- Shader handle injection ---

    // Provide the WATER shader handle resolved from ShaderLibrary (BuiltinShader::WATER = 20).
    // Must be called after init() and after initShaderLibrary().
    void setShaderHandle(rhi::ShaderHandle waterShader);

    // --- Surface management ---

    // Create a water surface at the given plane with the given config.
    // Allocates a quad VAO+VBO and, if cfg.reflectionEnabled and the hardware
    // tier allows, a half-resolution reflection FBO.
    // Returns a valid WaterHandle on success; returns WaterHandle{0} if
    // MAX_WATER_SURFACES is reached.
    // Cold path only -- do NOT call per frame.
    WaterHandle createWater(const WaterPlane& plane, const WaterSurfaceConfig& cfg);

    // Destroy a water surface and free its GPU resources.
    // No-op if the handle is invalid.
    void destroyWater(WaterHandle handle);

    // Update the per-surface configuration (wave params, colors, flags, etc.).
    // No-op if the handle is invalid.
    void setWaterConfig(WaterHandle handle, const WaterSurfaceConfig& cfg);

    // Read the current per-surface configuration.
    // Returns the config stored for the given handle, or a default-constructed
    // WaterSurfaceConfig{} if the handle is invalid.
    WaterSurfaceConfig getWaterConfig(WaterHandle handle) const;

    // --- Per-frame update ---

    // Accumulate elapsed time for UV scroll animation.
    // Must be called once per frame before render().
    void update(float dt);

    // --- Reflection pass (STANDARD+ only) ---

    // Render the scene into the half-resolution reflection FBO for each active
    // surface that has reflectionEnabled == true and a valid FBO.
    //
    // renderScene is a plain C function pointer (no std::function, no heap) that
    // the caller provides to draw the scene above the water plane. userData is
    // forwarded to the callback unchanged and may be nullptr.
    //
    // On LEGACY tier (reflection FBO not allocated or isSoftwareRenderer) this
    // is a no-op.
    void renderReflection(void (*renderScene)(void* userData), void* userData);

    // --- Water render pass ---

    // Draw all active water surfaces using the WATER shader.
    // Binds the reflection texture (if available), sets all uniforms, and issues
    // one glDrawArrays call per active surface.
    //
    // isSoftwareRenderer: when true, reflection FBO sampling is skipped and
    //   reflectionStrength is forced to 0 to avoid artefacts on Mesa/llvmpipe.
    void render(const glm::mat4& view, const glm::mat4& proj,
                const glm::vec3& cameraPos,
                const glm::vec3& lightDir,
                const glm::vec3& lightColor,
                const glm::vec3& ambientColor,
                bool isSoftwareRenderer);

private:
    // --- Per-surface slot ---
    struct WaterSlot {
        bool              active = false;
        WaterHandle       handle = {};
        WaterPlane        plane  = {};
        WaterSurfaceConfig config = {};

        // OpenGL object names (0 = unallocated)
        u32 vao = 0; // Vertex array object for the water quad
        u32 vbo = 0; // Vertex buffer: positions + UVs (4 vertices)
        u32 ibo = 0; // Index buffer: 6 indices (2 triangles)

        // Reflection FBO resources (STANDARD+ only; all 0 when not allocated).
        u32 reflFbo      = 0; // GL_FRAMEBUFFER object name
        u32 reflColorTex = 0; // GL_TEXTURE_2D -- RGBA8 colour attachment (half-res)
        u32 reflDepthRbo = 0; // GL_RENDERBUFFER -- depth24 attachment (half-res)

        // RHI texture handle wrapping the reflection colour attachment.
        // Used to bind the FBO colour output as a sampler2D in the water shader.
        // Invalid ({0}) when the reflection FBO is not allocated.
        rhi::TextureHandle reflTexture = {0};
    };

    // Fixed-size surface storage -- no dynamic allocation.
    WaterSlot m_slots[MAX_WATER_SURFACES] = {};

    // Monotonically increasing ID counter; 0 is reserved for invalid handles.
    u32 m_nextHandleId = 1;

    // Accumulated time for UV scroll animation (seconds).
    float m_time = 0.0f;

    // Screen dimensions used to allocate half-resolution reflection FBOs.
    int m_screenWidth  = 0;
    int m_screenHeight = 0;

    // WATER shader handle (BuiltinShader::WATER = 20), injected via setShaderHandle().
    rhi::ShaderHandle m_waterShader = {0};

    // Whether init() has been called successfully.
    bool m_initialised = false;

    // --- Internal helpers ---

    // Resolve a slot index from a WaterHandle.
    // Returns MAX_WATER_SURFACES (sentinel) if not found.
    u32 findSlot(WaterHandle handle) const;

    // Build the 6-vertex quad (two triangles) covering the WaterPlane extents
    // and upload to slot.vao/vbo. Called once per createWater().
    void uploadQuadGeometry(WaterSlot& slot);

    // Allocate the half-resolution reflection FBO for a slot.
    // Sets reflFbo, reflColorTex, reflDepthRbo.
    // Returns true on success; false if GL_FRAMEBUFFER_COMPLETE check fails.
    bool allocReflectionFbo(WaterSlot& slot);

    // Free the reflection FBO resources for a slot and zero all FBO fields.
    void freeReflectionFbo(WaterSlot& slot);
};

} // namespace ffe::renderer
