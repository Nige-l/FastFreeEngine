#include <catch2/catch_test_macros.hpp>

#include "scene/scene_graph.h"
#include "renderer/render_system.h"

using namespace ffe;
using namespace ffe::scene;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static World makeWorld() {
    World w;
    return w;
}

static entt::entity createEntity(World& w) {
    return static_cast<entt::entity>(w.createEntity());
}

// ---------------------------------------------------------------------------
// setParent / getParent round-trip
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: setParent/getParent round-trip", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity parent = createEntity(w);
    const entt::entity child  = createEntity(w);

    REQUIRE(setParent(w, child, parent));
    REQUIRE(getParent(w, child) == parent);
}

// ---------------------------------------------------------------------------
// removeParent makes entity a root
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: removeParent makes entity root", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity parent = createEntity(w);
    const entt::entity child  = createEntity(w);

    REQUIRE(setParent(w, child, parent));
    REQUIRE_FALSE(isRoot(w, child));

    removeParent(w, child);
    REQUIRE(isRoot(w, child));
    REQUIRE((getParent(w, child) == entt::null));
}

// ---------------------------------------------------------------------------
// setParent replaces previous parent
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: setParent replaces previous parent", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity parentA = createEntity(w);
    const entt::entity parentB = createEntity(w);
    const entt::entity child   = createEntity(w);

    REQUIRE(setParent(w, child, parentA));
    REQUIRE(getParent(w, child) == parentA);

    // Re-parent to parentB
    REQUIRE(setParent(w, child, parentB));
    REQUIRE(getParent(w, child) == parentB);

    // child should no longer be in parentA's children
    uint32_t countA = 0;
    getChildren(w, parentA, countA);
    REQUIRE(countA == 0);

    // child should be in parentB's children
    uint32_t countB = 0;
    const entt::entity* childrenB = getChildren(w, parentB, countB);
    REQUIRE(countB == 1);
    REQUIRE(childrenB[0] == child);
}

// ---------------------------------------------------------------------------
// Circular parenting prevention
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: circular parenting prevention", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity a = createEntity(w);
    const entt::entity b = createEntity(w);
    const entt::entity c = createEntity(w);

    // Build chain: A -> B -> C
    REQUIRE(setParent(w, b, a));
    REQUIRE(setParent(w, c, b));

    // Attempting C -> A should fail (A is ancestor of C)
    REQUIRE_FALSE(setParent(w, a, c));

    // Verify the chain is unchanged
    REQUIRE(getParent(w, b) == a);
    REQUIRE(getParent(w, c) == b);
    REQUIRE(isRoot(w, a));

    SECTION("Self-parenting is rejected") {
        REQUIRE_FALSE(setParent(w, a, a));
    }

    SECTION("Direct cycle A->B, B->A is rejected") {
        // B is already a child of A
        REQUIRE_FALSE(setParent(w, a, b));
    }
}

// ---------------------------------------------------------------------------
// getRootEntities returns only unparented entities
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: getRootEntities", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity a = createEntity(w);
    const entt::entity b = createEntity(w);
    const entt::entity c = createEntity(w);
    (void)b; // b exists in the world to affect root count; suppress unused warning

    // All three are roots initially
    entt::entity roots[8] = {};
    uint32_t count = getRootEntities(w, roots, 8);
    REQUIRE(count == 3);

    // Parent c under a -> only a and b are roots
    REQUIRE(setParent(w, c, a));
    count = getRootEntities(w, roots, 8);
    REQUIRE(count == 2);

    // Verify neither returned entity is c
    bool foundC = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (roots[i] == c) { foundC = true; }
    }
    REQUIRE_FALSE(foundC);
}

// ---------------------------------------------------------------------------
// isAncestor
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: isAncestor", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity a = createEntity(w);
    const entt::entity b = createEntity(w);
    const entt::entity c = createEntity(w);
    const entt::entity d = createEntity(w);

    // Chain: A -> B -> C
    REQUIRE(setParent(w, b, a));
    REQUIRE(setParent(w, c, b));

    REQUIRE(isAncestor(w, c, a));  // A is ancestor of C
    REQUIRE(isAncestor(w, c, b));  // B is ancestor of C
    REQUIRE(isAncestor(w, b, a));  // A is ancestor of B

    REQUIRE_FALSE(isAncestor(w, a, c)); // C is NOT ancestor of A
    REQUIRE_FALSE(isAncestor(w, a, b)); // B is NOT ancestor of A
    REQUIRE_FALSE(isAncestor(w, a, a)); // Entity is NOT its own ancestor
    REQUIRE_FALSE(isAncestor(w, c, d)); // D is unrelated
}

// ---------------------------------------------------------------------------
// Children component is cleaned up when empty
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: Children component cleaned up when empty", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity parent = createEntity(w);
    const entt::entity child  = createEntity(w);

    // Parent should not have Children component initially
    REQUIRE_FALSE(w.registry().all_of<Children>(parent));

    // After setParent, parent gets Children component
    REQUIRE(setParent(w, child, parent));
    REQUIRE(w.registry().all_of<Children>(parent));

    // After removeParent, parent's Children component should be removed
    removeParent(w, child);
    REQUIRE_FALSE(w.registry().all_of<Children>(parent));
}

// ---------------------------------------------------------------------------
// getChildren returns nullptr for entity with no children
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: getChildren returns nullptr for childless entity", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity e = createEntity(w);

    uint32_t count = 99;
    const entt::entity* result = getChildren(w, e, count);
    REQUIRE(result == nullptr);
    REQUIRE(count == 0);
}

// ---------------------------------------------------------------------------
// Multiple children
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: multiple children", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity parent = createEntity(w);
    const entt::entity c1 = createEntity(w);
    const entt::entity c2 = createEntity(w);
    const entt::entity c3 = createEntity(w);

    REQUIRE(setParent(w, c1, parent));
    REQUIRE(setParent(w, c2, parent));
    REQUIRE(setParent(w, c3, parent));

    uint32_t count = 0;
    const entt::entity* children = getChildren(w, parent, count);
    REQUIRE(count == 3);
    REQUIRE(children != nullptr);

    // All three children should be present (order may vary due to swap-remove)
    bool found1 = false, found2 = false, found3 = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (children[i] == c1) { found1 = true; }
        if (children[i] == c2) { found2 = true; }
        if (children[i] == c3) { found3 = true; }
    }
    REQUIRE(found1);
    REQUIRE(found2);
    REQUIRE(found3);
}

// ---------------------------------------------------------------------------
// removeParent is a no-op on root entity
// ---------------------------------------------------------------------------

TEST_CASE("Scene graph: removeParent no-op on root", "[scene][scene_graph]") {
    World w = makeWorld();
    const entt::entity e = createEntity(w);

    // Should not crash or change anything
    removeParent(w, e);
    REQUIRE(isRoot(w, e));
}
