#pragma once

// demo_paths.h — compute asset and script roots relative to the executable.
//
// All FFE demos use this to find assets/ and their script directory without
// hardcoding absolute paths. Works on Linux via /proc/self/exe.
//
// Usage:
//   char assetRoot[512], scriptRoot[512];
//   demoAssetRoot(assetRoot, sizeof(assetRoot));
//   demoScriptRoot(assetRoot, "pong", scriptRoot, sizeof(scriptRoot));

#include <cstdio>
#include <cstring>
#include <unistd.h>

// Get the project root by resolving /proc/self/exe and walking up to find assets/.
// Returns false if resolution fails.
inline bool demoProjectRoot(char* buf, size_t bufSize) {
    char exePath[512];
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) { return false; }
    exePath[len] = '\0';

    // Walk up directory levels until we find one containing "assets/"
    // Executable is typically at: <project>/build/examples/<demo>/ffe_<demo>
    // So we need to go up 4 levels to reach <project>.
    // But to be robust, we walk up and check for assets/ at each level.
    char* slash = exePath + len;
    for (int i = 0; i < 6; ++i) {
        // Find last slash
        while (slash > exePath && *slash != '/') { --slash; }
        if (slash <= exePath) { break; }
        *slash = '\0';

        // Check if assets/ exists here
        char testPath[600];
        snprintf(testPath, sizeof(testPath), "%s/assets", exePath);
        if (access(testPath, F_OK) == 0) {
            snprintf(buf, bufSize, "%s", exePath);
            return true;
        }
    }
    return false;
}

// Compute the asset root: <projectRoot>/assets
inline bool demoAssetRoot(char* buf, size_t bufSize) {
    char root[512];
    if (!demoProjectRoot(root, sizeof(root))) { return false; }
    snprintf(buf, bufSize, "%s/assets", root);
    return true;
}

// Compute the script root for a named demo: <projectRoot>/examples/<demoName>
inline bool demoScriptRoot(const char* demoName, char* buf, size_t bufSize) {
    char root[512];
    if (!demoProjectRoot(root, sizeof(root))) { return false; }
    snprintf(buf, bufSize, "%s/examples/%s", root, demoName);
    return true;
}
