#pragma once

#include "core/types.h"
#include "core/system.h"
#include "renderer/rhi_types.h"
#include "renderer/mesh_loader.h"
#include "renderer/render_queue.h"
#include "renderer/skeleton.h"
#include "renderer/sprite_batch.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <new>

namespace ffe {

// --- Transform3D component — 3D world transform. ---
// Use this for 3D entities. Does NOT replace Transform (2D-only).
// 2D entities use Transform (scalar z-axis rotation); 3D entities use Transform3D.
struct Transform3D {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};  // 12 bytes
    glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f}; // 16 bytes — identity quaternion
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};  // 12 bytes
    u32       _pad     = 0;                     //  4 bytes — explicit alignment padding
};
static_assert(sizeof(Transform3D) == 44, "Transform3D must be 44 bytes");

// --- Mesh ECS component — references a loaded mesh asset ---
// The GPU resources (VAO, VBO, IBO) are owned by the mesh asset cache,
// not by the component. Multiple entities may reference the same MeshHandle.
struct Mesh {
    renderer::MeshHandle meshHandle;  // 4 bytes — which loaded mesh to render
    u32                  _pad = 0;    // 4 bytes — explicit padding
};
static_assert(sizeof(Mesh) == 8, "Mesh component must be 8 bytes");

// --- Material3D ECS component — per-entity 3D material override ---
// Optional: if not present on a 3D entity, the mesh renderer uses default values
// (white diffuse color, default white texture, builtin MESH_BLINN_PHONG shader).
struct Material3D {
    glm::vec4          diffuseColor    = {1.0f, 1.0f, 1.0f, 1.0f}; // 16 bytes
    rhi::TextureHandle diffuseTexture;   // 4 bytes (0 = use default white texture)
    rhi::ShaderHandle  shaderOverride;   // 4 bytes (0 = use builtin MESH_BLINN_PHONG)

    // Specular properties (Blinn-Phong)
    glm::vec3          specularColor   = {1.0f, 1.0f, 1.0f};       // 12 bytes
    f32                shininess       = 32.0f;                      //  4 bytes

    // Normal map and specular map textures (0 = none/disabled)
    rhi::TextureHandle normalMapTexture;   // 4 bytes
    rhi::TextureHandle specularMapTexture; // 4 bytes
};
static_assert(sizeof(Material3D) == 48, "Material3D must be 48 bytes");

// --- Skeleton ECS component — per-entity bone matrices for skeletal animation ---
// Each entity with a skinned mesh has its own Skeleton component containing
// the final bone transforms for the current animation frame. These are the
// matrices uploaded to the GPU as uniforms each draw call.
//
// Multiple entities sharing the same MeshHandle may be at different animation
// frames, so bone matrices are per-entity (instance state, not asset state).
//
// The bone matrices hold the final transforms: worldTransform * inverseBindMatrix,
// ready for vertex shader consumption.
struct Skeleton {
    glm::mat4 boneMatrices[renderer::MAX_BONES] = {}; // final bone transforms (model-space)
    u32       boneCount = 0;
};
// Skeleton is ~4100 bytes (64 mat4 + u32 + padding). Verify it's at least
// the expected minimum size (no silent truncation from incorrect MAX_BONES).
static_assert(sizeof(Skeleton) >= renderer::MAX_BONES * sizeof(glm::mat4) + sizeof(u32),
              "Skeleton too small — bone matrix array may be truncated");

// --- AnimationState ECS component — per-entity animation playback state ---
// Lightweight component (no pointers, no heap) that controls which animation
// clip is playing, at what time, and at what speed.
struct AnimationState {
    u32  clipIndex  = 0;      // index into MeshAnimations::clips[]
    f32  time       = 0.0f;   // current playback time in seconds
    f32  speed      = 1.0f;   // playback speed multiplier (1.0 = normal)
    bool looping    = true;
    bool playing    = false;
    u8   _pad[2]    = {};     // explicit padding
};
static_assert(sizeof(AnimationState) == 16, "AnimationState must be 16 bytes");

// --- Name component — human-readable entity label for the editor ---
// Fixed-size char array (no heap allocation). Default name is "Entity".
struct Name {
    char name[64] = "Entity";
};
static_assert(sizeof(Name) == 64, "Name must be 64 bytes");

// --- Parent component — references this entity's parent in the scene hierarchy ---
// Used by the editor's scene tree and hierarchy panel. entt::null means no parent.
struct Parent {
    entt::entity parent = entt::null;
};

// --- Children component — fixed-size list of child entities ---
// Max 32 children per entity. This avoids heap allocation in the component.
// The editor uses this alongside Parent for hierarchy traversal.
struct Children {
    entt::entity children[32] = {};
    uint32_t count = 0;
};
static_assert(sizeof(Children) <= 132, "Children must not exceed 132 bytes");

// --- Transform component (needed by render system) ---
// Defined here because the render system needs it and it is not yet in core.
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
    f32 rotation       = 0.0f; // Radians, z-axis rotation for 2D
};

// --- PreviousTransform component — used for render interpolation ---
// Mirrors Transform exactly. Snapshotted by CopyTransformSystem at the
// START of each tick (before gameplay systems update Transform).
// renderPrepareSystem lerps between PreviousTransform and Transform using
// the render alpha to eliminate fixed-timestep jitter.
// Opt in by adding this component alongside Transform + Sprite.
// Static entities (tilemaps, UI) can omit it — they render without lerp.
struct PreviousTransform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
    f32 rotation       = 0.0f;
};

// --- Sprite component ---
struct Sprite {
    rhi::TextureHandle texture;
    glm::vec2 size;
    glm::vec2 uvMin = {0.0f, 0.0f};
    glm::vec2 uvMax = {1.0f, 1.0f};
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    i16 layer     = 0;
    i16 sortOrder = 0;
    bool flipX    = false;
    bool flipY    = false;
};

// --- SpriteAnimation component ---
// Drives atlas-based sprite animation. Entities with both Sprite and
// SpriteAnimation will have their uvMin/uvMax updated each frame by
// animationUpdateSystem based on the current frame index and grid layout.
// POD. No heap. No pointers.
struct SpriteAnimation {
    u16 frameCount   = 1;       // total frames in the atlas
    u16 columns      = 1;       // atlas grid columns
    u16 currentFrame = 0;       // current frame index (0-based)
    u16 _pad         = 0;       // explicit padding for alignment
    f32 frameTime    = 0.1f;    // seconds per frame
    f32 elapsed      = 0.0f;    // time accumulator
    bool looping     = true;
    bool playing     = false;
};

// --- Tilemap component ---
// Grid of tile indices rendered as a batch. Tile index 0 = empty (not rendered).
// Indices 1+ map to frames in the tileset atlas (1-based for the atlas grid).
// The entity's Transform positions the top-left corner of the tilemap in world space.
// Tile data is heap-allocated once at creation time (cold path).
// POD except for the tile data pointer. Must be freed via destroyTilemap().
struct Tilemap {
    u16* tiles     = nullptr;   // width * height tile indices (heap-allocated)
    u16 width      = 0;         // Grid width in tiles
    u16 height     = 0;         // Grid height in tiles
    f32 tileWidth  = 16.0f;     // Tile size in world units
    f32 tileHeight = 16.0f;
    rhi::TextureHandle texture; // Tileset atlas texture
    u16 columns    = 1;         // Columns in the tileset atlas
    u16 tileCount  = 1;         // Total tiles in the tileset (for row calculation)
    i16 layer      = 0;         // Render layer
    i16 sortOrder  = 0;
};

// Allocate tile data for a tilemap. Initialises all tiles to 0 (empty).
inline bool initTilemap(Tilemap& tm, const u16 w, const u16 h) {
    if (w == 0 || h == 0 || w > 1024 || h > 1024) return false;
    tm.tiles = new(std::nothrow) u16[static_cast<u32>(w) * h]();
    if (tm.tiles == nullptr) return false;
    tm.width  = w;
    tm.height = h;
    return true;
}

// Free tile data. Safe to call multiple times or on uninitialised tilemap.
inline void destroyTilemap(Tilemap& tm) {
    delete[] tm.tiles;
    tm.tiles  = nullptr;
    tm.width  = 0;
    tm.height = 0;
}

// --- Particle system ---
// Engine-side particle emitter. Each emitter owns a fixed-size pool of particles
// (no heap allocation — particles are stored inline). The emitter tracks emission
// rate, lifetime, velocity spread, color range, and size range.
//
// Particles are updated by particleUpdateSystem (registered as a system).
// Particles are rendered by renderParticles (called from Application::render,
// bypassing the render queue like tilemaps).
//
// Up to MAX_PARTICLES per emitter. When the pool is full, new emissions are
// silently dropped (no allocation, no crash).

inline constexpr u32 MAX_PARTICLES = 128;

struct Particle {
    glm::vec2 position  = {0.0f, 0.0f};
    glm::vec2 velocity  = {0.0f, 0.0f};
    f32 lifetime        = 0.0f;  // Total lifetime (for alpha fade)
    f32 remaining       = 0.0f;  // Time remaining
    f32 size            = 4.0f;
    f32 sizeEnd         = 0.0f;  // Size at end of life (lerp)
    glm::vec4 color     = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colorEnd  = {1.0f, 1.0f, 1.0f, 0.0f};
};

struct ParticleEmitter {
    // --- Particle pool (inline, no heap) ---
    Particle particles[MAX_PARTICLES] = {};
    u32 activeCount = 0;

    // --- Emission config ---
    f32 emitRate     = 10.0f;   // Particles per second
    f32 emitAccum    = 0.0f;    // Accumulator for fractional emission
    bool emitting    = false;

    // --- Particle config (randomised per-particle within [min, max]) ---
    f32 lifetimeMin  = 0.5f;
    f32 lifetimeMax  = 1.5f;
    f32 speedMin     = 20.0f;
    f32 speedMax     = 80.0f;
    f32 angleMin     = 0.0f;    // Radians (0 = right, pi/2 = up)
    f32 angleMax     = 6.28318f; // Full circle by default (2*PI)
    f32 sizeStart    = 4.0f;
    f32 sizeEnd      = 0.0f;
    f32 gravityY     = 0.0f;    // Applied to velocity.y each tick

    // --- Color (start and end, linearly interpolated over lifetime) ---
    glm::vec4 colorStart = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 colorEnd   = {1.0f, 1.0f, 1.0f, 0.0f};

    // --- Rendering ---
    rhi::TextureHandle texture; // Use default white texture if invalid
    i16 layer     = 0;
    i16 sortOrder = 0;

    // --- Emitter offset from entity Transform ---
    glm::vec2 offset = {0.0f, 0.0f};

    // --- Burst mode ---
    u32 burstCount = 0;  // If > 0, emit this many instantly and stop
};

// Simple deterministic pseudo-random for particle emission.
// Uses a linear congruential generator. Seeded once, used only for particles.
// Returns a float in [0, 1).
inline f32 particleRandom(u32& seed) {
    seed = seed * 1103515245u + 12345u;
    return static_cast<f32>((seed >> 16) & 0x7FFF) / 32768.0f;
}

} // namespace ffe

namespace ffe::renderer {

// CopyTransformSystem — run at priority 5, before any gameplay system.
// Snapshots Transform into PreviousTransform for all entities that have both.
// Must run before gameplay systems modify Transform each tick.
void copyTransformSystem(World& world, float dt);

// Priority at which copyTransformSystem should be registered.
// Must be lower than any gameplay system priority (gameplay typically >= 100).
inline constexpr i32 COPY_TRANSFORM_PRIORITY = 5;

// AnimationUpdateSystem — run at priority 50, after CopyTransform (5) and
// before gameplay systems (>= 100). Advances SpriteAnimation timers and
// writes updated UV coordinates into the Sprite component.
void animationUpdateSystem(World& world, float dt);

// Priority at which animationUpdateSystem should be registered.
inline constexpr i32 ANIMATION_UPDATE_PRIORITY = 50;

// The render preparation system. Called explicitly from Application::render(),
// NOT registered in the system list. Accepts alpha (interpolation factor [0,1))
// and lerps between PreviousTransform and Transform when building DrawCommands.
void renderPrepareSystem(World& world, float alpha);

// Render all Tilemap components directly into the sprite batch.
// Called from Application::render() after the render queue sprites are drawn.
// Bypasses the render queue to avoid filling it with thousands of tile commands.
void renderTilemaps(World& world, SpriteBatch& batch);

// ParticleUpdateSystem — run at priority 55, after animation (50) and before
// gameplay systems (>= 100). Updates all ParticleEmitter components: emits new
// particles, applies velocity + gravity, kills expired particles.
void particleUpdateSystem(World& world, float dt);

// Priority at which particleUpdateSystem should be registered.
inline constexpr i32 PARTICLE_UPDATE_PRIORITY = 55;

// Render all active particles directly into the sprite batch.
// Called from Application::render() after tilemaps. Bypasses the render queue.
void renderParticles(World& world, SpriteBatch& batch, rhi::TextureHandle defaultTexture);

} // namespace ffe::renderer
