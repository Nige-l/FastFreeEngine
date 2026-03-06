#include <catch2/catch_test_macros.hpp>
#include "scripting/script_engine.h"
#include "core/ecs.h"
#include "core/input.h"
#include "renderer/render_system.h"
#include "renderer/rhi.h"
#include "renderer/rhi_types.h"
#include "renderer/texture_loader.h"
#include "audio/audio.h"
#include "physics/collider2d.h"
#include "renderer/text_renderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

// =============================================================================
// Fixtures
// =============================================================================

// SaveLoadFixture: creates a ScriptEngine with a temporary save root.
// The temp directory and all contents are removed on destruction.
struct SaveLoadFixture {
    ffe::ScriptEngine engine;
    std::string tmpDir;

    SaveLoadFixture() {
        REQUIRE(engine.init());

        // Create a unique temporary directory for this test.
        char tmpl[] = "/tmp/ffe_save_test_XXXXXX";
        const char* dir = mkdtemp(tmpl);
        REQUIRE(dir != nullptr);
        tmpDir = dir;

        REQUIRE(engine.setSaveRoot(tmpDir.c_str()));
    }

    ~SaveLoadFixture() {
        engine.shutdown();
        // Clean up temp directory.
        std::error_code ec;
        std::filesystem::remove_all(tmpDir, ec);
    }
};

// =============================================================================
// setSaveRoot
// =============================================================================

TEST_CASE("setSaveRoot rejects null", "[scripting][save]") {
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());
    REQUIRE_FALSE(engine.setSaveRoot(nullptr));
    engine.shutdown();
}

TEST_CASE("setSaveRoot rejects empty string", "[scripting][save]") {
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());
    REQUIRE_FALSE(engine.setSaveRoot(""));
    engine.shutdown();
}

TEST_CASE("setSaveRoot is write-once", "[scripting][save]") {
    SaveLoadFixture fix;
    // Already set in fixture — second call must fail.
    REQUIRE_FALSE(fix.engine.setSaveRoot("/tmp/other"));
}

TEST_CASE("saveRoot returns nullptr when not set", "[scripting][save]") {
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());
    REQUIRE(engine.saveRoot() == nullptr);
    engine.shutdown();
}

TEST_CASE("saveRoot returns set path", "[scripting][save]") {
    SaveLoadFixture fix;
    REQUIRE(fix.engine.saveRoot() != nullptr);
    REQUIRE(std::string(fix.engine.saveRoot()) == fix.tmpDir);
}

// =============================================================================
// saveData — basic functionality
// =============================================================================

TEST_CASE("saveData writes a JSON file", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("test.json", { level = 3, score = 1500 })
        assert(ok == true, "saveData should return true, got: " .. tostring(err))
    )");
    REQUIRE(ok);

    // Verify the file exists.
    const std::string filePath = fix.tmpDir + "/saves/test.json";
    REQUIRE(std::filesystem::exists(filePath));
}

TEST_CASE("loadData reads back saved data", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("round_trip.json", { level = 5, name = "Alice", active = true })
        assert(ok, "save failed: " .. tostring(err))

        local data, err2 = ffe.loadData("round_trip.json")
        assert(data ~= nil, "load failed: " .. tostring(err2))
        assert(data.level == 5, "expected level 5, got " .. tostring(data.level))
        assert(data.name == "Alice", "expected name Alice, got " .. tostring(data.name))
        assert(data.active == true, "expected active true")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData overwrites existing file", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        ffe.saveData("overwrite.json", { version = 1 })
        ffe.saveData("overwrite.json", { version = 2 })

        local data = ffe.loadData("overwrite.json")
        assert(data.version == 2, "expected version 2 after overwrite")
    )");
    REQUIRE(ok);
}

// =============================================================================
// saveData — nested tables and arrays
// =============================================================================

TEST_CASE("saveData handles nested tables", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local data = {
            player = { name = "Bob", hp = 100 },
            inventory = { "sword", "shield", "potion" }
        }
        local ok, err = ffe.saveData("nested.json", data)
        assert(ok, "save failed: " .. tostring(err))

        local loaded = ffe.loadData("nested.json")
        assert(loaded.player.name == "Bob")
        assert(loaded.player.hp == 100)
        assert(loaded.inventory[1] == "sword")
        assert(loaded.inventory[2] == "shield")
        assert(loaded.inventory[3] == "potion")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData handles array-like tables", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("array.json", { scores = { 100, 200, 300 } })
        assert(ok, "save failed: " .. tostring(err))

        local data = ffe.loadData("array.json")
        assert(data.scores[1] == 100)
        assert(data.scores[2] == 200)
        assert(data.scores[3] == 300)
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData handles booleans and nil", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("bools.json", { flag = true, other = false })
        assert(ok, "save failed: " .. tostring(err))

        local data = ffe.loadData("bools.json")
        assert(data.flag == true)
        assert(data.other == false)
    )");
    REQUIRE(ok);
}

// =============================================================================
// saveData — filename validation (S1)
// =============================================================================

TEST_CASE("saveData rejects empty filename", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("", { x = 1 })
        assert(ok == nil, "should fail for empty filename")
        assert(err == "invalid filename", "wrong error: " .. tostring(err))
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData rejects path traversal (..)", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("../evil.json", { x = 1 })
        assert(ok == nil, "should fail for path traversal")
        assert(err == "invalid filename")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData rejects slashes in filename", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("subdir/file.json", { x = 1 })
        assert(ok == nil, "should fail for slash in filename")
        assert(err == "invalid filename")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData rejects non-.json extension", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("save.txt", { x = 1 })
        assert(ok == nil, "should fail for non-json extension")
        assert(err == "invalid filename")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData rejects backslash in filename", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("sub\\file.json", { x = 1 })
        assert(ok == nil, "should fail for backslash")
        assert(err == "invalid filename")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData rejects special characters", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("save file.json", { x = 1 })
        assert(ok == nil, "should fail for space in filename")
        assert(err == "invalid filename")
    )");
    REQUIRE(ok);
}

// =============================================================================
// saveData — type validation
// =============================================================================

TEST_CASE("saveData rejects non-string filename", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData(42, { x = 1 })
        assert(ok == nil)
        assert(err == "filename must be a string")
    )");
    REQUIRE(ok);
}

TEST_CASE("saveData rejects non-table data", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("test.json", "not a table")
        assert(ok == nil)
        assert(err == "data must be a table")
    )");
    REQUIRE(ok);
}

// =============================================================================
// loadData — error cases
// =============================================================================

TEST_CASE("loadData returns nil for non-existent file", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local data, err = ffe.loadData("nonexistent.json")
        assert(data == nil)
        assert(err == "file not found")
    )");
    REQUIRE(ok);
}

TEST_CASE("loadData rejects invalid filename", "[scripting][save][security]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local data, err = ffe.loadData("../evil.json")
        assert(data == nil)
        assert(err == "invalid filename")
    )");
    REQUIRE(ok);
}

TEST_CASE("loadData rejects non-string filename", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local data, err = ffe.loadData(123)
        assert(data == nil)
        assert(err == "filename must be a string")
    )");
    REQUIRE(ok);
}

// =============================================================================
// save root not configured
// =============================================================================

TEST_CASE("saveData fails when save root not set", "[scripting][save]") {
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());

    const bool ok = engine.doString(R"(
        local ok, err = ffe.saveData("test.json", { x = 1 })
        assert(ok == nil)
        assert(err == "save root not configured")
    )");
    REQUIRE(ok);

    engine.shutdown();
}

TEST_CASE("loadData fails when save root not set", "[scripting][save]") {
    ffe::ScriptEngine engine;
    REQUIRE(engine.init());

    const bool ok = engine.doString(R"(
        local data, err = ffe.loadData("test.json")
        assert(data == nil)
        assert(err == "save root not configured")
    )");
    REQUIRE(ok);

    engine.shutdown();
}

// =============================================================================
// saveData — skips unsupported types without failing
// =============================================================================

TEST_CASE("saveData skips functions in table without failing", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        local data = { score = 100, callback = function() end }
        local ok, err = ffe.saveData("skip_func.json", data)
        assert(ok, "save should succeed even with function in table: " .. tostring(err))

        local loaded = ffe.loadData("skip_func.json")
        assert(loaded.score == 100)
        assert(loaded.callback == nil, "function should not be serialized")
    )");
    REQUIRE(ok);
}

// =============================================================================
// saveData — creates saves/ directory automatically
// =============================================================================

TEST_CASE("saveData creates saves directory if missing", "[scripting][save]") {
    SaveLoadFixture fix;

    // The saves/ directory should not exist yet.
    const std::string savesDir = fix.tmpDir + "/saves";
    REQUIRE_FALSE(std::filesystem::exists(savesDir));

    const bool ok = fix.engine.doString(R"(
        local ok, err = ffe.saveData("auto_dir.json", { x = 1 })
        assert(ok, "save failed: " .. tostring(err))
    )");
    REQUIRE(ok);

    REQUIRE(std::filesystem::exists(savesDir));
    REQUIRE(std::filesystem::is_directory(savesDir));
}

// =============================================================================
// Numeric types (integer vs float round-trip)
// =============================================================================

TEST_CASE("saveData preserves integer and float distinction", "[scripting][save]") {
    SaveLoadFixture fix;

    const bool ok = fix.engine.doString(R"(
        ffe.saveData("numbers.json", { int_val = 42, float_val = 3.14 })
        local data = ffe.loadData("numbers.json")
        assert(data.int_val == 42, "integer mismatch: " .. tostring(data.int_val))
        -- Float comparison with tolerance
        assert(math.abs(data.float_val - 3.14) < 0.001, "float mismatch: " .. tostring(data.float_val))
    )");
    REQUIRE(ok);
}
