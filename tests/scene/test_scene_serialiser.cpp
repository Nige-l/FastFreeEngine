#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "scene/scene_serialiser.h"
#include "renderer/render_system.h"

#include <cstring>
#include <string>

using namespace ffe;
using namespace ffe::scene;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static World makeWorld() {
    World w;
    return w;
}

// ---------------------------------------------------------------------------
// Round-trip: Transform (2D)
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: round-trip Transform", "[scene]") {
    World src = makeWorld();

    const EntityId e = src.createEntity();
    auto& t = src.addComponent<Transform>(e);
    t.position = {10.0f, 20.0f, 0.0f};
    t.rotation = 1.5f;
    t.scale    = {2.0f, 3.0f, 1.0f};

    const std::string json = serialiseToJson(src);
    REQUIRE(!json.empty());

    World dst = makeWorld();
    REQUIRE(deserialiseFromJson(dst, json));

    // Find the entity with a Transform
    uint32_t count = 0;
    for (auto [entity, tr] : dst.view<Transform>().each()) {
        REQUIRE_THAT(tr.position.x, Catch::Matchers::WithinAbs(10.0f, 0.001f));
        REQUIRE_THAT(tr.position.y, Catch::Matchers::WithinAbs(20.0f, 0.001f));
        REQUIRE_THAT(tr.rotation,   Catch::Matchers::WithinAbs(1.5f,  0.001f));
        REQUIRE_THAT(tr.scale.x,    Catch::Matchers::WithinAbs(2.0f,  0.001f));
        REQUIRE_THAT(tr.scale.y,    Catch::Matchers::WithinAbs(3.0f,  0.001f));
        ++count;
    }
    REQUIRE(count == 1);
}

// ---------------------------------------------------------------------------
// Round-trip: Transform3D
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: round-trip Transform3D", "[scene]") {
    World src = makeWorld();

    const EntityId e = src.createEntity();
    auto& t = src.addComponent<Transform3D>(e);
    t.position = {1.0f, 2.0f, 3.0f};
    t.rotation = glm::quat{0.707f, 0.0f, 0.707f, 0.0f};
    t.scale    = {0.5f, 0.5f, 0.5f};

    const std::string json = serialiseToJson(src);
    REQUIRE(!json.empty());

    World dst = makeWorld();
    REQUIRE(deserialiseFromJson(dst, json));

    uint32_t count = 0;
    for (auto [entity, tr] : dst.view<Transform3D>().each()) {
        REQUIRE_THAT(tr.position.x, Catch::Matchers::WithinAbs(1.0f, 0.001f));
        REQUIRE_THAT(tr.position.y, Catch::Matchers::WithinAbs(2.0f, 0.001f));
        REQUIRE_THAT(tr.position.z, Catch::Matchers::WithinAbs(3.0f, 0.001f));
        REQUIRE_THAT(tr.rotation.w, Catch::Matchers::WithinAbs(0.707f, 0.001f));
        REQUIRE_THAT(tr.rotation.x, Catch::Matchers::WithinAbs(0.0f,   0.001f));
        REQUIRE_THAT(tr.rotation.y, Catch::Matchers::WithinAbs(0.707f, 0.001f));
        REQUIRE_THAT(tr.rotation.z, Catch::Matchers::WithinAbs(0.0f,   0.001f));
        REQUIRE_THAT(tr.scale.x,    Catch::Matchers::WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(tr.scale.y,    Catch::Matchers::WithinAbs(0.5f, 0.001f));
        REQUIRE_THAT(tr.scale.z,    Catch::Matchers::WithinAbs(0.5f, 0.001f));
        ++count;
    }
    REQUIRE(count == 1);
}

// ---------------------------------------------------------------------------
// Round-trip: Name component
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: round-trip Name", "[scene]") {
    World src = makeWorld();

    const EntityId e = src.createEntity();
    auto& name = src.addComponent<Name>(e);
    std::strncpy(name.name, "Player", sizeof(name.name) - 1);
    name.name[sizeof(name.name) - 1] = '\0';

    const std::string json = serialiseToJson(src);
    REQUIRE(!json.empty());

    World dst = makeWorld();
    REQUIRE(deserialiseFromJson(dst, json));

    uint32_t count = 0;
    for (auto [entity, n] : dst.view<Name>().each()) {
        REQUIRE(std::string(n.name) == "Player");
        ++count;
    }
    REQUIRE(count == 1);
}

// ---------------------------------------------------------------------------
// Round-trip: Parent/Children hierarchy
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: round-trip Parent/Children hierarchy", "[scene]") {
    World src = makeWorld();

    const EntityId parent = src.createEntity();
    auto& parentName = src.addComponent<Name>(parent);
    std::strncpy(parentName.name, "Root", sizeof(parentName.name) - 1);

    const EntityId child = src.createEntity();
    auto& childName = src.addComponent<Name>(child);
    std::strncpy(childName.name, "Child", sizeof(childName.name) - 1);

    // Set up hierarchy
    auto& p = src.addComponent<Parent>(child);
    p.parent = static_cast<entt::entity>(parent);

    auto& ch = src.addComponent<Children>(parent);
    ch.children[0] = static_cast<entt::entity>(child);
    ch.count = 1;

    const std::string json = serialiseToJson(src);
    REQUIRE(!json.empty());

    World dst = makeWorld();
    REQUIRE(deserialiseFromJson(dst, json));

    // Verify we have exactly one entity with Parent and one with Children
    uint32_t parentCount = 0;
    uint32_t childrenCount = 0;
    for (auto [entity, par] : dst.view<Parent>().each()) {
        REQUIRE((par.parent != entt::null));
        // Verify the parent entity has the Name "Root"
        REQUIRE(dst.registry().all_of<Name>(par.parent));
        REQUIRE(std::string(dst.registry().get<Name>(par.parent).name) == "Root");
        ++parentCount;
    }
    for (auto [entity, kids] : dst.view<Children>().each()) {
        REQUIRE(kids.count == 1);
        REQUIRE((kids.children[0] != entt::null));
        // Verify the child entity has the Name "Child"
        REQUIRE(dst.registry().all_of<Name>(kids.children[0]));
        REQUIRE(std::string(dst.registry().get<Name>(kids.children[0]).name) == "Child");
        ++childrenCount;
    }
    REQUIRE(parentCount == 1);
    REQUIRE(childrenCount == 1);
}

// ---------------------------------------------------------------------------
// Empty scene
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: empty scene round-trip", "[scene]") {
    World src = makeWorld();

    const std::string json = serialiseToJson(src);
    REQUIRE(!json.empty());

    World dst = makeWorld();
    REQUIRE(deserialiseFromJson(dst, json));

    // No entities should exist
    uint32_t count = 0;
    for (auto [entity] : dst.registry().storage<entt::entity>().each()) {
        if (dst.registry().valid(entity)) {
            ++count;
        }
    }
    REQUIRE(count == 0);
}

// ---------------------------------------------------------------------------
// Version validation
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: wrong version rejected", "[scene]") {
    const std::string badJson = R"({"version": 999, "entities": []})";

    World dst = makeWorld();
    REQUIRE_FALSE(deserialiseFromJson(dst, badJson));
}

TEST_CASE("Scene serialiser: missing version rejected", "[scene]") {
    const std::string badJson = R"({"entities": []})";

    World dst = makeWorld();
    REQUIRE_FALSE(deserialiseFromJson(dst, badJson));
}

// ---------------------------------------------------------------------------
// Entity count limit
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: entity count limit enforced", "[scene]") {
    // Build JSON with 10001 entities (exceeds MAX_SERIALISED_ENTITIES = 10000)
    std::string bigJson = R"({"version": 1, "entities": [)";
    for (uint32_t i = 0; i < 10001; ++i) {
        if (i > 0) bigJson += ",";
        bigJson += R"({"components":{}})";
    }
    bigJson += "]}";

    World dst = makeWorld();
    REQUIRE_FALSE(deserialiseFromJson(dst, bigJson));
}

// ---------------------------------------------------------------------------
// NaN rejection
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: NaN float values rejected", "[scene]") {
    // nlohmann-json represents NaN as null when dumped. We inject a non-finite
    // value via a string that uses a very large exponent to produce infinity,
    // which the deserialiser must reject.
    // Note: JSON spec does not allow NaN/Inf literals, so we test with values
    // that our validator catches.
    const std::string nanJson = R"({
        "version": 1,
        "entities": [{
            "components": {
                "Transform": {
                    "x": 1e+999,
                    "y": 0.0,
                    "z": 0.0,
                    "rotation": 0.0,
                    "scaleX": 1.0,
                    "scaleY": 1.0
                }
            }
        }]
    })";

    World dst = makeWorld();
    // The JSON parser may represent 1e+999 as infinity. Our validator must reject it.
    // If the parser itself rejects it, the deserialise will also fail — either way, success.
    REQUIRE_FALSE(deserialiseFromJson(dst, nanJson));
}

// ---------------------------------------------------------------------------
// Multiple entities with mixed components
// ---------------------------------------------------------------------------

TEST_CASE("Scene serialiser: multiple entities mixed components", "[scene]") {
    World src = makeWorld();

    // Entity 1: Transform only
    const EntityId e1 = src.createEntity();
    auto& t1 = src.addComponent<Transform>(e1);
    t1.position = {100.0f, 200.0f, 0.0f};

    // Entity 2: Transform3D + Name
    const EntityId e2 = src.createEntity();
    auto& t2 = src.addComponent<Transform3D>(e2);
    t2.position = {5.0f, 10.0f, 15.0f};
    auto& n2 = src.addComponent<Name>(e2);
    std::strncpy(n2.name, "Cube", sizeof(n2.name) - 1);

    // Entity 3: Name only
    const EntityId e3 = src.createEntity();
    auto& n3 = src.addComponent<Name>(e3);
    std::strncpy(n3.name, "Empty", sizeof(n3.name) - 1);
    (void)e3;

    const std::string json = serialiseToJson(src);
    REQUIRE(!json.empty());

    World dst = makeWorld();
    REQUIRE(deserialiseFromJson(dst, json));

    // Count entities by component
    uint32_t transformCount = 0;
    uint32_t transform3dCount = 0;
    uint32_t nameCount = 0;
    for (auto [e, t] : dst.view<Transform>().each()) {
        (void)e; (void)t;
        ++transformCount;
    }
    for (auto [e, t] : dst.view<Transform3D>().each()) {
        (void)e; (void)t;
        ++transform3dCount;
    }
    for (auto [e, n] : dst.view<Name>().each()) {
        (void)e; (void)n;
        ++nameCount;
    }
    REQUIRE(transformCount == 1);
    REQUIRE(transform3dCount == 1);
    REQUIRE(nameCount == 2); // "Cube" and "Empty"
}
