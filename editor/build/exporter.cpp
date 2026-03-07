#include "build/exporter.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace ffe::editor {

namespace {

/// Copy the contents of srcDir into destDir, creating destDir if needed.
/// Returns an error string on failure, or empty string on success.
std::string copyDirectoryContents(const fs::path& srcDir, const fs::path& destDir) {
    std::error_code ec;
    if (!fs::exists(srcDir, ec)) {
        // Source does not exist — skip silently (user may have listed an empty slot)
        return {};
    }
    if (!fs::is_directory(srcDir, ec)) {
        return "Not a directory: " + srcDir.string();
    }
    fs::create_directories(destDir, ec);
    if (ec) {
        return "Failed to create directory: " + destDir.string() + " (" + ec.message() + ")";
    }
    fs::copy(srcDir, destDir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return "Failed to copy " + srcDir.string() + " -> " + destDir.string() + " (" + ec.message() + ")";
    }
    return {};
}

} // anonymous namespace

ExportResult exportGame(const BuildConfig& config, const std::string& runtimeBinaryPath) {
    ExportResult result;

    // --- Validate inputs ---
    if (std::strlen(config.projectName) == 0) {
        result.errorMessage = "Project name is empty";
        return result;
    }
    if (std::strlen(config.entryScene) == 0) {
        result.errorMessage = "Entry scene is not set";
        return result;
    }

    // Verify runtime binary exists
    std::error_code ec;
    if (!fs::exists(runtimeBinaryPath, ec)) {
        result.errorMessage = "Runtime binary not found: " + runtimeBinaryPath;
        return result;
    }

    // --- Step 1: Create output directory ---
    const fs::path outputRoot = fs::path(config.outputDir) / config.projectName;
    fs::create_directories(outputRoot, ec);
    if (ec) {
        result.errorMessage = "Failed to create output directory: " + outputRoot.string() +
                              " (" + ec.message() + ")";
        return result;
    }

    // --- Step 2: Copy runtime binary, renamed to project name ---
    const fs::path destBinary = outputRoot / config.projectName;
    fs::copy_file(runtimeBinaryPath, destBinary, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.errorMessage = "Failed to copy runtime binary: " + ec.message();
        return result;
    }
    // Make the binary executable (POSIX)
    fs::permissions(destBinary,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    // Non-fatal if permissions fail (e.g., on Windows)

    // --- Step 3: Copy asset directories into output/assets/ ---
    const fs::path assetsOut = outputRoot / "assets";
    for (int i = 0; i < config.assetDirCount && i < 4; ++i) {
        if (std::strlen(config.assetDirs[i]) == 0) {
            continue;
        }
        const fs::path srcDir(config.assetDirs[i]);
        // Preserve the directory name under assets/
        const fs::path destDir = assetsOut / srcDir.filename();
        const auto err = copyDirectoryContents(srcDir, destDir);
        if (!err.empty()) {
            result.errorMessage = err;
            return result;
        }
    }

    // --- Step 4: Copy entry scene into output/scenes/ ---
    const fs::path scenesOut = outputRoot / "scenes";
    fs::create_directories(scenesOut, ec);
    if (ec) {
        result.errorMessage = "Failed to create scenes directory: " + ec.message();
        return result;
    }
    const fs::path entrySceneSrc(config.entryScene);
    if (fs::exists(entrySceneSrc, ec)) {
        const fs::path entrySceneDest = scenesOut / entrySceneSrc.filename();
        fs::copy_file(entrySceneSrc, entrySceneDest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.errorMessage = "Failed to copy entry scene: " + ec.message();
            return result;
        }
    }
    // Note: entry scene may not exist yet during testing — we still generate main.lua

    // --- Step 5: Copy script directories into output/scripts/ ---
    const fs::path scriptsOut = outputRoot / "scripts";
    for (int i = 0; i < config.scriptDirCount && i < 2; ++i) {
        if (std::strlen(config.scriptDirs[i]) == 0) {
            continue;
        }
        const fs::path srcDir(config.scriptDirs[i]);
        const fs::path destDir = scriptsOut / srcDir.filename();
        const auto err = copyDirectoryContents(srcDir, destDir);
        if (!err.empty()) {
            result.errorMessage = err;
            return result;
        }
    }

    // --- Step 6: Generate main.lua ---
    const fs::path mainLuaPath = outputRoot / "main.lua";
    {
        std::ofstream ofs(mainLuaPath);
        if (!ofs.is_open()) {
            result.errorMessage = "Failed to create main.lua at: " + mainLuaPath.string();
            return result;
        }
        const fs::path sceneRelPath = fs::path("scenes") / fs::path(config.entryScene).filename();
        ofs << "-- Auto-generated by FFE Editor\n";
        ofs << "ffe.loadScene(\"" << sceneRelPath.string() << "\")\n";
        if (!ofs.good()) {
            result.errorMessage = "Failed to write main.lua";
            return result;
        }
    }

    // --- Done ---
    result.success    = true;
    result.outputPath = fs::absolute(outputRoot).string();
    return result;
}

} // namespace ffe::editor
