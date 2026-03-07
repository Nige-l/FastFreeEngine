// test_frustum.cpp -- Catch2 unit tests for view frustum extraction and AABB culling.
//
// Tests verify frustum plane extraction from VP matrices and AABB visibility testing.
// Pure CPU math -- no GL context required.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "renderer/frustum.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace ffe::renderer;

// -----------------------------------------------------------------------
// extractFrustum
// -----------------------------------------------------------------------

TEST_CASE("extractFrustum produces 6 planes", "[frustum]") {
    const glm::mat4 vp = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f)
                        * glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const Frustum f = extractFrustum(vp);

    // Just verify we have 6 non-zero planes
    for (int i = 0; i < 6; ++i) {
        const float len = std::sqrt(f.planes[i].x * f.planes[i].x +
                                    f.planes[i].y * f.planes[i].y +
                                    f.planes[i].z * f.planes[i].z);
        CHECK(len > 0.0f);
    }
}

TEST_CASE("Frustum planes are normalized", "[frustum]") {
    const glm::mat4 vp = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f)
                        * glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const Frustum f = extractFrustum(vp);

    for (int i = 0; i < 6; ++i) {
        const float len = std::sqrt(f.planes[i].x * f.planes[i].x +
                                    f.planes[i].y * f.planes[i].y +
                                    f.planes[i].z * f.planes[i].z);
        CHECK_THAT(len, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    }
}

// -----------------------------------------------------------------------
// isAABBVisible
// -----------------------------------------------------------------------

TEST_CASE("isAABBVisible: AABB at origin visible with identity VP", "[frustum]") {
    // Identity VP creates a clip volume from -1 to 1 in all axes.
    const Frustum f = extractFrustum(glm::mat4(1.0f));
    CHECK(isAABBVisible(f, glm::vec3(-0.5f), glm::vec3(0.5f)));
}

TEST_CASE("isAABBVisible: AABB far behind camera is culled", "[frustum]") {
    // Camera at (0,0,5) looking at origin, near=0.1, far=100
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Frustum f = extractFrustum(proj * view);

    // AABB far behind the camera (z > 5 + some margin, in world space it's behind camera)
    CHECK_FALSE(isAABBVisible(f, glm::vec3(-1.0f, -1.0f, 10.0f), glm::vec3(1.0f, 1.0f, 20.0f)));
}

TEST_CASE("isAABBVisible: AABB to the far left is culled", "[frustum]") {
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Frustum f = extractFrustum(proj * view);

    // AABB far to the left, outside the FOV
    CHECK_FALSE(isAABBVisible(f, glm::vec3(-500.0f, -1.0f, -5.0f), glm::vec3(-400.0f, 1.0f, 5.0f)));
}

TEST_CASE("isAABBVisible: AABB partially inside is visible", "[frustum]") {
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Frustum f = extractFrustum(proj * view);

    // AABB straddles the near plane area — partially in front of camera
    CHECK(isAABBVisible(f, glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f)));
}

TEST_CASE("isAABBVisible: large AABB containing camera is visible", "[frustum]") {
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Frustum f = extractFrustum(proj * view);

    // Very large AABB surrounding the camera
    CHECK(isAABBVisible(f, glm::vec3(-1000.0f), glm::vec3(1000.0f)));
}

TEST_CASE("isAABBVisible: AABB beyond far plane is culled", "[frustum]") {
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Frustum f = extractFrustum(proj * view);

    // AABB far in front of camera, beyond the 100-unit far plane
    // Camera is at z=5 looking toward -z, so objects at z < 5-100 = -95 are beyond far plane
    CHECK_FALSE(isAABBVisible(f, glm::vec3(-1.0f, -1.0f, -200.0f), glm::vec3(1.0f, 1.0f, -150.0f)));
}
