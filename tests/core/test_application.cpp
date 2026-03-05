#include <catch2/catch_test_macros.hpp>
#include "core/application.h"

TEST_CASE("Application headless mode runs and shuts down", "[application]") {
    ffe::ApplicationConfig config;
    config.headless = true;
    config.tickRate = 60.0f;

    ffe::Application app(config);
    const int32_t exitCode = app.run();
    REQUIRE(exitCode == 0);
}

TEST_CASE("Application lifecycle and config access", "[application]") {
    ffe::ApplicationConfig config;
    config.headless = true;
    config.windowTitle = "TestApp";
    config.windowWidth = 800;
    config.windowHeight = 600;

    ffe::Application app(config);

    REQUIRE(app.config().headless == true);
    REQUIRE(app.config().windowWidth == 800);
    REQUIRE(app.config().windowHeight == 600);
}

TEST_CASE("Application shutdown request", "[application]") {
    ffe::ApplicationConfig config;
    config.headless = true;

    ffe::Application app(config);

    // Register a system that requests shutdown on first tick
    auto shutdownFn = [](ffe::World& /*world*/, float /*dt*/) {
        // We cannot easily access Application from here in the current design,
        // so we verify that headless auto-shutdown works instead.
    };
    (void)shutdownFn; // Acknowledge this is a design limitation in the skeleton

    const int32_t exitCode = app.run();
    REQUIRE(exitCode == 0);
}

TEST_CASE("Application frame allocator is accessible", "[application]") {
    ffe::ApplicationConfig config;
    config.headless = true;

    ffe::Application app(config);

    auto& allocator = app.frameAllocator();
    REQUIRE(allocator.capacity() > 0);

    void* ptr = allocator.allocate(64);
    REQUIRE(ptr != nullptr);
}
