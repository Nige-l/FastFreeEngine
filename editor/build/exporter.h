#pragma once

#include "build/build_config.h"

#include <string>

namespace ffe::editor {

/// Result of a game export operation.
struct ExportResult {
    bool        success = false;
    std::string errorMessage;
    std::string outputPath;   // Absolute path to the exported game directory
};

/// Export the game as a standalone package.
///
/// Steps:
/// 1. Create output directory (config.outputDir / config.projectName)
/// 2. Copy the runtime binary (ffe_runtime) into the output, renamed to project name
/// 3. Copy asset directories into output/assets/
/// 4. Copy scene files into output/scenes/
/// 5. Copy script directories into output/scripts/
/// 6. Generate main.lua that loads the entry scene
///
/// @param config            Build configuration (project name, directories, etc.)
/// @param runtimeBinaryPath Path to the pre-built ffe_runtime executable
/// @return ExportResult with success/failure and output path or error message
ExportResult exportGame(const BuildConfig& config, const std::string& runtimeBinaryPath);

} // namespace ffe::editor
