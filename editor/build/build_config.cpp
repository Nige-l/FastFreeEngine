#include "build/build_config.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>

namespace ffe::editor {

bool saveBuildConfig(const BuildConfig& config, const std::string& path) {
    nlohmann::json j;
    j["projectName"] = config.projectName;
    j["entryScene"]  = config.entryScene;
    j["outputDir"]   = config.outputDir;

    // Serialize asset directories (only the active ones)
    auto& assetArr = j["assetDirs"];
    assetArr = nlohmann::json::array();
    for (int i = 0; i < config.assetDirCount && i < 4; ++i) {
        assetArr.push_back(config.assetDirs[i]);
    }

    // Serialize script directories (only the active ones)
    auto& scriptArr = j["scriptDirs"];
    scriptArr = nlohmann::json::array();
    for (int i = 0; i < config.scriptDirCount && i < 2; ++i) {
        scriptArr.push_back(config.scriptDirs[i]);
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        return false;
    }
    ofs << j.dump(2);
    return ofs.good();
}

bool loadBuildConfig(BuildConfig& config, const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(ifs)),
                               std::istreambuf_iterator<char>());
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded()) {
        return false;
    }

    BuildConfig tmp{};

    if (j.contains("projectName") && j["projectName"].is_string()) {
        const auto& s = j["projectName"].get<std::string>();
        std::strncpy(tmp.projectName, s.c_str(), sizeof(tmp.projectName) - 1);
        tmp.projectName[sizeof(tmp.projectName) - 1] = '\0';
    }
    if (j.contains("entryScene") && j["entryScene"].is_string()) {
        const auto& s = j["entryScene"].get<std::string>();
        std::strncpy(tmp.entryScene, s.c_str(), sizeof(tmp.entryScene) - 1);
        tmp.entryScene[sizeof(tmp.entryScene) - 1] = '\0';
    }
    if (j.contains("outputDir") && j["outputDir"].is_string()) {
        const auto& s = j["outputDir"].get<std::string>();
        std::strncpy(tmp.outputDir, s.c_str(), sizeof(tmp.outputDir) - 1);
        tmp.outputDir[sizeof(tmp.outputDir) - 1] = '\0';
    }

    if (j.contains("assetDirs") && j["assetDirs"].is_array()) {
        const auto& arr = j["assetDirs"];
        tmp.assetDirCount = static_cast<int>(std::min(arr.size(), static_cast<size_t>(4)));
        for (int i = 0; i < tmp.assetDirCount; ++i) {
            if (arr[i].is_string()) {
                const auto& s = arr[i].get<std::string>();
                std::strncpy(tmp.assetDirs[i], s.c_str(), sizeof(tmp.assetDirs[i]) - 1);
                tmp.assetDirs[i][sizeof(tmp.assetDirs[i]) - 1] = '\0';
            }
        }
    }

    if (j.contains("scriptDirs") && j["scriptDirs"].is_array()) {
        const auto& arr = j["scriptDirs"];
        tmp.scriptDirCount = static_cast<int>(std::min(arr.size(), static_cast<size_t>(2)));
        for (int i = 0; i < tmp.scriptDirCount; ++i) {
            if (arr[i].is_string()) {
                const auto& s = arr[i].get<std::string>();
                std::strncpy(tmp.scriptDirs[i], s.c_str(), sizeof(tmp.scriptDirs[i]) - 1);
                tmp.scriptDirs[i][sizeof(tmp.scriptDirs[i]) - 1] = '\0';
            }
        }
    }

    config = tmp;
    return true;
}

} // namespace ffe::editor
