// test_prediction.cpp — Catch2 unit tests for client-side prediction
// and server reconciliation.
//
// Tests run in the ffe_tests_networking executable.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "networking/prediction.h"
#include "core/ecs.h"
#include "renderer/render_system.h"

using namespace ffe::networking;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// Test movement function: adds inputBits as velocity * dt to position.x
// This is simple enough to verify prediction and reconciliation.
// ---------------------------------------------------------------------------
static void testMoveFn(ffe::World& world, const uint32_t entityId,
                       const InputCommand& cmd, void* /*userData*/) {
    const auto eid = static_cast<ffe::EntityId>(entityId);
    if (!world.isValid(eid)) { return; }
    if (!world.hasComponent<ffe::Transform>(eid)) { return; }

    auto& t = world.getComponent<ffe::Transform>(eid);
    // Move right by inputBits units per second
    t.position.x += static_cast<float>(cmd.inputBits) * cmd.dt;
}

// ===========================================================================
// PredictionBuffer tests
// ===========================================================================

TEST_CASE("PredictionBuffer: record and retrieve by tick",
          "[networking][prediction]") {
    PredictionBuffer buf;

    InputCommand cmd;
    cmd.tickNumber = 42;
    cmd.inputBits  = 0xFF;
    cmd.dt         = 0.016f;
    buf.record(cmd);

    const InputCommand* result = buf.get(42);
    REQUIRE(result != nullptr);
    CHECK(result->tickNumber == 42);
    CHECK(result->inputBits == 0xFF);
    CHECK_THAT(result->dt, WithinAbs(0.016f, 0.0001f));
}

TEST_CASE("PredictionBuffer: circular wraparound",
          "[networking][prediction]") {
    PredictionBuffer buf;

    // Fill the entire buffer and then some
    for (uint32_t i = 0; i < MAX_PREDICTION_BUFFER + 10; ++i) {
        InputCommand cmd;
        cmd.tickNumber = i + 1;
        cmd.inputBits  = i;
        buf.record(cmd);
    }

    // Count should be capped at MAX_PREDICTION_BUFFER
    CHECK(buf.count() == MAX_PREDICTION_BUFFER);

    // Oldest entries should have been overwritten
    CHECK(buf.get(1) == nullptr);
    CHECK(buf.get(10) == nullptr);

    // Recent entries should still be accessible
    const uint32_t lastTick = MAX_PREDICTION_BUFFER + 10;
    const InputCommand* result = buf.get(lastTick);
    REQUIRE(result != nullptr);
    CHECK(result->tickNumber == lastTick);
    CHECK(result->inputBits == lastTick - 1);
}

TEST_CASE("PredictionBuffer: discardBefore removes old entries",
          "[networking][prediction]") {
    PredictionBuffer buf;

    for (uint32_t i = 1; i <= 10; ++i) {
        InputCommand cmd;
        cmd.tickNumber = i;
        buf.record(cmd);
    }

    CHECK(buf.count() == 10);

    buf.discardBefore(6); // Discard ticks 1-5

    CHECK(buf.count() == 5);
    CHECK(buf.get(5) == nullptr); // Tick 5 was discarded
    CHECK(buf.get(6) != nullptr); // Tick 6 still present
    CHECK(buf.get(10) != nullptr);
}

TEST_CASE("PredictionBuffer: get returns nullptr for missing tick",
          "[networking][prediction]") {
    PredictionBuffer buf;

    InputCommand cmd;
    cmd.tickNumber = 10;
    buf.record(cmd);

    CHECK(buf.get(10) != nullptr);
    CHECK(buf.get(11) == nullptr);
    CHECK(buf.get(0) == nullptr);
    CHECK(buf.get(9) == nullptr);
}

TEST_CASE("PredictionBuffer: count tracks correctly",
          "[networking][prediction]") {
    PredictionBuffer buf;

    CHECK(buf.count() == 0);

    InputCommand cmd;
    cmd.tickNumber = 1;
    buf.record(cmd);
    CHECK(buf.count() == 1);

    cmd.tickNumber = 2;
    buf.record(cmd);
    CHECK(buf.count() == 2);

    cmd.tickNumber = 3;
    buf.record(cmd);
    CHECK(buf.count() == 3);
}

TEST_CASE("PredictionBuffer: clear resets",
          "[networking][prediction]") {
    PredictionBuffer buf;

    for (uint32_t i = 1; i <= 5; ++i) {
        InputCommand cmd;
        cmd.tickNumber = i;
        buf.record(cmd);
    }

    CHECK(buf.count() == 5);
    buf.clear();
    CHECK(buf.count() == 0);
    CHECK(buf.get(1) == nullptr);
    CHECK(buf.get(5) == nullptr);
}

// ===========================================================================
// ClientPrediction tests
// ===========================================================================

TEST_CASE("ClientPrediction: recordAndPredict increments tick and calls moveFn",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    auto& t = world.addComponent<ffe::Transform>(entity);
    t.position.x = 0.0f;

    ClientPrediction pred;
    pred.setMovementFunction(testMoveFn, nullptr);
    pred.setLocalEntity(static_cast<uint32_t>(entity));

    CHECK(pred.currentTick() == 0);

    InputCommand cmd;
    cmd.inputBits = 10;  // 10 units/sec
    cmd.dt        = 0.1f; // 0.1 sec => move 1.0

    pred.recordAndPredict(world, cmd);

    CHECK(pred.currentTick() == 1);
    CHECK_THAT(t.position.x, WithinAbs(1.0f, 0.001f));

    // Second tick
    pred.recordAndPredict(world, cmd);
    CHECK(pred.currentTick() == 2);
    CHECK_THAT(t.position.x, WithinAbs(2.0f, 0.001f));
}

TEST_CASE("ClientPrediction: reconcile with matching state returns 0 error",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    auto& t = world.addComponent<ffe::Transform>(entity);
    t.position.x = 5.0f;
    t.position.y = 3.0f;
    t.position.z = 1.0f;

    ClientPrediction pred;
    pred.setMovementFunction(testMoveFn, nullptr);
    pred.setLocalEntity(static_cast<uint32_t>(entity));

    // Server says we're at exactly where we are
    const float error = pred.reconcile(world, 0, 5.0f, 3.0f, 1.0f);
    CHECK_THAT(error, WithinAbs(0.0f, 0.001f));

    // Position should be unchanged
    CHECK_THAT(t.position.x, WithinAbs(5.0f, 0.001f));
    CHECK_THAT(t.position.y, WithinAbs(3.0f, 0.001f));
    CHECK_THAT(t.position.z, WithinAbs(1.0f, 0.001f));
}

TEST_CASE("ClientPrediction: reconcile with mismatched state snaps and returns error",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    auto& t = world.addComponent<ffe::Transform>(entity);
    t.position.x = 10.0f;
    t.position.y = 0.0f;
    t.position.z = 0.0f;

    ClientPrediction pred;
    pred.setMovementFunction(testMoveFn, nullptr);
    pred.setLocalEntity(static_cast<uint32_t>(entity));

    // Server says we're at 5.0 -- large discrepancy
    const float error = pred.reconcile(world, 0, 5.0f, 0.0f, 0.0f);
    CHECK(error > 0.1f); // Above threshold
    CHECK_THAT(error, WithinAbs(5.0f, 0.001f));

    // Position should have been snapped to server state
    CHECK_THAT(t.position.x, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("ClientPrediction: reconcile replays buffered inputs after correction",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    auto& t = world.addComponent<ffe::Transform>(entity);
    t.position.x = 0.0f;

    ClientPrediction pred;
    pred.setMovementFunction(testMoveFn, nullptr);
    pred.setLocalEntity(static_cast<uint32_t>(entity));

    // Record 5 ticks of movement: each moves 1.0 (10 units/sec * 0.1s)
    InputCommand cmd;
    cmd.inputBits = 10;
    cmd.dt        = 0.1f;

    for (int i = 0; i < 5; ++i) {
        pred.recordAndPredict(world, cmd);
    }
    CHECK_THAT(t.position.x, WithinAbs(5.0f, 0.001f));
    CHECK(pred.currentTick() == 5);

    // Server says at tick 2, we were at x=1.5 (not 2.0 — slight discrepancy)
    // After snap to 1.5, replay ticks 3,4,5 => 1.5 + 3.0 = 4.5
    const float error = pred.reconcile(world, 2, 1.5f, 0.0f, 0.0f);

    CHECK(error > 0.1f); // Error was large enough to trigger correction
    // After replay: 1.5 + 3*1.0 = 4.5
    CHECK_THAT(t.position.x, WithinAbs(4.5f, 0.001f));
}

TEST_CASE("ClientPrediction: setLocalEntity / localEntity accessor",
          "[networking][prediction]") {
    ClientPrediction pred;

    // Default value
    CHECK(pred.localEntity() == 0xFFFFFFFF);

    pred.setLocalEntity(42);
    CHECK(pred.localEntity() == 42);

    pred.setLocalEntity(0);
    CHECK(pred.localEntity() == 0);
}

TEST_CASE("ClientPrediction: lastError returns last reconciliation error",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    auto& t = world.addComponent<ffe::Transform>(entity);
    t.position.x = 10.0f;

    ClientPrediction pred;
    pred.setMovementFunction(testMoveFn, nullptr);
    pred.setLocalEntity(static_cast<uint32_t>(entity));

    // Initially zero
    CHECK_THAT(pred.lastError(), WithinAbs(0.0f, 0.001f));

    // After reconcile with error
    pred.reconcile(world, 0, 5.0f, 0.0f, 0.0f);
    CHECK_THAT(pred.lastError(), WithinAbs(5.0f, 0.001f));

    // After reconcile with small error (below threshold)
    t.position.x = 5.0f;
    pred.reconcile(world, 0, 5.05f, 0.0f, 0.0f);
    CHECK(pred.lastError() < 0.11f); // Below threshold
}

TEST_CASE("ClientPrediction: reconcile below threshold does not snap",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    auto& t = world.addComponent<ffe::Transform>(entity);
    t.position.x = 5.0f;
    t.position.y = 0.0f;
    t.position.z = 0.0f;

    ClientPrediction pred;
    pred.setMovementFunction(testMoveFn, nullptr);
    pred.setLocalEntity(static_cast<uint32_t>(entity));

    // Server says 5.05 — within threshold of 0.1
    const float error = pred.reconcile(world, 0, 5.05f, 0.0f, 0.0f);

    CHECK(error <= 0.1f);
    // Position should NOT have been snapped (stays at 5.0, not 5.05)
    CHECK_THAT(t.position.x, WithinAbs(5.0f, 0.001f));
}

TEST_CASE("ClientPrediction: no moveFn set — recordAndPredict does not crash",
          "[networking][prediction]") {
    ffe::World world;
    const auto entity = world.createEntity();
    world.addComponent<ffe::Transform>(entity);

    ClientPrediction pred;
    pred.setLocalEntity(static_cast<uint32_t>(entity));
    // Do NOT set a movement function

    InputCommand cmd;
    cmd.inputBits = 10;
    cmd.dt        = 0.1f;

    // Should not crash
    pred.recordAndPredict(world, cmd);
    CHECK(pred.currentTick() == 1);
}

TEST_CASE("InputCommand: default construction",
          "[networking][prediction]") {
    InputCommand cmd;
    CHECK(cmd.tickNumber == 0);
    CHECK(cmd.inputBits == 0);
    CHECK_THAT(cmd.dt, WithinAbs(0.0f, 0.0001f));
    CHECK_THAT(cmd.aimX, WithinAbs(0.0f, 0.0001f));
    CHECK_THAT(cmd.aimY, WithinAbs(0.0f, 0.0001f));
}
