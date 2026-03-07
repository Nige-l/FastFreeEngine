#pragma once

// demo_paths.h — compute asset and script roots relative to the executable.
//
// All FFE demos use this to find assets/ and their script directory without
// hardcoding absolute paths.
//
// Linux:   resolves /proc/self/exe via readlink.
// Windows: resolves executable path via GetModuleFileNameA.
//
// Usage:
//   char assetRoot[512], scriptRoot[512];
//   demoAssetRoot(assetRoot, sizeof(assetRoot));
//   demoScriptRoot(assetRoot, "pong", scriptRoot, sizeof(scriptRoot));

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

// Get the project root by resolving the executable path and walking up to find assets/.
// Returns false if resolution fails.
inline bool demoProjectRoot(char* buf, size_t bufSize) {
    char exePath[512];

#ifdef _WIN32
    const DWORD len = GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(sizeof(exePath) - 1));
    if (len == 0 || len >= sizeof(exePath) - 1) { return false; }
    // GetModuleFileNameA writes a null terminator; ensure it.
    exePath[len] = '\0';
    // Convert backslashes to forward slashes for uniform handling below.
    for (DWORD i = 0; i < len; ++i) {
        if (exePath[i] == '\\') { exePath[i] = '/'; }
    }
    char* slash = exePath + len;
#else
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) { return false; }
    exePath[len] = '\0';
    char* slash = exePath + len;
#endif

    // Walk up directory levels until we find one containing "assets/"
    // Executable is typically at: <project>/build/examples/<demo>/ffe_<demo>[.exe]
    // So we need to go up 4 levels to reach <project>.
    // But to be robust, we walk up and check for assets/ at each level.
    for (int i = 0; i < 6; ++i) {
        // Find last slash
        while (slash > exePath && *slash != '/') { --slash; }
        if (slash <= exePath) { break; }
        *slash = '\0';

        // Check if assets/ exists here
        char testPath[600];
        snprintf(testPath, sizeof(testPath), "%s/assets", exePath);
#ifdef _WIN32
        const DWORD attrs = GetFileAttributesA(testPath);
        const bool found = (attrs != INVALID_FILE_ATTRIBUTES) &&
                           (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
        const bool found = (access(testPath, F_OK) == 0);
#endif
        if (found) {
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
