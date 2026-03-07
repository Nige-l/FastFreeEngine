#pragma once

// frustum.h -- View frustum extraction and AABB visibility testing for FFE.
//
// Pure CPU math utility. No GPU calls. No allocations.
// Used by terrain_renderer.cpp for frustum culling of terrain chunks.
//
// Frustum planes are extracted from the view-projection matrix using the
// Griess-Hartmann method (extract from VP matrix rows). AABB testing uses
// the "positive vertex" (p-vertex) method for efficient rejection.
//
// Tier support: all tiers (CPU-only math).

#include <glm/glm.hpp>

namespace ffe::renderer {

// Six frustum planes: left, right, bottom, top, near, far.
// Each plane is stored as vec4(a, b, c, d) where ax + by + cz + d = 0.
// The normal (a, b, c) points inward (into the frustum).
struct Frustum {
    glm::vec4 planes[6]; // left, right, bottom, top, near, far
};

// Extract frustum planes from a combined view-projection matrix.
// Planes are normalized (normal has unit length) for correct distance tests.
Frustum extractFrustum(const glm::mat4& viewProjection);

// Test if an axis-aligned bounding box is inside or intersects the frustum.
// Returns true if the AABB is at least partially visible (not fully behind any plane).
// Returns false if the AABB is fully outside any single frustum plane.
// Uses the "positive vertex" method: for each plane, test the AABB vertex
// most in the direction of the plane normal. If that vertex is behind the plane,
// the entire AABB is outside.
bool isAABBVisible(const Frustum& frustum, const glm::vec3& aabbMin, const glm::vec3& aabbMax);

} // namespace ffe::renderer
