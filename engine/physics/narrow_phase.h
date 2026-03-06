#pragma once

#include "core/types.h"

#include <algorithm> // std::min, std::max

namespace ffe {

// ---------------------------------------------------------------------------
// Narrow-phase overlap tests for 2D collision detection.
// All functions are inline. No sqrt. Minimal branching.
// Positions are world-space centers (from Transform.position.x/y).
// ---------------------------------------------------------------------------

// AABB vs AABB overlap test.
// posA/posB: center positions. halfA/halfB: half-extents (width, height).
inline bool overlapAABB(
    const f32 posAx, const f32 posAy, const f32 halfAw, const f32 halfAh,
    const f32 posBx, const f32 posBy, const f32 halfBw, const f32 halfBh)
{
    // Separating axis test: overlap on both X and Y axes.
    const f32 dx = posAx - posBx;
    const f32 dy = posAy - posBy;
    // Use absolute difference vs sum of half-extents.
    // Branchless: the compiler will emit conditional moves or SIMD.
    const f32 adx = dx < 0.0f ? -dx : dx;
    const f32 ady = dy < 0.0f ? -dy : dy;
    return (adx < (halfAw + halfBw)) && (ady < (halfAh + halfBh));
}

// Circle vs Circle overlap test.
// posA/posB: center positions. radiusA/radiusB: circle radii.
// No sqrt — uses squared distance comparison.
inline bool overlapCircle(
    const f32 posAx, const f32 posAy, const f32 radiusA,
    const f32 posBx, const f32 posBy, const f32 radiusB)
{
    const f32 dx = posAx - posBx;
    const f32 dy = posAy - posBy;
    const f32 distSq = dx * dx + dy * dy;
    const f32 radiusSum = radiusA + radiusB;
    return distSq < radiusSum * radiusSum;
}

// AABB vs Circle overlap test.
// aabbPos: AABB center. aabbHalf: AABB half-extents.
// circlePos: circle center. circleRadius: circle radius.
// Finds closest point on AABB to circle center, then distance check.
inline bool overlapAABBCircle(
    const f32 aabbPosX, const f32 aabbPosY, const f32 aabbHalfW, const f32 aabbHalfH,
    const f32 circlePosX, const f32 circlePosY, const f32 circleRadius)
{
    // Clamp circle center to AABB bounds to find closest point.
    const f32 aabbMinX = aabbPosX - aabbHalfW;
    const f32 aabbMaxX = aabbPosX + aabbHalfW;
    const f32 aabbMinY = aabbPosY - aabbHalfH;
    const f32 aabbMaxY = aabbPosY + aabbHalfH;

    const f32 closestX = std::max(aabbMinX, std::min(circlePosX, aabbMaxX));
    const f32 closestY = std::max(aabbMinY, std::min(circlePosY, aabbMaxY));

    const f32 dx = circlePosX - closestX;
    const f32 dy = circlePosY - closestY;
    return (dx * dx + dy * dy) < circleRadius * circleRadius;
}

} // namespace ffe
