// test_lag_compensation.cpp — Catch2 unit tests for server-side lag compensation.
//
// Tests run in the ffe_tests_networking executable.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "networking/lag_compensation.h"

#include <cmath>
#include <limits>

using namespace ffe::networking;
using Catch::Matchers::WithinAbs;

// ===========================================================================
// Helper: create a single active entity state
// ===========================================================================
static EntityState makeEntity(const uint32_t id, const float x, const float y,
                              const float z, const float radius = 0.5f) {
    EntityState es;
    es.entityId = id;
    es.x        = x;
    es.y        = y;
    es.z        = z;
    es.radius   = radius;
    es.active   = true;
    return es;
}

// ===========================================================================
// 1. HistoryFrame stores entity positions
// ===========================================================================

TEST_CASE("HistoryFrame stores entity positions",
          "[networking][lag_compensation]") {
    HistoryFrame frame;
    frame.tick        = 10;
    frame.entityCount = 1;
    frame.entities[0] = makeEntity(42, 1.0f, 2.0f, 3.0f);

    CHECK(frame.tick == 10);
    CHECK(frame.entityCount == 1);
    CHECK(frame.entities[0].entityId == 42);
    CHECK_THAT(frame.entities[0].x, WithinAbs(1.0f, 0.001f));
    CHECK_THAT(frame.entities[0].y, WithinAbs(2.0f, 0.001f));
    CHECK_THAT(frame.entities[0].z, WithinAbs(3.0f, 0.001f));
    CHECK(frame.entities[0].active);
}

// ===========================================================================
// 2. LagCompensator recordFrame and getFrame roundtrip
// ===========================================================================

TEST_CASE("LagCompensator recordFrame and getFrame roundtrip",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState entities[2] = {
        makeEntity(1, 10.0f, 20.0f, 30.0f),
        makeEntity(2, 40.0f, 50.0f, 60.0f)
    };

    lc.recordFrame(5, entities, 2);

    const HistoryFrame* frame = lc.getFrame(5);
    REQUIRE(frame != nullptr);
    CHECK(frame->tick == 5);
    CHECK(frame->entityCount == 2);
    CHECK(frame->entities[0].entityId == 1);
    CHECK_THAT(frame->entities[0].x, WithinAbs(10.0f, 0.001f));
    CHECK(frame->entities[1].entityId == 2);
    CHECK_THAT(frame->entities[1].x, WithinAbs(40.0f, 0.001f));
}

// ===========================================================================
// 3. LagCompensator circular buffer wraparound
// ===========================================================================

TEST_CASE("LagCompensator circular buffer wraparound",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 0.0f, 0.0f, 0.0f);

    // Fill buffer beyond MAX_HISTORY_TICKS
    for (uint32_t i = 1; i <= MAX_HISTORY_TICKS + 10; ++i) {
        ent.x = static_cast<float>(i);
        lc.recordFrame(i, &ent, 1);
    }

    // frameCount capped at MAX_HISTORY_TICKS
    CHECK(lc.frameCount() == MAX_HISTORY_TICKS);

    // Oldest entries should be gone
    CHECK(lc.getFrame(1) == nullptr);
    CHECK(lc.getFrame(10) == nullptr);

    // Recent entries should still exist
    const uint32_t lastTick = MAX_HISTORY_TICKS + 10;
    const HistoryFrame* frame = lc.getFrame(lastTick);
    REQUIRE(frame != nullptr);
    CHECK_THAT(frame->entities[0].x, WithinAbs(static_cast<float>(lastTick), 0.001f));
}

// ===========================================================================
// 4. LagCompensator getFrame returns nullptr for missing tick
// ===========================================================================

TEST_CASE("LagCompensator getFrame returns nullptr for missing tick",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 0.0f, 0.0f, 0.0f);
    lc.recordFrame(10, &ent, 1);

    CHECK(lc.getFrame(10) != nullptr);
    CHECK(lc.getFrame(11) == nullptr);
    CHECK(lc.getFrame(9) == nullptr);
    CHECK(lc.getFrame(0) == nullptr);
}

// ===========================================================================
// 5. LagCompensator clear resets
// ===========================================================================

TEST_CASE("LagCompensator clear resets",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 5.0f, 0.0f, 0.0f);
    lc.recordFrame(1, &ent, 1);
    lc.recordFrame(2, &ent, 1);
    lc.recordFrame(3, &ent, 1);

    CHECK(lc.frameCount() == 3);

    lc.clear();

    CHECK(lc.frameCount() == 0);
    CHECK(lc.getFrame(1) == nullptr);
    CHECK(lc.getFrame(2) == nullptr);
    CHECK(lc.getFrame(3) == nullptr);
}

// ===========================================================================
// 6. performHitCheck hits a sphere at known position
// ===========================================================================

TEST_CASE("performHitCheck hits a sphere at known position",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    // Place entity at (5, 0, 0) with radius 1.0
    EntityState ent = makeEntity(1, 5.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(1, &ent, 1);

    // Shoot ray from origin along +X
    const HitCheckResult result = lc.performHitCheck(
        1,
        0.0f, 0.0f, 0.0f,  // origin
        1.0f, 0.0f, 0.0f,  // direction
        100.0f,             // maxDistance
        0xFFFFFFFF);        // ignoreEntityId (none)

    CHECK(result.hit);
    CHECK(result.hitEntityId == 1);
    // Hit point should be at x=4 (sphere surface: center 5 minus radius 1)
    CHECK_THAT(result.hitX, WithinAbs(4.0f, 0.01f));
    CHECK_THAT(result.hitY, WithinAbs(0.0f, 0.01f));
    CHECK_THAT(result.hitZ, WithinAbs(0.0f, 0.01f));
    CHECK_THAT(result.distance, WithinAbs(4.0f, 0.01f));
}

// ===========================================================================
// 7. performHitCheck misses when ray aimed away
// ===========================================================================

TEST_CASE("performHitCheck misses when ray aimed away",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 5.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(1, &ent, 1);

    // Shoot ray along -X (away from entity)
    const HitCheckResult result = lc.performHitCheck(
        1,
        0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,
        100.0f,
        0xFFFFFFFF);

    CHECK_FALSE(result.hit);
}

// ===========================================================================
// 8. performHitCheck ignores specified entity
// ===========================================================================

TEST_CASE("performHitCheck ignores specified entity",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState entities[2] = {
        makeEntity(1, 5.0f, 0.0f, 0.0f, 1.0f),
        makeEntity(2, 10.0f, 0.0f, 0.0f, 1.0f)
    };
    lc.recordFrame(1, entities, 2);

    // Shoot along +X, ignoring entity 1 (closer)
    const HitCheckResult result = lc.performHitCheck(
        1,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        100.0f,
        1); // ignore entity 1

    CHECK(result.hit);
    CHECK(result.hitEntityId == 2);
    CHECK_THAT(result.hitX, WithinAbs(9.0f, 0.01f)); // sphere at 10 - radius 1
}

// ===========================================================================
// 9. performHitCheck with maxDistance too short returns miss
// ===========================================================================

TEST_CASE("performHitCheck with maxDistance too short returns miss",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 50.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(1, &ent, 1);

    // Max distance 10 but entity is at 50
    const HitCheckResult result = lc.performHitCheck(
        1,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        10.0f,
        0xFFFFFFFF);

    CHECK_FALSE(result.hit);
}

// ===========================================================================
// 10. performHitCheck at rewound tick uses historical positions
// ===========================================================================

TEST_CASE("performHitCheck at rewound tick uses historical positions",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    // Tick 1: entity at x=5
    EntityState ent1 = makeEntity(1, 5.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(1, &ent1, 1);

    // Tick 2: entity moved to x=10
    EntityState ent2 = makeEntity(1, 10.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(2, &ent2, 1);

    // Hit check at tick 1 should find entity at x=5
    const HitCheckResult r1 = lc.performHitCheck(
        1,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        100.0f,
        0xFFFFFFFF);

    CHECK(r1.hit);
    CHECK_THAT(r1.hitX, WithinAbs(4.0f, 0.01f)); // sphere at 5 - radius 1

    // Hit check at tick 2 should find entity at x=10
    const HitCheckResult r2 = lc.performHitCheck(
        2,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        100.0f,
        0xFFFFFFFF);

    CHECK(r2.hit);
    CHECK_THAT(r2.hitX, WithinAbs(9.0f, 0.01f)); // sphere at 10 - radius 1
}

// ===========================================================================
// 11. LagCompensator rejects NaN origin in hit check
// ===========================================================================

TEST_CASE("LagCompensator rejects NaN origin in hit check",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 5.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(1, &ent, 1);

    const float nan = std::numeric_limits<float>::quiet_NaN();

    const HitCheckResult result = lc.performHitCheck(
        1,
        nan, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        100.0f,
        0xFFFFFFFF);

    CHECK_FALSE(result.hit);
}

// ===========================================================================
// 12. LagCompensator rejects NaN direction in hit check
// ===========================================================================

TEST_CASE("LagCompensator rejects NaN direction in hit check",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    EntityState ent = makeEntity(1, 5.0f, 0.0f, 0.0f, 1.0f);
    lc.recordFrame(1, &ent, 1);

    const float nan = std::numeric_limits<float>::quiet_NaN();

    const HitCheckResult result = lc.performHitCheck(
        1,
        0.0f, 0.0f, 0.0f,
        nan, 0.0f, 0.0f,
        100.0f,
        0xFFFFFFFF);

    CHECK_FALSE(result.hit);
}

// ===========================================================================
// 13. setMaxRewindTicks clamps to MAX_HISTORY_TICKS
// ===========================================================================

TEST_CASE("setMaxRewindTicks clamps to MAX_HISTORY_TICKS",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    lc.setMaxRewindTicks(1000);
    CHECK(lc.maxRewindTicks() == MAX_HISTORY_TICKS);

    lc.setMaxRewindTicks(32);
    CHECK(lc.maxRewindTicks() == 32);

    lc.setMaxRewindTicks(0);
    CHECK(lc.maxRewindTicks() == 0);
}

// ===========================================================================
// 14. oldestTick/newestTick track correctly
// ===========================================================================

TEST_CASE("oldestTick and newestTick track correctly",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    // Empty: both return 0
    CHECK(lc.oldestTick() == 0);
    CHECK(lc.newestTick() == 0);

    EntityState ent = makeEntity(1, 0.0f, 0.0f, 0.0f);

    lc.recordFrame(10, &ent, 1);
    CHECK(lc.oldestTick() == 10);
    CHECK(lc.newestTick() == 10);

    lc.recordFrame(20, &ent, 1);
    CHECK(lc.oldestTick() == 10);
    CHECK(lc.newestTick() == 20);

    lc.recordFrame(30, &ent, 1);
    CHECK(lc.oldestTick() == 10);
    CHECK(lc.newestTick() == 30);
}

// ===========================================================================
// 15. frameCount tracks correctly
// ===========================================================================

TEST_CASE("frameCount tracks correctly",
          "[networking][lag_compensation]") {
    LagCompensator lc;

    CHECK(lc.frameCount() == 0);

    EntityState ent = makeEntity(1, 0.0f, 0.0f, 0.0f);

    lc.recordFrame(1, &ent, 1);
    CHECK(lc.frameCount() == 1);

    lc.recordFrame(2, &ent, 1);
    CHECK(lc.frameCount() == 2);

    lc.recordFrame(3, &ent, 1);
    CHECK(lc.frameCount() == 3);

    // Fill to max
    for (uint32_t i = 4; i <= MAX_HISTORY_TICKS + 5; ++i) {
        lc.recordFrame(i, &ent, 1);
    }
    CHECK(lc.frameCount() == MAX_HISTORY_TICKS);
}
