#pragma once

#include "core/types.h"
#include <glm/glm.hpp>

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

    // Perspective params
    f32 fovDegrees = 60.0f;
    f32 nearPlane  = 0.1f;
    f32 farPlane   = 100.0f;

    // Orthographic params — viewport-sized in pixels, origin at center
    f32 orthoLeft   = -640.0f;
    f32 orthoRight  =  640.0f;
    f32 orthoBottom = -360.0f;
    f32 orthoTop    =  360.0f;

    // Viewport dimensions
    f32 viewportWidth  = 1280.0f;
    f32 viewportHeight = 720.0f;
};

// Compute the view matrix
glm::mat4 computeViewMatrix(const Camera& cam);

// Compute the projection matrix
glm::mat4 computeProjectionMatrix(const Camera& cam);

// Combined view * projection
glm::mat4 computeViewProjectionMatrix(const Camera& cam);

} // namespace ffe::renderer
