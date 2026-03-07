// frustum.cpp -- View frustum extraction and AABB visibility testing for FFE.
//
// Pure CPU math. No GPU calls. No allocations. O(1) per AABB test.
//
// extractFrustum(): Griess-Hartmann method extracts 6 planes from VP matrix rows.
// isAABBVisible(): "positive vertex" method tests AABB against all 6 planes.

#include "renderer/frustum.h"

#include <cmath>

namespace ffe::renderer {

Frustum extractFrustum(const glm::mat4& vp) {
    Frustum f;

    // Griess-Hartmann: extract planes from rows of the VP matrix.
    // GLM stores matrices column-major, so vp[col][row].
    // Row i of the matrix is: vp[0][i], vp[1][i], vp[2][i], vp[3][i]

    // Left:   row3 + row0
    f.planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );

    // Right:  row3 - row0
    f.planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );

    // Bottom: row3 + row1
    f.planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );

    // Top:    row3 - row1
    f.planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );

    // Near:   row3 + row2
    f.planes[4] = glm::vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );

    // Far:    row3 - row2
    f.planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize all planes so distance tests are correct.
    for (auto& plane : f.planes) {
        const float len = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        if (len > 1e-8f) {
            plane /= len;
        }
    }

    return f;
}

bool isAABBVisible(const Frustum& frustum, const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    // For each frustum plane, find the "positive vertex" (p-vertex) of the AABB —
    // the vertex most in the direction of the plane normal. If this vertex is
    // behind the plane (negative side), the entire AABB is outside the frustum.
    for (const auto& plane : frustum.planes) {
        // Select the p-vertex: for each axis, pick max if normal component is positive,
        // min otherwise.
        const float px = (plane.x >= 0.0f) ? aabbMax.x : aabbMin.x;
        const float py = (plane.y >= 0.0f) ? aabbMax.y : aabbMin.y;
        const float pz = (plane.z >= 0.0f) ? aabbMax.z : aabbMin.z;

        // Signed distance from the p-vertex to the plane
        const float dist = plane.x * px + plane.y * py + plane.z * pz + plane.w;

        // If the p-vertex is behind the plane, the entire AABB is outside
        if (dist < 0.0f) {
            return false;
        }
    }

    return true;
}

} // namespace ffe::renderer
