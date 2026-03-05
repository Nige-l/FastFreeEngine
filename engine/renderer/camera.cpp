#include "renderer/camera.h"
#include <glm/gtc/matrix_transform.hpp>

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
    }
    return glm::ortho(
        cam.orthoLeft, cam.orthoRight,
        cam.orthoBottom, cam.orthoTop,
        cam.nearPlane, cam.farPlane
    );
}

glm::mat4 computeViewProjectionMatrix(const Camera& cam) {
    return computeProjectionMatrix(cam) * computeViewMatrix(cam);
}

} // namespace ffe::renderer
