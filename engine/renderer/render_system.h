#pragma once

#include "core/types.h"
#include "core/system.h"
#include "renderer/rhi_types.h"
#include "renderer/render_queue.h"
#include "renderer/sprite_batch.h"

#include <glm/glm.hpp>

namespace ffe {

// --- Transform component (needed by render system) ---
// Defined here because the render system needs it and it is not yet in core.
struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
    f32 rotation       = 0.0f; // Radians, z-axis rotation for 2D
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
};

} // namespace ffe

namespace ffe::renderer {

// The render preparation system. Registered as a SystemUpdateFn at priority 500.
void renderPrepareSystem(World& world, float dt);

// Priority constant for registration.
inline constexpr i32 RENDER_PREPARE_PRIORITY = 500;

} // namespace ffe::renderer
