#include <catch2/catch_test_macros.hpp>
#include "core/ecs.h"
#include "renderer/render_system.h"

// ---------------------------------------------------------------------------
// Particle system tests — ECS component + update system
// ---------------------------------------------------------------------------

TEST_CASE("ParticleEmitter default state", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    world.addComponent<ffe::Transform>(e);
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);

    REQUIRE(em.activeCount == 0);
    REQUIRE(em.emitting == false);
    REQUIRE(em.emitRate > 0.0f);
    REQUIRE(em.burstCount == 0);
}

TEST_CASE("Continuous emission spawns particles", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    auto& tf = world.addComponent<ffe::Transform>(e);
    tf.position = {100.0f, 200.0f, 0.0f};
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.emitting = true;
    em.emitRate = 100.0f; // 100 particles/sec
    em.lifetimeMin = 1.0f;
    em.lifetimeMax = 1.0f;
    em.speedMin = 50.0f;
    em.speedMax = 50.0f;

    // After 0.1s at 100/sec, expect ~10 particles
    ffe::renderer::particleUpdateSystem(world, 0.1f);
    REQUIRE(em.activeCount == 10);
}

TEST_CASE("Burst emission creates particles and stops", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    world.addComponent<ffe::Transform>(e);
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.burstCount = 20;
    em.lifetimeMin = 2.0f;
    em.lifetimeMax = 2.0f;

    ffe::renderer::particleUpdateSystem(world, 0.016f);
    REQUIRE(em.activeCount == 20);
    REQUIRE(em.burstCount == 0);
    REQUIRE(em.emitting == false); // Burst sets emitting to false
}

TEST_CASE("Particles die when lifetime expires", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    world.addComponent<ffe::Transform>(e);
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.burstCount = 5;
    em.lifetimeMin = 0.1f;
    em.lifetimeMax = 0.1f;

    // Spawn particles
    ffe::renderer::particleUpdateSystem(world, 0.016f);
    REQUIRE(em.activeCount == 5);

    // Advance past lifetime — all should die
    ffe::renderer::particleUpdateSystem(world, 0.2f);
    REQUIRE(em.activeCount == 0);
}

TEST_CASE("Gravity affects particle velocity", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    world.addComponent<ffe::Transform>(e);
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.burstCount = 1;
    em.lifetimeMin = 10.0f;
    em.lifetimeMax = 10.0f;
    em.speedMin = 0.0f;
    em.speedMax = 0.0f;
    em.gravityY = -100.0f;

    // Spawn one particle at zero velocity
    ffe::renderer::particleUpdateSystem(world, 0.016f);
    REQUIRE(em.activeCount == 1);

    const float initialY = em.particles[0].position.y;

    // Update again — gravity should pull it down
    ffe::renderer::particleUpdateSystem(world, 0.1f);
    REQUIRE(em.particles[0].velocity.y < 0.0f);
    REQUIRE(em.particles[0].position.y < initialY);
}

TEST_CASE("MAX_PARTICLES caps pool size", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    world.addComponent<ffe::Transform>(e);
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.burstCount = ffe::MAX_PARTICLES + 100; // Try to exceed max
    em.lifetimeMin = 10.0f;
    em.lifetimeMax = 10.0f;

    ffe::renderer::particleUpdateSystem(world, 0.016f);
    REQUIRE(em.activeCount == ffe::MAX_PARTICLES);
}

TEST_CASE("Emitter offset shifts particle spawn position", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    auto& tf = world.addComponent<ffe::Transform>(e);
    tf.position = {0.0f, 0.0f, 0.0f};
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.offset = {50.0f, 30.0f};
    em.burstCount = 1;
    em.lifetimeMin = 10.0f;
    em.lifetimeMax = 10.0f;
    em.speedMin = 0.0f;
    em.speedMax = 0.0f;

    ffe::renderer::particleUpdateSystem(world, 0.016f);
    REQUIRE(em.activeCount == 1);
    REQUIRE(em.particles[0].position.x == 50.0f);
    REQUIRE(em.particles[0].position.y == 30.0f);
}

TEST_CASE("particleRandom produces values in [0, 1)", "[particles]") {
    ffe::u32 seed = 12345;
    for (int i = 0; i < 1000; ++i) {
        const float val = ffe::particleRandom(seed);
        REQUIRE(val >= 0.0f);
        REQUIRE(val < 1.0f);
    }
}

TEST_CASE("No emitter on entity without Transform", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    // Add emitter without transform — system should handle gracefully
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.emitting = true;
    em.emitRate = 100.0f;

    // particleUpdateSystem requires Transform — entity without it is skipped
    // (the view filters on <const Transform, ParticleEmitter>)
    ffe::renderer::particleUpdateSystem(world, 0.1f);
    REQUIRE(em.activeCount == 0);
}

TEST_CASE("Stopped emitter does not spawn new particles", "[particles]") {
    ffe::World world;
    const ffe::EntityId e = world.createEntity();
    world.addComponent<ffe::Transform>(e);
    auto& em = world.addComponent<ffe::ParticleEmitter>(e);
    em.emitting = false;
    em.emitRate = 100.0f;

    ffe::renderer::particleUpdateSystem(world, 1.0f);
    REQUIRE(em.activeCount == 0);
}
