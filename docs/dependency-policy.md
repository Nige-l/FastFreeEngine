# Dependency Policy

## Two sources: vcpkg and third_party/

**vcpkg** (`vcpkg.json`) — compiled libraries with build systems, transitive dependencies, or version management needs.

**third_party/** — single-file or header-only libraries that are simpler to vendor directly than to manage through vcpkg.

## Current dependencies

### vcpkg

| Library | Purpose |
|---------|---------|
| entt | ECS framework |
| joltphysics | 3D physics |
| luajit | Scripting runtime |
| sol2 | C++ ↔ Lua binding layer |
| glm | Math (vectors, matrices, quaternions) |
| imgui | Editor UI (with GLFW + OpenGL3 backends) |
| stb | Image loading (stb_image) |
| nlohmann-json | JSON parsing (scene serialization, config) |
| tracy | Profiling (optional, compile-time) |
| catch2 | Test framework |
| volk | Vulkan loader (meta-loader, no linking to libvulkan) |
| vulkan-headers | Vulkan API headers |
| vulkan-memory-allocator | VMA — Vulkan memory allocation |
| enet | UDP networking |

### third_party/ (vendored)

| File | Purpose | Why vendored |
|------|---------|-------------|
| cgltf.h | glTF/GLB mesh loading | Single header, no vcpkg port needed |
| glad/ | OpenGL function loader (generated) | Generated for specific GL version/extensions |
| httplib.h | HTTP client (LLM panel) | Single header, only used in editor builds |
| miniaudio.h | Audio backend | Single header, no vcpkg port needed |
| stb/ | stb_truetype, stb_image_write | Specific headers not covered by vcpkg stb |
| stb_vorbis.c | Vorbis audio decoding | Single file, pairs with miniaudio |

## Policy for adding new dependencies

1. **Justify the dependency.** Can the feature be implemented without it? A 50-line implementation beats a new dependency.
2. **Check size and build impact.** Large dependencies (>10s compile time, >1MB binary size) need explicit approval.
3. **Prefer header-only / single-file** for small libraries → vendor in `third_party/`.
4. **Use vcpkg** for anything with a build system, transitive dependencies, or frequent version updates.
5. **License must be MIT, BSD, zlib, or public domain.** No GPL, no LGPL in engine core.
6. **Document in commit message.** Every new dependency must be called out in the commit that adds it.
7. **Update this file** when adding or removing any dependency.

## Removing dependencies

If a vendored library or vcpkg dependency is no longer used, remove it. Dead dependencies are technical debt.
