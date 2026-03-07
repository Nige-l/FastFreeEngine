#include <catch2/catch_test_macros.hpp>

#include "build/build_config.h"
#include "build/exporter.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace ffe::editor;

// Helper: create a temporary directory for test output, cleaned up on destruction.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) {
        path = fs::temp_directory_path() / ("ffe_test_" + name + "_" +
               std::to_string(std::hash<std::string>{}(name) ^ static_cast<size_t>(
                   std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// ============================================================
// BuildConfig save/load round-trip
// ============================================================

TEST_CASE("BuildConfig save/load round-trip", "[editor][build_config]") {
    TempDir tmp("build_config");
    const std::string configPath = (tmp.path / "config.json").string();

    BuildConfig original{};
    std::strncpy(original.projectName, "TestGame", sizeof(original.projectName));
    std::strncpy(original.entryScene, "scenes/level1.json", sizeof(original.entryScene));
    std::strncpy(original.outputDir, "/tmp/out/", sizeof(original.outputDir));
    std::strncpy(original.assetDirs[0], "art/", sizeof(original.assetDirs[0]));
    std::strncpy(original.assetDirs[1], "audio/", sizeof(original.assetDirs[1]));
    std::strncpy(original.assetDirs[2], "meshes/", sizeof(original.assetDirs[2]));
    original.assetDirCount = 3;
    std::strncpy(original.scriptDirs[0], "lua/", sizeof(original.scriptDirs[0]));
    original.scriptDirCount = 1;

    REQUIRE(saveBuildConfig(original, configPath));

    BuildConfig loaded{};
    REQUIRE(loadBuildConfig(loaded, configPath));

    CHECK(std::string(loaded.projectName) == "TestGame");
    CHECK(std::string(loaded.entryScene) == "scenes/level1.json");
    CHECK(std::string(loaded.outputDir) == "/tmp/out/");
    CHECK(loaded.assetDirCount == 3);
    CHECK(std::string(loaded.assetDirs[0]) == "art/");
    CHECK(std::string(loaded.assetDirs[1]) == "audio/");
    CHECK(std::string(loaded.assetDirs[2]) == "meshes/");
    CHECK(loaded.scriptDirCount == 1);
    CHECK(std::string(loaded.scriptDirs[0]) == "lua/");
}

TEST_CASE("BuildConfig load from nonexistent file returns false", "[editor][build_config]") {
    BuildConfig config{};
    CHECK_FALSE(loadBuildConfig(config, "/nonexistent/path/config.json"));
    // Original values should be unchanged
    CHECK(std::string(config.projectName) == "MyGame");
}

TEST_CASE("BuildConfig load from invalid JSON returns false", "[editor][build_config]") {
    TempDir tmp("build_config_invalid");
    const std::string configPath = (tmp.path / "bad.json").string();
    {
        std::ofstream ofs(configPath);
        ofs << "this is not json {{{{";
    }
    BuildConfig config{};
    CHECK_FALSE(loadBuildConfig(config, configPath));
    CHECK(std::string(config.projectName) == "MyGame");
}

// ============================================================
// Exporter tests
// ============================================================

TEST_CASE("exportGame creates output directory structure", "[editor][exporter]") {
    TempDir tmp("exporter");

    // Create a fake runtime binary
    const fs::path fakeRuntime = tmp.path / "ffe_runtime";
    {
        std::ofstream ofs(fakeRuntime);
        ofs << "#!/bin/sh\necho fake\n";
    }

    // Create a fake entry scene
    const fs::path sceneFile = tmp.path / "level1.json";
    {
        std::ofstream ofs(sceneFile);
        ofs << R"({"entities":[]})";
    }

    // Create a fake asset directory
    const fs::path assetDir = tmp.path / "textures";
    fs::create_directories(assetDir);
    {
        std::ofstream ofs(assetDir / "player.png");
        ofs << "fake png data";
    }

    // Create a fake script directory
    const fs::path scriptDir = tmp.path / "scripts";
    fs::create_directories(scriptDir);
    {
        std::ofstream ofs(scriptDir / "player.lua");
        ofs << "-- player script";
    }

    BuildConfig config{};
    std::strncpy(config.projectName, "TestGame", sizeof(config.projectName) - 1);
    config.projectName[sizeof(config.projectName) - 1] = '\0';
    std::strncpy(config.entryScene, sceneFile.c_str(), sizeof(config.entryScene) - 1);
    config.entryScene[sizeof(config.entryScene) - 1] = '\0';
    const std::string outDir = (tmp.path / "output").string() + "/";
    std::strncpy(config.outputDir, outDir.c_str(), sizeof(config.outputDir) - 1);
    config.outputDir[sizeof(config.outputDir) - 1] = '\0';
    std::strncpy(config.assetDirs[0], assetDir.c_str(), sizeof(config.assetDirs[0]) - 1);
    config.assetDirs[0][sizeof(config.assetDirs[0]) - 1] = '\0';
    config.assetDirCount = 1;
    std::strncpy(config.scriptDirs[0], scriptDir.c_str(), sizeof(config.scriptDirs[0]) - 1);
    config.scriptDirs[0][sizeof(config.scriptDirs[0]) - 1] = '\0';
    config.scriptDirCount = 1;

    const auto result = exportGame(config, fakeRuntime.string());

    REQUIRE(result.success);
    CHECK_FALSE(result.outputPath.empty());

    // Verify directory structure
    const fs::path gameDir = fs::path(outDir) / "TestGame";
    CHECK(fs::exists(gameDir / "TestGame"));           // runtime binary copy
    CHECK(fs::exists(gameDir / "main.lua"));           // generated entry point
    CHECK(fs::exists(gameDir / "scenes" / "level1.json"));
    CHECK(fs::exists(gameDir / "assets" / "textures" / "player.png"));
    CHECK(fs::exists(gameDir / "scripts" / "scripts" / "player.lua"));
}

TEST_CASE("exportGame generates main.lua with entry scene reference", "[editor][exporter]") {
    TempDir tmp("exporter_mainlua");

    // Create a fake runtime binary
    const fs::path fakeRuntime = tmp.path / "ffe_runtime";
    {
        std::ofstream ofs(fakeRuntime);
        ofs << "fake";
    }

    BuildConfig config{};
    std::strncpy(config.projectName, "MyGame", sizeof(config.projectName) - 1);
    config.projectName[sizeof(config.projectName) - 1] = '\0';
    std::strncpy(config.entryScene, "level1.json", sizeof(config.entryScene) - 1);
    config.entryScene[sizeof(config.entryScene) - 1] = '\0';
    const std::string outDir = (tmp.path / "output").string() + "/";
    std::strncpy(config.outputDir, outDir.c_str(), sizeof(config.outputDir) - 1);
    config.outputDir[sizeof(config.outputDir) - 1] = '\0';
    config.assetDirCount = 0;
    config.scriptDirCount = 0;

    const auto result = exportGame(config, fakeRuntime.string());
    REQUIRE(result.success);

    // Read main.lua and verify contents
    const fs::path mainLua = fs::path(outDir) / "MyGame" / "main.lua";
    REQUIRE(fs::exists(mainLua));

    std::ifstream ifs(mainLua);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    CHECK(content.find("ffe.loadScene") != std::string::npos);
    CHECK(content.find("scenes/level1.json") != std::string::npos);
    CHECK(content.find("Auto-generated by FFE Editor") != std::string::npos);
}

TEST_CASE("exportGame fails gracefully with missing runtime binary", "[editor][exporter]") {
    BuildConfig config{};
    std::strncpy(config.projectName, "MyGame", sizeof(config.projectName));
    std::strncpy(config.entryScene, "level1.json", sizeof(config.entryScene));
    std::strncpy(config.outputDir, "/tmp/ffe_test_no_runtime/", sizeof(config.outputDir));

    const auto result = exportGame(config, "/nonexistent/ffe_runtime");

    CHECK_FALSE(result.success);
    CHECK(result.errorMessage.find("Runtime binary not found") != std::string::npos);
}

TEST_CASE("exportGame fails with empty project name", "[editor][exporter]") {
    BuildConfig config{};
    std::memset(config.projectName, 0, sizeof(config.projectName));
    std::strncpy(config.entryScene, "level1.json", sizeof(config.entryScene));

    const auto result = exportGame(config, "/some/path");

    CHECK_FALSE(result.success);
    CHECK(result.errorMessage.find("Project name is empty") != std::string::npos);
}

TEST_CASE("exportGame fails with empty entry scene", "[editor][exporter]") {
    BuildConfig config{};
    std::strncpy(config.projectName, "MyGame", sizeof(config.projectName));
    std::memset(config.entryScene, 0, sizeof(config.entryScene));

    const auto result = exportGame(config, "/some/path");

    CHECK_FALSE(result.success);
    CHECK(result.errorMessage.find("Entry scene is not set") != std::string::npos);
}
