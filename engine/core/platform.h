#pragma once
// Cross-platform path canonicalization

#ifdef _WIN32
#include <stdlib.h>   // _fullpath
#else
#include <stdlib.h>   // realpath, free
#include <cstring>    // strlen, memcpy
#endif

namespace ffe {

/// Canonicalize a path, resolving symlinks and '..' components.
/// Returns true and writes into outBuf (size outBufSize) on success.
/// Returns false if the path does not exist, outBuf is too small, or
/// the resolved path would not fit within outBufSize bytes (including '\0').
///
/// POSIX: uses realpath(path, nullptr) which heap-allocates a PATH_MAX-sized
/// buffer internally, then copies the result into outBuf. This avoids writing
/// up to PATH_MAX (4096) bytes into an outBuf that may be smaller (e.g. the
/// 512-byte buffer used by setSaveRoot). The heap allocation is on the cold
/// path only (load-time / init-time).
///
/// Windows: _fullpath writes at most outBufSize bytes — the size is enforced
/// by the API directly.
inline bool canonicalizePath(const char* path, char* outBuf, size_t outBufSize) {
#ifdef _WIN32
    return _fullpath(outBuf, path, outBufSize) != nullptr;
#else
    // realpath(path, nullptr) heap-allocates and returns a null-terminated
    // canonical path of at most PATH_MAX bytes, or nullptr on failure.
    char* resolved = ::realpath(path, nullptr);
    if (!resolved) { return false; }
    const size_t len = ::strlen(resolved);
    if (len >= outBufSize) {          // won't fit (need len+1 bytes for '\0')
        ::free(resolved);
        return false;
    }
    ::memcpy(outBuf, resolved, len + 1u);  // +1 copies the null terminator
    ::free(resolved);
    return true;
#endif
}

} // namespace ffe
