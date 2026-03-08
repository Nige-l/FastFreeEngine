// test_prefab_system.cpp -- Catch2 unit tests for the PrefabSystem (Phase 10 M1).
//
// CPU-only tests. No GL context required. All tests operate on the prefab pool
// and ECS World without touching any GPU resource.
//
// Fixture files live under tests/core/fixtures/ (relative to the project root).
// The FIXTURE_DIR macro resolves the path at compile time using __FILE__, so
// tests find their fixtures regardless of the working directory from which
// ctest is invoked.
//
// Covered by these tests (matches ADR Section 2.11 test plan):
//
//   Handle validity               — tests 1, 2
//   PrefabOverrides struct        — tests 3, 4, 5, 6, 19, 20, 21
//   loadPrefab (success)          — tests 7, 11
//   loadPrefab (error paths)      — tests 8, 9, 10
//   getPrefabCount                — tests 12, 13, 14
//   instantiatePrefab             — test 15
//   unloadPrefab                  — test 16
//   Duplicate load                — test 17
//   Struct sizing                 — test 18

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/prefab_system.h"

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// FIXTURE_DIR — absolute path to tests/core/fixtures/, derived from __FILE__
// so it works regardless of the ctest working directory.
//
// __FILE__ expands to the absolute path of this source file because CMake
// compiles with full paths. We strip the filename part to get the directory,
// then append "fixtures/".
// ---------------------------------------------------------------------------

static std::string fixtureDir() {
    // __FILE__ is e.g. "/home/nigel/FastFreeEngine/tests/core/test_prefab_system.cpp"
    std::string path = __FILE__;
    const auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        path = path.substr(0, slash + 1); // keep trailing slash
    }
    return path + "fixtures/";
}

static std::string fixture(const char* name) {
    return fixtureDir() + name;
}

// ---------------------------------------------------------------------------
// PrefabHandle validity
// ---------------------------------------------------------------------------

TEST_CASE("PrefabHandle{0} is invalid", "[prefab][handle]") {
    constexpr ffe::PrefabHandle h{0};
    REQUIRE_FALSE(ffe::isValid(h));
    REQUIRE_FALSE(static_cast<bool>(h));
}

TEST_CASE("PrefabHandle{1} is valid", "[prefab][handle]") {
    constexpr ffe::PrefabHandle h{1};
    REQUIRE(ffe::isValid(h));
    REQUIRE(static_cast<bool>(h));
}

// ---------------------------------------------------------------------------
// PrefabOverrides struct
// ---------------------------------------------------------------------------

TEST_CASE("PrefabOverrides::MAX equals 8", "[prefab][overrides]") {
    static_assert(ffe::PrefabOverrides::MAX == 8,
        "PrefabOverrides::MAX must be 8 per ADR spec");
    REQUIRE(ffe::PrefabOverrides::MAX == 8);
}

TEST_CASE("PrefabOverrides starts with count == 0", "[prefab][overrides]") {
    ffe::PrefabOverrides ov;
    REQUIRE(ov.count == 0);
}

TEST_CASE("PrefabOverrides::set(float) increments count", "[prefab][overrides]") {
    ffe::PrefabOverrides ov;
    ov.set("Transform3D", "x", 1.0f);
    REQUIRE(ov.count == 1);
    ov.set("Transform3D", "y", 2.0f);
    REQUIRE(ov.count == 2);
}

TEST_CASE("PrefabOverrides::set at MAX capacity: count stays at 8, no crash", "[prefab][overrides]") {
    ffe::PrefabOverrides ov;

    // Fill to capacity
    for (int i = 0; i < ffe::PrefabOverrides::MAX; ++i) {
        ov.set("Transform3D", "x", static_cast<float>(i));
    }
    REQUIRE(ov.count == ffe::PrefabOverrides::MAX);

    // One more — must not crash, count must not change
    ov.set("Transform3D", "x", 99.0f);
    REQUIRE(ov.count == ffe::PrefabOverrides::MAX);
}

TEST_CASE("PrefabOverrides::set float sets type Float", "[prefab][overrides]") {
    ffe::PrefabOverrides ov;
    ov.set("Transform3D", "x", 3.14f);
    REQUIRE(ov.count == 1);
    REQUIRE(ov.items[0].type == ffe::PrefabOverride::Type::Float);
    REQUIRE_THAT(ov.items[0].value.f, Catch::Matchers::WithinAbs(3.14f, 0.0001f));
}

TEST_CASE("PrefabOverrides::set int sets type Int", "[prefab][overrides]") {
    ffe::PrefabOverrides ov;
    ov.set("SomeComponent", "someField", 42);
    REQUIRE(ov.count == 1);
    REQUIRE(ov.items[0].type == ffe::PrefabOverride::Type::Int);
    REQUIRE(ov.items[0].value.i == 42);
}

TEST_CASE("PrefabOverrides::set bool sets type Bool", "[prefab][overrides]") {
    ffe::PrefabOverrides ov;
    ov.set("SomeComponent", "enabled", true);
    REQUIRE(ov.count == 1);
    REQUIRE(ov.items[0].type == ffe::PrefabOverride::Type::Bool);
    REQUIRE(ov.items[0].value.b == true);
}

// ---------------------------------------------------------------------------
// sizeof guard — PrefabOverride must not have grown unexpectedly
// ---------------------------------------------------------------------------

TEST_CASE("sizeof(PrefabOverride) is not unexpectedly large", "[prefab][sizing]") {
    // 32 + 32 + union(4) + uint8_t type + padding = at most 128 bytes
    REQUIRE(sizeof(ffe::PrefabOverride) <= 128);
}

// ---------------------------------------------------------------------------
// getPrefabCount — initial state
// ---------------------------------------------------------------------------

TEST_CASE("getPrefabCount is 0 initially", "[prefab][count]") {
    ffe::PrefabSystem ps;
    REQUIRE(ps.getPrefabCount() == 0);
}

// ---------------------------------------------------------------------------
// loadPrefab — success paths
// ---------------------------------------------------------------------------

TEST_CASE("Load valid prefab returns a valid handle", "[prefab][load]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    const ffe::PrefabHandle h = ps.loadPrefab(fixture("tree_prefab.json"));
    REQUIRE(ffe::isValid(h));
}

TEST_CASE("getPrefabCount is 1 after loading one prefab", "[prefab][count]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    ps.loadPrefab(fixture("tree_prefab.json"));
    REQUIRE(ps.getPrefabCount() == 1);
}

TEST_CASE("getPrefabCount is 0 after unloading", "[prefab][count]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    const ffe::PrefabHandle h = ps.loadPrefab(fixture("tree_prefab.json"));
    REQUIRE(ps.getPrefabCount() == 1);

    ps.unloadPrefab(h);
    REQUIRE(ps.getPrefabCount() == 0);
}

TEST_CASE("Load same file twice returns two distinct valid handles", "[prefab][load]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    const ffe::PrefabHandle h1 = ps.loadPrefab(fixture("tree_prefab.json"));
    const ffe::PrefabHandle h2 = ps.loadPrefab(fixture("tree_prefab.json"));

    REQUIRE(ffe::isValid(h1));
    REQUIRE(ffe::isValid(h2));
    REQUIRE(h1.id != h2.id);
    REQUIRE(ps.getPrefabCount() == 2);
}

TEST_CASE("Load prefab with unknown component: returns valid handle, unknown component skipped", "[prefab][load][forward_compat]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    const ffe::PrefabHandle h = ps.loadPrefab(fixture("unknown_component_prefab.json"));
    REQUIRE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// loadPrefab — error paths
// ---------------------------------------------------------------------------

TEST_CASE("Load nonexistent file returns PrefabHandle{0}", "[prefab][load][error]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    const ffe::PrefabHandle h = ps.loadPrefab(fixture("does_not_exist.json"));
    REQUIRE_FALSE(ffe::isValid(h));
}

TEST_CASE("Load malformed JSON returns PrefabHandle{0}, no crash", "[prefab][load][error]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    const ffe::PrefabHandle h = ps.loadPrefab(fixture("malformed_prefab.json"));
    REQUIRE_FALSE(ffe::isValid(h));
}

TEST_CASE("Load path traversal attempt returns PrefabHandle{0}", "[prefab][load][security]") {
    ffe::PrefabSystem ps;
    ps.setAssetRoot(fixtureDir());

    // Attempt to read a file outside the asset root via ../..
    const ffe::PrefabHandle h = ps.loadPrefab("../../etc/passwd");
    REQUIRE_FALSE(ffe::isValid(h));
}

// ---------------------------------------------------------------------------
// unloadPrefab — robustness
// ---------------------------------------------------------------------------

TEST_CASE("Unload invalid handle is a no-op, no crash", "[prefab][unload]") {
    ffe::PrefabSystem ps;
    // Must not throw, crash, or corrupt state
    ps.unloadPrefab(ffe::PrefabHandle{0});
    REQUIRE(ps.getPrefabCount() == 0);
}

// ---------------------------------------------------------------------------
// instantiatePrefab — invalid handle
// ---------------------------------------------------------------------------

TEST_CASE("instantiatePrefab with invalid handle returns NULL_ENTITY, no crash", "[prefab][instantiate]") {
    ffe::PrefabSystem ps;
    ffe::World world;

    const ffe::EntityId eid = ps.instantiatePrefab(world, ffe::PrefabHandle{0});
    REQUIRE(eid == ffe::NULL_ENTITY);
}

