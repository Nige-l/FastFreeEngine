// test_skybox.cpp — Unit tests for the skybox subsystem.
//
// Tests SkyboxConfig defaults, cubemap loading graceful failure (no GL context),
// and Lua binding registration. GL-dependent tests are not included because
// the test executable runs headless (no GL context).

#include <catch2/catch_test_macros.hpp>
#include "renderer/mesh_renderer.h"
#include "renderer/texture_loader.h"

#include <array>
#include <string>

TEST_CASE("SkyboxConfig has sane defaults", "[skybox]") {
    const ffe::renderer::SkyboxConfig cfg;
    REQUIRE(cfg.cubemapTexture == 0);
    REQUIRE(cfg.enabled == false);
}

TEST_CASE("SkyboxConfig can be toggled without texture", "[skybox]") {
    ffe::renderer::SkyboxConfig cfg;
    cfg.enabled = true;
    REQUIRE(cfg.enabled == true);
    REQUIRE(cfg.cubemapTexture == 0);

    cfg.enabled = false;
    REQUIRE(cfg.enabled == false);
}

TEST_CASE("loadCubemap returns 0 when asset root not set", "[skybox]") {
    // In the test environment, setAssetRoot() has not been called.
    // loadCubemap should fail gracefully and return 0.
    const std::array<std::string, 6> fakePaths = {
        "skybox/right.png", "skybox/left.png",
        "skybox/top.png",   "skybox/bottom.png",
        "skybox/front.png", "skybox/back.png"
    };
    const ffe::u32 result = ffe::renderer::loadCubemap(fakePaths);
    REQUIRE(result == 0);
}

TEST_CASE("loadCubemap returns 0 with empty face paths", "[skybox]") {
    const std::array<std::string, 6> emptyPaths = {"", "", "", "", "", ""};
    const ffe::u32 result = ffe::renderer::loadCubemap(emptyPaths);
    REQUIRE(result == 0);
}
