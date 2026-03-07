#pragma once

#include <string>

namespace ffe::editor {

/// Build configuration for exporting a standalone game package.
/// Fixed-size buffers avoid heap allocations — these are editor-time
/// settings, not per-frame data, but we follow engine conventions.
struct BuildConfig {
    char projectName[64]       = "MyGame";
    char entryScene[256]       = "";           // Path to the first scene file
    char outputDir[256]        = "export/";    // Export output directory
    char assetDirs[4][256]     = {"assets/", "textures/", "", ""};
    char scriptDirs[2][256]    = {"scripts/", ""};
    int  assetDirCount         = 2;
    int  scriptDirCount        = 1;
};

/// Save build config to a JSON file at the given path.
/// Returns true on success.
bool saveBuildConfig(const BuildConfig& config, const std::string& path);

/// Load build config from a JSON file at the given path.
/// Returns true on success. On failure, config is left unchanged.
bool loadBuildConfig(BuildConfig& config, const std::string& path);

} // namespace ffe::editor
