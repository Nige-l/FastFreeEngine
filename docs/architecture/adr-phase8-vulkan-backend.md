# ADR: Phase 8 Milestone 1 -- Vulkan Backend Bootstrap

**Status:** Proposed
**Author:** architect
**Date:** 2026-03-07
**Tiers:** STANDARD (Vulkan 1.0), MODERN (Vulkan 1.2+)
**Supersedes:** None
**Related:** `docs/architecture/002-renderer-rhi.md` (original RHI design)

---

## 1. Context

### 1.1 Why Vulkan?

FFE's entire rendering stack runs on OpenGL 3.3 (LEGACY tier). This was the correct first choice -- it maximises hardware reach and keeps the engine accessible. But competitive engines have moved on:

- **Godot 4** shipped a Vulkan renderer (RenderingDevice) as its primary backend in 2023, retaining OpenGL 3.3 as a compatibility fallback. Godot's Vulkan path delivers compute shaders, GPU-driven rendering, and explicit multi-queue.
- **Bevy** uses wgpu, which abstracts over Vulkan/Metal/DX12/WebGPU. Bevy gets cross-platform Vulkan-class features without writing raw Vulkan.
- **Raylib** remains OpenGL-only (3.3/4.3/ES2). This is one reason Raylib is not competitive for shipped 3D games.
- **Unreal/Unity** have had Vulkan backends for years.

FFE cannot claim competitive parity with Godot 4 while limited to OpenGL. Specific capabilities Vulkan unlocks:

1. **Compute shaders** -- GPU particle simulation, culling, terrain generation
2. **Explicit memory management** -- VMA (already in vcpkg.json) enables sub-allocation strategies that eliminate driver overhead
3. **Multi-threaded command recording** -- record command buffers on worker threads, submit from main thread
4. **Render graph potential** -- explicit synchronisation enables a future render graph that automates barrier placement
5. **Validation layers** -- Vulkan's validation infrastructure catches bugs that OpenGL's error model silently ignores
6. **Ray tracing** (MODERN tier) -- VK_KHR_ray_tracing_pipeline for future work

### 1.2 What FFE Gains from M1

M1 is narrowly scoped: prove the pipeline works by rendering a clear-color frame through Vulkan. No mesh rendering, no shaders beyond the trivial clear pass. The value is:

- The RHI abstraction is refactored to support multiple backends without breaking existing code
- The Vulkan initialisation sequence (instance, device, swap chain, render pass) is validated
- The build system knows how to conditionally compile OpenGL or Vulkan
- Developers can pass `-DFFE_BACKEND=VULKAN` and see a coloured window

---

## 2. Decision

### 2.1 RHI Abstraction Strategy: Compile-Time Backend Selection

**Chosen approach: compile-time polymorphism via build flag.**

The constitution (Section 3) prohibits virtual function calls in per-frame code. The current RHI is a set of free functions in `namespace ffe::rhi`. Both backends will implement the same set of free functions with the same signatures. The linker selects which `.cpp` file provides the symbols based on a CMake option.

**Rejected alternatives:**

| Approach | Why rejected |
|----------|-------------|
| Virtual interface (`class RhiBackend`) | Virtual dispatch on every draw call, uniform set, and state change violates Section 3. Even with devirtualisation hints, the compiler cannot guarantee inlining across translation units. |
| `std::function` / function-pointer table | Hidden heap allocation (`std::function`) or indirect call overhead (function pointers). Also complicates the API -- every RHI call becomes an indirect call through a table loaded at init. |
| Runtime backend selection (dlopen) | Adds dynamic library complexity. FFE targets students and hobbyists; "your game crashed because libvulkan.so wasn't found" is a bad experience. Compile-time selection is deterministic. |

**How it works:**

```
engine/renderer/rhi.h               -- public API (unchanged)
engine/renderer/rhi_types.h         -- shared types (unchanged)
engine/renderer/opengl/rhi_opengl.cpp  -- OpenGL implementation (exists)
engine/renderer/vulkan/rhi_vulkan.cpp  -- Vulkan implementation (new, M1)
```

Only ONE of `rhi_opengl.cpp` or `rhi_vulkan.cpp` is compiled into `ffe_renderer`. The CMake variable `FFE_BACKEND` controls which:

```cmake
set(FFE_BACKEND "OPENGL" CACHE STRING "RHI backend: OPENGL, VULKAN")
```

The `FFE_TIER` variable remains and controls feature levels. `FFE_BACKEND` controls which graphics API implements those features. The mapping:

| Tier | Allowed backends |
|------|-----------------|
| RETRO | OPENGL only |
| LEGACY | OPENGL only |
| STANDARD | OPENGL or VULKAN |
| MODERN | VULKAN only |

Setting `FFE_BACKEND=VULKAN` with `FFE_TIER=LEGACY` is a CMake error.

### 2.2 RHI Interface Changes for M1

The existing `rhi.h` interface is almost backend-agnostic. Two items need attention:

1. **`GLFWwindow*` in `endFrame()`** -- GLFW supports both OpenGL and Vulkan. The `endFrame(GLFWwindow*)` signature stays. The Vulkan backend uses `glfwGetRequiredInstanceExtensions()` and creates a `VkSurfaceKHR` via `glfwCreateWindowSurface()`. No signature change needed.

2. **`RhiConfig` additions** -- Add a `backend` field to `RhiConfig`:

```cpp
enum class RhiBackend : u8 {
    OPENGL,
    VULKAN,
};

struct RhiConfig {
    i32 viewportWidth  = 1280;
    i32 viewportHeight = 720;
    bool headless      = false;
    bool vsync         = true;
    bool debugGL       = false;        // retained for OpenGL
    bool debugVulkan   = false;        // enable validation layers
    RhiBackend backend = RhiBackend::OPENGL;  // informational; actual backend is compile-time
};
```

The `backend` field is informational -- the actual backend is determined at compile time. It allows runtime code (logging, diagnostics) to know which backend is active without preprocessor checks scattered everywhere.

3. **No new RHI functions in M1.** The Vulkan backend implements the existing `rhi.h` surface. Functions that cannot be meaningfully implemented yet (e.g., `createShader`, `drawArrays`) return stub results (`RhiResult::ERROR_UNSUPPORTED` or null handles) with a log message. M2+ will fill them in.

### 2.3 Vulkan Instance, Device, and Swap Chain Design

#### Instance Creation

```cpp
// engine/renderer/vulkan/vk_init.h
namespace ffe::rhi::vk {

struct VulkanContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          presentQueue   = VK_NULL_HANDLE;
    u32              graphicsFamily = 0;
    u32              presentFamily  = 0;
    VkSurfaceKHR     surface        = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain      = VK_NULL_HANDLE;
    VkFormat         swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D       swapchainExtent = {0, 0};
    VkRenderPass     clearPass      = VK_NULL_HANDLE;  // M1: clear-color-only render pass

    // Swap chain images (not owned -- destroyed with swapchain)
    static constexpr u32 MAX_SWAPCHAIN_IMAGES = 4;
    VkImage     swapImages[MAX_SWAPCHAIN_IMAGES]     = {};
    VkImageView swapImageViews[MAX_SWAPCHAIN_IMAGES] = {};
    VkFramebuffer swapFramebuffers[MAX_SWAPCHAIN_IMAGES] = {};
    u32         swapImageCount = 0;

    // Synchronisation (per frame-in-flight)
    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;
    VkSemaphore imageAvailable[MAX_FRAMES_IN_FLIGHT]  = {};
    VkSemaphore renderFinished[MAX_FRAMES_IN_FLIGHT]  = {};
    VkFence     inFlightFences[MAX_FRAMES_IN_FLIGHT]   = {};
    VkCommandPool   commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT] = {};
    u32 currentFrame = 0;
};

// Lifecycle
RhiResult initVulkan(VulkanContext& ctx, GLFWwindow* window, const RhiConfig& config);
void shutdownVulkan(VulkanContext& ctx);

// Frame
void beginFrameVulkan(VulkanContext& ctx, const glm::vec4& clearColor);
void endFrameVulkan(VulkanContext& ctx);

} // namespace ffe::rhi::vk
```

Key decisions:

- **Application info:** `apiVersion = VK_API_VERSION_1_0` (STANDARD tier). MODERN tier will request 1.2+.
- **Validation layers:** Enabled when `config.debugVulkan == true`. Layer name: `VK_LAYER_KHRONOS_validation`. Controlled by build type (Debug = on, Release = off by default).
- **Instance extensions:** `VK_KHR_surface` + platform surface (from `glfwGetRequiredInstanceExtensions()`). Debug builds add `VK_EXT_debug_utils`.
- **Device extensions:** `VK_KHR_swapchain` (required).

#### Physical Device Selection

Scoring heuristic (highest score wins):

1. **Discrete GPU:** +1000 points
2. **Integrated GPU:** +100 points
3. **Supports required queue families** (graphics + present): required (skip if missing)
4. **Supports swap chain** (at least one format + one present mode): required (skip if missing)
5. **VRAM size:** +1 point per 256 MB

If no device meets requirements, `initVulkan` returns `RhiResult::ERROR_UNSUPPORTED`.

#### Logical Device Creation

- One graphics queue, one present queue (may be the same family).
- No optional features enabled in M1. Future milestones enable features incrementally.
- No VMA allocator in M1 (no buffers/textures yet). VMA integration comes in M2 when buffer/texture creation is implemented.

#### Swap Chain

- **Format preference:** `VK_FORMAT_B8G8R8A8_SRGB` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`. Fall back to first available.
- **Present mode:** `VK_PRESENT_MODE_FIFO_KHR` (guaranteed available, equivalent to vsync). If `!config.vsync`, prefer `VK_PRESENT_MODE_MAILBOX_KHR` if available, else `VK_PRESENT_MODE_IMMEDIATE_KHR`.
- **Image count:** `minImageCount + 1`, clamped to `maxImageCount` (if nonzero).
- **Extent:** Match GLFW framebuffer size via `glfwGetFramebufferSize()`.
- **Swap chain recreation:** On `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR`, destroy and recreate. This handles window resize.

#### Render Pass (M1: Clear-Color Only)

A single render pass with one colour attachment:

- `loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`
- `storeOp = VK_ATTACHMENT_STORE_OP_STORE`
- `initialLayout = VK_IMAGE_LAYOUT_UNDEFINED`
- `finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`

One subpass, no depth attachment (M1 scope). The clear colour is passed via `VkClearValue` at `vkCmdBeginRenderPass`.

#### Frame Flow (M1)

```
beginFrame(clearColor):
    vkWaitForFences(inFlightFences[currentFrame])
    vkAcquireNextImageKHR -> imageIndex
    vkResetFences(inFlightFences[currentFrame])
    vkResetCommandBuffer(commandBuffers[currentFrame])
    vkBeginCommandBuffer
    vkCmdBeginRenderPass(clearPass, swapFramebuffers[imageIndex], clearColor)
    // M1: nothing drawn -- just clear
    vkCmdEndRenderPass
    vkEndCommandBuffer

endFrame(window):
    vkQueueSubmit(graphicsQueue, commandBuffers[currentFrame],
                  wait: imageAvailable, signal: renderFinished)
    vkQueuePresentKHR(presentQueue, renderFinished, swapchain, imageIndex)
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT
```

### 2.4 Vulkan Function Loading: volk

**Decision: Use volk for Vulkan function loading.**

volk is a meta-loader that loads Vulkan functions at runtime via `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr`. Benefits:

- No link-time dependency on `libvulkan.so` -- the Vulkan loader is opened via `dlopen` at runtime
- Device-specific function pointers avoid dispatch overhead through the loader trampoline
- Single-header library, easy to vendor or pull from vcpkg
- Used by major projects (Sascha Willems samples, The-Forge)

**vcpkg dependency:** Add `"volk"` to `vcpkg.json`. `vulkan-memory-allocator` is already present (added during an earlier planning session).

**Not using:** raw `vulkan-headers` + manual `vkGetInstanceProcAddr` calls. Too much boilerplate. GLAD-style generation (glad-vk) is an option but volk is more established in the Vulkan ecosystem.

### 2.5 Build System Changes

```cmake
# Root CMakeLists.txt -- add after FFE_TIER
set(FFE_BACKEND "OPENGL" CACHE STRING "RHI backend: OPENGL, VULKAN")
set_property(CACHE FFE_BACKEND PROPERTY STRINGS OPENGL VULKAN)

# Validate tier/backend combinations
if(FFE_BACKEND STREQUAL "VULKAN")
    if(FFE_TIER STREQUAL "RETRO" OR FFE_TIER STREQUAL "LEGACY")
        message(FATAL_ERROR "Vulkan backend requires STANDARD or MODERN tier. "
                "Set -DFFE_TIER=STANDARD or -DFFE_TIER=MODERN.")
    endif()
endif()
if(FFE_TIER STREQUAL "MODERN" AND NOT FFE_BACKEND STREQUAL "VULKAN")
    message(FATAL_ERROR "MODERN tier requires Vulkan backend. "
            "Set -DFFE_BACKEND=VULKAN.")
endif()
```

```cmake
# engine/renderer/CMakeLists.txt -- replace current backend selection
if(FFE_BACKEND STREQUAL "VULKAN")
    list(APPEND RENDERER_SOURCES
        vulkan/rhi_vulkan.cpp
        vulkan/vk_init.cpp
    )
    target_compile_definitions(ffe_renderer PRIVATE FFE_BACKEND_VULKAN=1)
else()
    list(APPEND RENDERER_SOURCES
        opengl/rhi_opengl.cpp
        opengl/gl_debug.cpp
    )
    target_compile_definitions(ffe_renderer PRIVATE FFE_BACKEND_OPENGL=1)
endif()

# Link dependencies based on backend
if(FFE_BACKEND STREQUAL "VULKAN")
    find_package(volk CONFIG REQUIRED)
    target_link_libraries(ffe_renderer PRIVATE volk::volk glfw)
else()
    target_link_libraries(ffe_renderer PRIVATE glad glfw Tracy::TracyClient)
endif()
```

The `glad` library is NOT linked for Vulkan builds. GLFW is still linked (window management). Tracy is linked for both.

### 2.6 Application Layer Changes

`application.cpp` currently calls `gladLoadGLLoader` and creates an OpenGL context via GLFW hints. For Vulkan:

- GLFW window hints change: `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)` instead of requesting an OpenGL context
- No `gladLoadGLLoader` call
- `rhi::init()` handles Vulkan instance/device/surface creation internally

This is controlled by a preprocessor check on `FFE_BACKEND_VULKAN`:

```cpp
#ifdef FFE_BACKEND_VULKAN
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // ... existing OpenGL hints
#endif
```

The Vulkan `rhi::init()` receives the `GLFWwindow*` pointer (added to `RhiConfig` or passed separately) to create `VkSurfaceKHR`. The OpenGL `rhi::init()` ignores it.

**RhiConfig addition for M1:**

```cpp
struct RhiConfig {
    // ... existing fields ...
    GLFWwindow* window = nullptr;  // Required for Vulkan surface creation; ignored by OpenGL
};
```

This is a minor interface change. The OpenGL backend already receives the window via `endFrame(GLFWwindow*)`, but Vulkan needs it at init time for surface creation. Passing it in config avoids a second init step.

---

## 3. File Plan (M1 Scope)

### New Files

| File | Purpose |
|------|---------|
| `engine/renderer/vulkan/rhi_vulkan.cpp` | Vulkan implementation of all `ffe::rhi` functions from `rhi.h`. M1: `init`/`shutdown`/`beginFrame`/`endFrame` are real; all others are stubs returning `ERROR_UNSUPPORTED` or null handles. |
| `engine/renderer/vulkan/vk_init.h` | `VulkanContext` struct and init/shutdown/frame function declarations. Internal header, not public API. |
| `engine/renderer/vulkan/vk_init.cpp` | Vulkan instance creation, device selection, swap chain creation, render pass creation, synchronisation setup, frame recording. |
| `tests/renderer/test_rhi_vulkan.cpp` | Unit tests for Vulkan init (headless mode -- verifies config validation, tier/backend constraints, stub function behaviour). |

### Modified Files

| File | Change |
|------|--------|
| `engine/renderer/rhi_types.h` | Add `RhiBackend` enum, add `window` and `debugVulkan` and `backend` fields to `RhiConfig`. |
| `engine/renderer/CMakeLists.txt` | Replace tier-based backend selection with `FFE_BACKEND`-based selection. Add volk dependency for Vulkan builds. |
| `CMakeLists.txt` (root) | Add `FFE_BACKEND` cache variable and tier/backend validation. |
| `vcpkg.json` | Add `"volk"` dependency. |
| `engine/core/application.cpp` | Add `#ifdef FFE_BACKEND_VULKAN` guards for GLFW hints and GLAD loading. Pass `window` pointer in `RhiConfig`. |
| `tests/CMakeLists.txt` | Add `test_rhi_vulkan.cpp` (conditionally, only when `FFE_BACKEND=VULKAN`). |

### Files NOT Modified

| File | Why unchanged |
|------|--------------|
| `engine/renderer/rhi.h` | The function signatures do not change. Both backends implement the same free functions. |
| `engine/renderer/opengl/rhi_opengl.cpp` | No changes. The `window` field in `RhiConfig` is ignored by OpenGL init. |
| `engine/renderer/shader_library.*` | M1 does not compile shaders. Shader cross-compilation (GLSL -> SPIR-V) is M2+ scope. |
| `engine/renderer/sprite_batch.*` | No rendering in M1. |
| All other renderer subsystems | Untouched. They call `ffe::rhi::*` functions which resolve to whichever backend is linked. |

---

## 4. Tier Mapping

| Tier | API | Min Vulkan Version | Notes |
|------|-----|-------------------|-------|
| RETRO | OpenGL 2.1 | N/A | Vulkan not available |
| LEGACY | OpenGL 3.3 | N/A | Vulkan not available. **This is the default.** |
| STANDARD | OpenGL 4.5 **or** Vulkan 1.0 | 1.0 | Developer chooses backend at build time |
| MODERN | Vulkan 1.2+ | 1.2 | OpenGL not available. Ray tracing optional. |

The default build (`-DFFE_TIER=LEGACY`) produces an OpenGL build identical to today. A developer must explicitly opt in to Vulkan with `-DFFE_BACKEND=VULKAN -DFFE_TIER=STANDARD`.

---

## 5. Consequences

### What Changes

1. **Build system gains a second axis.** `FFE_TIER` x `FFE_BACKEND` creates a matrix. M1 supports: `{RETRO,LEGACY,STANDARD} x OPENGL` and `{STANDARD,MODERN} x VULKAN`. CI must test at least `LEGACY/OPENGL` (existing) and `STANDARD/VULKAN` (new).

2. **`rhi_types.h` gains new fields.** The `RhiConfig` struct grows. All new fields have defaults that preserve existing behaviour (`backend = OPENGL`, `window = nullptr`, `debugVulkan = false`). No existing code breaks.

3. **A new vcpkg dependency: volk.** This is a small header-heavy library. It does not affect OpenGL builds (not linked).

4. **Vulkan validation layers in debug builds.** `VK_LAYER_KHRONOS_validation` adds ~5-10% overhead in debug mode but catches driver-level bugs. Disabled in Release.

### Risks

| Risk | Mitigation |
|------|-----------|
| Vulkan driver not installed on developer machine | `rhi::init()` returns `ERROR_UNSUPPORTED` with a clear log message. Default build remains OpenGL. |
| Swap chain recreation complexity (resize, minimise, alt-tab) | M1 handles `VK_ERROR_OUT_OF_DATE_KHR` with full swap chain teardown/recreate. Edge cases (zero-size framebuffer on minimise) are guarded. |
| CI environment lacks Vulkan ICD | CI runs headless. Vulkan headless mode uses the same stub path as OpenGL headless. Vulkan-specific CI (with lavapipe or Mesa's vulkan software renderer) is deferred to M2. |
| volk version compatibility | Pin via vcpkg baseline. volk tracks Vulkan headers closely and is stable. |
| `RhiConfig::window` leaks GLFW into the public header | `GLFWwindow` is already forward-declared in `rhi.h`. The config struct gains a pointer to an already-visible type. Acceptable. |

### What Is Deferred

| Item | Target Milestone |
|------|-----------------|
| Buffer/texture creation via VMA | M2 |
| Shader compilation (GLSL -> SPIR-V via shaderc or pre-compiled) | M2 |
| Vertex input / pipeline objects | M2 |
| Mesh rendering (3D) | M3 |
| Sprite batching (2D) | M3 |
| Depth buffer / depth attachment | M2 |
| Multi-threaded command recording | M4+ |
| Compute shaders | M5+ |
| Ray tracing (MODERN tier) | M6+ |
| Render graph | Phase 9+ |
| Vulkan CI with lavapipe | M2 |

---

## 6. Security Considerations

### 6.1 Validation Layers

Vulkan validation layers (`VK_LAYER_KHRONOS_validation`) are the primary security tool. They detect:

- Use-after-free of Vulkan objects
- Missing synchronisation (race conditions between GPU and CPU)
- Invalid API usage (wrong image layout, missing barriers)
- Out-of-bounds descriptor indexing

**Policy:** Validation layers are enabled in all Debug builds. They are disabled in Release builds (they are not shipped to end users -- validation layers are a development tool).

### 6.2 Driver Trust Boundary

The Vulkan driver is a trust boundary. FFE does not trust the driver to:

- Return valid memory from `vkAllocateMemory` (always check `VkResult`)
- Survive invalid API calls gracefully (validation layers catch these in debug; release builds must not make invalid calls)
- Handle malicious SPIR-V (deferred -- shader compilation is M2+ scope, but when it arrives, only engine-compiled SPIR-V will be loaded, never user-supplied SPIR-V)

### 6.3 No User-Supplied Vulkan Objects

The Vulkan backend is fully internal. No public API exposes `VkDevice`, `VkBuffer`, or any Vulkan handle to game code or Lua scripts. The RHI's opaque handle system (`BufferHandle`, `TextureHandle`, `ShaderHandle`) remains the only interface. This eliminates an entire class of use-after-free and invalid-handle attacks.

### 6.4 Swap Chain Image Index Validation

`vkAcquireNextImageKHR` returns an image index. This index is validated against `swapImageCount` before use as an array index into `swapImages[]`, `swapImageViews[]`, and `swapFramebuffers[]`. Out-of-range indices trigger `ERROR_CONTEXT_LOST` and a shutdown path, not undefined behaviour.

---

## 7. M1 Success Criteria

M1 is complete when:

1. `cmake -B build -G Ninja -DFFE_BACKEND=VULKAN -DFFE_TIER=STANDARD` configures without error
2. The Vulkan backend compiles cleanly on Clang-18 with `-Wall -Wextra`, zero warnings
3. Running the built executable opens a GLFW window and displays a solid clear colour via Vulkan
4. Resizing the window triggers swap chain recreation without crash
5. Closing the window triggers clean Vulkan shutdown (no validation layer errors)
6. The default build (`-DFFE_TIER=LEGACY`, no `FFE_BACKEND` specified) is **completely unchanged** -- same OpenGL 3.3 backend, same test suite, same behaviour
7. All existing tests (1228+) pass on the default OpenGL build
8. Headless mode works for both backends (Vulkan headless = same stub path as OpenGL)
9. `FFE_BACKEND=VULKAN` with `FFE_TIER=LEGACY` produces a clear CMake error

---

## 8. Build Commands (M1)

```bash
# Default (unchanged -- OpenGL LEGACY)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DFFE_TIER=LEGACY
cmake --build build && ctest --test-dir build --output-on-failure --parallel $(nproc)

# Vulkan STANDARD
cmake -B build-vk -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug \
    -DFFE_TIER=STANDARD -DFFE_BACKEND=VULKAN
cmake --build build-vk

# Run (opens a window with clear colour)
./build-vk/examples/headless_test/ffe_headless_test  # or a new vk_clear_demo target
```
