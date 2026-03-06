#include "physics/collision_system.h"
#include "physics/collider2d.h"
#include "physics/narrow_phase.h"
#include "core/ecs.h"
#include "core/arena_allocator.h"
#include "core/logging.h"
#include "renderer/render_system.h" // Transform

#include <algorithm> // std::sort
#include <cmath>     // std::floor

namespace ffe {

// ---------------------------------------------------------------------------
// Spatial hash grid — arena-allocated, rebuilt every frame from scratch.
// ---------------------------------------------------------------------------

// Cell coordinate to bucket index hash.
// Prime multipliers reduce clustering for typical 2D game entity distributions.
static inline u32 hashCell(const i32 cx, const i32 cy, const u32 bucketMask) {
    // Cast to unsigned for well-defined overflow behaviour.
    const u32 ux = static_cast<u32>(cx);
    const u32 uy = static_cast<u32>(cy);
    return ((ux * 73856093u) ^ (uy * 19349663u)) & bucketMask;
}

// A single node in a spatial hash bucket (arena-allocated singly-linked list).
struct SpatialNode {
    EntityId entity;
    SpatialNode* next;
};

// Spatial hash grid. All pointers are arena-allocated.
struct SpatialHash {
    SpatialNode** buckets;  // Array of bucket head pointers (arena-allocated)
    u32 bucketCount;        // Always a power of two
    u32 bucketMask;         // bucketCount - 1
    f32 cellSize;           // World-space size of each grid cell
    f32 invCellSize;        // 1.0f / cellSize (precomputed)
};

// Initialise a spatial hash with the given arena. Returns false if arena is exhausted.
static bool initSpatialHash(SpatialHash& hash, ArenaAllocator& arena,
                            const u32 bucketCount, const f32 cellSize) {
    hash.bucketCount = bucketCount;
    hash.bucketMask  = bucketCount - 1;
    hash.cellSize    = cellSize;
    hash.invCellSize = 1.0f / cellSize;

    // Allocate bucket head pointer array (zeroed by allocateArray).
    hash.buckets = arena.allocateArray<SpatialNode*>(bucketCount);
    return hash.buckets != nullptr;
}

// Insert an entity into all cells its AABB overlaps.
static void insertEntity(SpatialHash& hash, ArenaAllocator& arena,
                         const EntityId entity,
                         const f32 posX, const f32 posY,
                         const f32 halfW, const f32 halfH) {
    // Compute cell range covered by the entity's world-space AABB.
    const i32 minCx = static_cast<i32>(std::floor((posX - halfW) * hash.invCellSize));
    const i32 maxCx = static_cast<i32>(std::floor((posX + halfW) * hash.invCellSize));
    const i32 minCy = static_cast<i32>(std::floor((posY - halfH) * hash.invCellSize));
    const i32 maxCy = static_cast<i32>(std::floor((posY + halfH) * hash.invCellSize));

    for (i32 cy = minCy; cy <= maxCy; ++cy) {
        for (i32 cx = minCx; cx <= maxCx; ++cx) {
            const u32 bucket = hashCell(cx, cy, hash.bucketMask);
            auto* node = arena.create<SpatialNode>();
            if (node == nullptr) {
                FFE_LOG_WARN("Physics", "Spatial hash arena exhausted during insert");
                return;
            }
            node->entity = entity;
            node->next   = hash.buckets[bucket];
            hash.buckets[bucket] = node;
        }
    }
}

// ---------------------------------------------------------------------------
// Candidate pair with canonical ordering (entityA < entityB).
// ---------------------------------------------------------------------------
struct CandidatePair {
    EntityId a;
    EntityId b;
};

// ---------------------------------------------------------------------------
// collisionSystem implementation
// ---------------------------------------------------------------------------

void collisionSystem(World& world, float /*dt*/) {
    // 1. Get arena allocator from ECS context.
    //    Application must emplace ArenaAllocator* before running systems.
    if (!world.registry().ctx().contains<ArenaAllocator*>()) {
        return; // No arena available — skip silently (headless/test scenarios).
    }
    auto* arena = world.registry().ctx().get<ArenaAllocator*>();
    if (arena == nullptr) {
        return;
    }

    // 2. Collect all entities with Transform + Collider2D.
    auto colliderView = world.view<Transform, Collider2D>();

    // Count entities to size the pair buffer.
    u32 entityCount = 0;
    for ([[maybe_unused]] auto entity : colliderView) {
        ++entityCount;
    }

    if (entityCount == 0) {
        // Ensure CollisionEventList is cleared even when no colliders exist.
        if (world.registry().ctx().contains<CollisionEventList>()) {
            auto& eventList = world.registry().ctx().get<CollisionEventList>();
            eventList.events = nullptr;
            eventList.count  = 0;
        }
        return;
    }

    // 3. Build spatial hash grid.
    static constexpr u32 BUCKET_COUNT = 1024u;
    static constexpr f32 CELL_SIZE    = 128.0f;

    SpatialHash hash{};
    if (!initSpatialHash(hash, *arena, BUCKET_COUNT, CELL_SIZE)) {
        FFE_LOG_WARN("Physics", "Failed to allocate spatial hash — skipping collision");
        return;
    }

    // Insert all collider entities into the spatial hash.
    // For circles, use radius as both halfW and halfH for cell coverage.
    for (auto entity : colliderView) {
        const auto& transform = colliderView.template get<Transform>(entity);
        const auto& collider  = colliderView.template get<Collider2D>(entity);

        const f32 posX = transform.position.x;
        const f32 posY = transform.position.y;

        f32 halfW = collider.halfWidth;
        f32 halfH = collider.halfHeight;
        if (collider.shape == ColliderShape::CIRCLE) {
            halfH = collider.halfWidth; // radius for both axes
        }

        insertEntity(hash, *arena, static_cast<EntityId>(entity), posX, posY, halfW, halfH);
    }

    // 4. Generate candidate pairs from overlapping grid cells.
    //    Worst case: O(n^2) pairs. Pre-allocate conservatively.
    //    For 1000 entities, max reasonable pairs ~ 50000.
    const u32 maxPairs = entityCount * 32u; // Heuristic upper bound
    auto* pairs = arena->allocateArray<CandidatePair>(maxPairs);
    if (pairs == nullptr) {
        FFE_LOG_WARN("Physics", "Failed to allocate pair buffer — skipping collision");
        return;
    }
    u32 pairCount = 0;

    // Iterate all buckets, generate pairs within each bucket.
    for (u32 b = 0; b < hash.bucketCount; ++b) {
        for (const SpatialNode* nodeA = hash.buckets[b]; nodeA != nullptr; nodeA = nodeA->next) {
            for (const SpatialNode* nodeB = nodeA->next; nodeB != nullptr; nodeB = nodeB->next) {
                if (nodeA->entity == nodeB->entity) {
                    continue; // Skip self-pairs
                }

                // Canonical ordering for deduplication.
                const EntityId a = nodeA->entity < nodeB->entity ? nodeA->entity : nodeB->entity;
                const EntityId bId = nodeA->entity < nodeB->entity ? nodeB->entity : nodeA->entity;

                if (pairCount < maxPairs) {
                    pairs[pairCount].a = a;
                    pairs[pairCount].b = bId;
                    ++pairCount;
                }
            }
        }
    }

    // 5. Deduplicate pairs: sort then remove adjacent duplicates.
    if (pairCount > 1) {
        std::sort(pairs, pairs + pairCount,
            [](const CandidatePair& x, const CandidatePair& y) {
                if (x.a != y.a) return x.a < y.a;
                return x.b < y.b;
            });

        u32 write = 1;
        for (u32 read = 1; read < pairCount; ++read) {
            if (pairs[read].a != pairs[read - 1].a ||
                pairs[read].b != pairs[read - 1].b) {
                pairs[write] = pairs[read];
                ++write;
            }
        }
        pairCount = write;
    }

    // 6. Filter by layer/mask and run narrow phase.
    //    Allocate collision event array (upper bound = pairCount).
    auto* events = arena->allocateArray<CollisionEvent>(pairCount > 0 ? pairCount : 1u);
    if (events == nullptr) {
        FFE_LOG_WARN("Physics", "Failed to allocate event buffer — skipping collision");
        return;
    }
    u32 eventCount = 0;

    for (u32 i = 0; i < pairCount; ++i) {
        const EntityId idA = pairs[i].a;
        const EntityId idB = pairs[i].b;

        // Safety: entities may have been destroyed between collection and here.
        if (!world.isValid(idA) || !world.isValid(idB)) {
            continue;
        }

        const auto& colA = world.getComponent<Collider2D>(idA);
        const auto& colB = world.getComponent<Collider2D>(idB);

        // Layer/mask bidirectional check.
        if ((colA.layer & colB.mask) == 0 || (colB.layer & colA.mask) == 0) {
            continue;
        }

        const auto& tA = world.getComponent<Transform>(idA);
        const auto& tB = world.getComponent<Transform>(idB);

        const f32 ax = tA.position.x;
        const f32 ay = tA.position.y;
        const f32 bx = tB.position.x;
        const f32 by = tB.position.y;

        bool overlapping = false;

        if (colA.shape == ColliderShape::AABB && colB.shape == ColliderShape::AABB) {
            overlapping = overlapAABB(ax, ay, colA.halfWidth, colA.halfHeight,
                                      bx, by, colB.halfWidth, colB.halfHeight);
        }
        else if (colA.shape == ColliderShape::CIRCLE && colB.shape == ColliderShape::CIRCLE) {
            overlapping = overlapCircle(ax, ay, colA.halfWidth,
                                        bx, by, colB.halfWidth);
        }
        else if (colA.shape == ColliderShape::AABB && colB.shape == ColliderShape::CIRCLE) {
            overlapping = overlapAABBCircle(ax, ay, colA.halfWidth, colA.halfHeight,
                                            bx, by, colB.halfWidth);
        }
        else { // colA is CIRCLE, colB is AABB
            overlapping = overlapAABBCircle(bx, by, colB.halfWidth, colB.halfHeight,
                                            ax, ay, colA.halfWidth);
        }

        if (overlapping) {
            events[eventCount].entityA = idA;
            events[eventCount].entityB = idB;
            ++eventCount;
        }
    }

    // 7. Write CollisionEventList to ECS context.
    if (!world.registry().ctx().contains<CollisionEventList>()) {
        world.registry().ctx().emplace<CollisionEventList>();
    }
    auto& eventList = world.registry().ctx().get<CollisionEventList>();
    eventList.events = events;
    eventList.count  = eventCount;
}

} // namespace ffe
