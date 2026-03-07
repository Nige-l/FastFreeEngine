#pragma once

#include "build/build_config.h"
#include "build/exporter.h"

#include <string>

namespace ffe::editor {

/// ImGui panel for configuring and triggering game export.
/// Displays project settings, asset/script directory lists,
/// and an "Export Game" button.
class BuildPanel {
public:
    /// Draw the panel. Call once per frame inside an ImGui context.
    void draw();

private:
    BuildConfig m_config{};
    ExportResult m_lastResult{};
    bool m_hasExported = false;

    // Path to the runtime binary (auto-detected or manually set)
    char m_runtimePath[256] = "ffe_runtime";

    void drawDirectoryList(const char* label, char dirs[][256], int maxCount,
                           int& count);
};

} // namespace ffe::editor
