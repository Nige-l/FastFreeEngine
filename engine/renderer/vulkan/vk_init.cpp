#ifdef FFE_BACKEND_VULKAN

// volk implementation -- must be defined in exactly one translation unit
#define VOLK_IMPLEMENTATION
#include "renderer/vulkan/vk_init.h"

#include <cstring>

namespace ffe::rhi::vk {

// --- Debug messenger callback ---
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    [[maybe_unused]] void* userData)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        FFE_LOG_ERROR("Vulkan-Validation", "%s", callbackData->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        FFE_LOG_WARN("Vulkan-Validation", "%s", callbackData->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        FFE_LOG_INFO("Vulkan-Validation", "%s", callbackData->pMessage);
    } else {
        FFE_LOG_TRACE("Vulkan-Validation", "%s", callbackData->pMessage);
    }
    return VK_FALSE;
}

// --- Check if a validation layer is available ---
static bool isLayerAvailable(const char* layerName) {
    u32 layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS) {
        return false;
    }
    if (layerCount == 0) return false;

    // Use a reasonable static limit to avoid heap allocation
    static constexpr u32 MAX_LAYERS = 128;
    if (layerCount > MAX_LAYERS) layerCount = MAX_LAYERS;

    VkLayerProperties layers[MAX_LAYERS];
    if (vkEnumerateInstanceLayerProperties(&layerCount, layers) != VK_SUCCESS) {
        return false;
    }

    for (u32 i = 0; i < layerCount; ++i) {
        if (std::strcmp(layers[i].layerName, layerName) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Instance Creation
// ============================================================================

VkInitResult createVulkanInstance(VulkanContext& ctx, const bool enableValidation) {
    // Initialize volk (loads vkGetInstanceProcAddr from the Vulkan loader)
    const VkResult volkResult = volkInitialize();
    if (volkResult != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "volkInitialize failed with VkResult %d", static_cast<int>(volkResult));
        return {false, "Failed to initialize volk (Vulkan loader not found?)"};
    }

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "FastFreeEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "FastFreeEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    // Gather required extensions from GLFW
    u32 glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (glfwExts == nullptr) {
        return {false, "GLFW reports no Vulkan support"};
    }

    // Build extension list (GLFW extensions + optional debug utils)
    static constexpr u32 MAX_EXTENSIONS = 16;
    const char* extensions[MAX_EXTENSIONS];
    u32 extCount = 0;

    for (u32 i = 0; i < glfwExtCount && extCount < MAX_EXTENSIONS; ++i) {
        extensions[extCount++] = glfwExts[i];
    }

    // Check if validation layer is actually available before requesting it
    const bool useValidation = enableValidation &&
        isLayerAvailable("VK_LAYER_KHRONOS_validation");

    if (enableValidation && !useValidation) {
        FFE_LOG_WARN("Vulkan", "Validation layers requested but VK_LAYER_KHRONOS_validation not available");
    }

    if (useValidation && extCount < MAX_EXTENSIONS) {
        extensions[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = extCount;
    createInfo.ppEnabledExtensionNames = extensions;

    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    if (useValidation) {
        createInfo.enabledLayerCount   = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &ctx.instance),
             (VkInitResult{false, "vkCreateInstance failed"}));

    // Load instance-level functions
    volkLoadInstance(ctx.instance);

    // Setup debug messenger if validation is active
    if (useValidation) {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        debugInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugCallback;

        VK_CHECK(vkCreateDebugUtilsMessengerEXT(ctx.instance, &debugInfo, nullptr, &ctx.debugMessenger),
                 (VkInitResult{false, "Failed to create debug messenger"}));

        ctx.debugEnabled = true;
    }

    FFE_LOG_INFO("Vulkan", "Instance created (validation: %s)", useValidation ? "enabled" : "disabled");
    return {true, ""};
}

// ============================================================================
// Physical Device Selection
// ============================================================================

VkInitResult selectPhysicalDevice(VulkanContext& ctx) {
    u32 deviceCount = 0;
    if (vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr) != VK_SUCCESS) {
        return {false, "vkEnumeratePhysicalDevices failed (count query)"};
    }
    if (deviceCount == 0) {
        return {false, "No Vulkan-capable GPU found"};
    }

    static constexpr u32 MAX_DEVICES = 8;
    if (deviceCount > MAX_DEVICES) deviceCount = MAX_DEVICES;

    VkPhysicalDevice devices[MAX_DEVICES];
    if (vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices) != VK_SUCCESS) {
        return {false, "vkEnumeratePhysicalDevices failed (device query)"};
    }

    i32 bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    u32 bestGraphicsFamily = 0;
    u32 bestPresentFamily = 0;

    for (u32 d = 0; d < deviceCount; ++d) {
        const VkPhysicalDevice candidate = devices[d];

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);

        // Find queue families
        u32 queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

        static constexpr u32 MAX_QUEUE_FAMILIES = 16;
        if (queueFamilyCount > MAX_QUEUE_FAMILIES) queueFamilyCount = MAX_QUEUE_FAMILIES;

        VkQueueFamilyProperties queueFamilies[MAX_QUEUE_FAMILIES];
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies);

        u32 graphicsIdx = UINT32_MAX;
        u32 presentIdx  = UINT32_MAX;

        for (u32 i = 0; i < queueFamilyCount; ++i) {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                graphicsIdx = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, ctx.surface, &presentSupport);
            if (presentSupport == VK_TRUE) {
                presentIdx = i;
            }

            // Prefer a family that supports both
            if (graphicsIdx == i && presentIdx == i) break;
        }

        if (graphicsIdx == UINT32_MAX || presentIdx == UINT32_MAX) continue;

        // Check swap chain support
        u32 formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, ctx.surface, &formatCount, nullptr);
        u32 presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, ctx.surface, &presentModeCount, nullptr);
        if (formatCount == 0 || presentModeCount == 0) continue;

        // Check for VK_KHR_swapchain extension support
        u32 extCount = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr);
        static constexpr u32 MAX_EXT = 256;
        if (extCount > MAX_EXT) extCount = MAX_EXT;
        VkExtensionProperties availableExts[MAX_EXT];
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, availableExts);

        bool hasSwapchain = false;
        for (u32 i = 0; i < extCount; ++i) {
            if (std::strcmp(availableExts[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
                break;
            }
        }
        if (!hasSwapchain) continue;

        // Score the device
        i32 score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 100;
        }

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(candidate, &memProps);
        for (u32 i = 0; i < memProps.memoryHeapCount; ++i) {
            if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                score += static_cast<i32>(memProps.memoryHeaps[i].size / (256ULL * 1024 * 1024));
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestDevice = candidate;
            bestGraphicsFamily = graphicsIdx;
            bestPresentFamily = presentIdx;
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        return {false, "No suitable Vulkan GPU found (missing queue families or swap chain support)"};
    }

    ctx.physicalDevice = bestDevice;
    ctx.graphicsFamily = bestGraphicsFamily;
    ctx.presentFamily  = bestPresentFamily;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(bestDevice, &props);
    FFE_LOG_INFO("Vulkan", "Selected GPU: %s (score: %d)", props.deviceName, bestScore);

    return {true, ""};
}

// ============================================================================
// Logical Device Creation
// ============================================================================

VkInitResult createLogicalDevice(VulkanContext& ctx) {
    // Queue create infos (may be 1 or 2 if graphics != present family)
    const f32 queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueInfos[2];
    u32 queueInfoCount = 0;

    VkDeviceQueueCreateInfo graphicsQueueInfo{};
    graphicsQueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphicsQueueInfo.queueFamilyIndex = ctx.graphicsFamily;
    graphicsQueueInfo.queueCount       = 1;
    graphicsQueueInfo.pQueuePriorities = &queuePriority;
    queueInfos[queueInfoCount++] = graphicsQueueInfo;

    if (ctx.presentFamily != ctx.graphicsFamily) {
        VkDeviceQueueCreateInfo presentQueueInfo{};
        presentQueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        presentQueueInfo.queueFamilyIndex = ctx.presentFamily;
        presentQueueInfo.queueCount       = 1;
        presentQueueInfo.pQueuePriorities = &queuePriority;
        queueInfos[queueInfoCount++] = presentQueueInfo;
    }

    // No optional features in M1
    VkPhysicalDeviceFeatures deviceFeatures{};

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = queueInfoCount;
    createInfo.pQueueCreateInfos       = queueInfos;
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.enabledExtensionCount   = 1;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    VK_CHECK(vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device),
             (VkInitResult{false, "vkCreateDevice failed"}));

    // Load device-level functions for optimal dispatch (skip loader trampoline)
    volkLoadDevice(ctx.device);

    vkGetDeviceQueue(ctx.device, ctx.graphicsFamily, 0, &ctx.graphicsQueue);
    vkGetDeviceQueue(ctx.device, ctx.presentFamily, 0, &ctx.presentQueue);

    FFE_LOG_INFO("Vulkan", "Logical device created (graphics family: %u, present family: %u)",
                 ctx.graphicsFamily, ctx.presentFamily);

    return {true, ""};
}

// ============================================================================
// VMA Allocator Creation
// ============================================================================

VkInitResult createAllocator(VulkanContext& ctx) {
    // VMA needs Vulkan function pointers. Since we use volk (dynamic loading),
    // we must provide the function pointers explicitly via VmaVulkanFunctions.
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
    allocatorInfo.physicalDevice   = ctx.physicalDevice;
    allocatorInfo.device           = ctx.device;
    allocatorInfo.instance         = ctx.instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    const VkResult result = vmaCreateAllocator(&allocatorInfo, &ctx.allocator);
    if (result != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "vmaCreateAllocator failed with VkResult %d", static_cast<int>(result));
        return {false, "Failed to create VMA allocator"};
    }

    FFE_LOG_INFO("Vulkan", "VMA allocator created");
    return {true, ""};
}

// ============================================================================
// Depth Format Selection (M5)
// ============================================================================

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
    // Preferred formats in order: pure depth float, depth+stencil float, depth+stencil 24-bit
    static constexpr VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (const VkFormat fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return fmt;
        }
    }

    // Fallback — D32_SFLOAT is universally supported on Vulkan 1.0 implementations
    return VK_FORMAT_D32_SFLOAT;
}

// ============================================================================
// Depth Resources (M5)
// ============================================================================

VkInitResult createDepthResources(VulkanContext& ctx) {
    if (ctx.swapchainExtent.width == 0 || ctx.swapchainExtent.height == 0) {
        return {true, ""};
    }

    ctx.depthFormat = findDepthFormat(ctx.physicalDevice);

    // Create depth image via VMA
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = ctx.depthFormat;
    imageInfo.extent.width  = ctx.swapchainExtent.width;
    imageInfo.extent.height = ctx.swapchainExtent.height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    const VkResult result = vmaCreateImage(ctx.allocator, &imageInfo, &allocInfo,
                                           &ctx.depthImage, &ctx.depthAllocation, nullptr);
    if (result != VK_SUCCESS) {
        FFE_LOG_ERROR("Vulkan", "Failed to create depth image (VkResult %d)", static_cast<int>(result));
        return {false, "Failed to create depth image"};
    }

    // Layout transition from UNDEFINED to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    // is handled implicitly by the render pass (depth attachment initialLayout = UNDEFINED,
    // finalLayout = DEPTH_STENCIL_ATTACHMENT_OPTIMAL). No explicit barrier needed.

    // Create depth image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = ctx.depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = ctx.depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    VK_CHECK(vkCreateImageView(ctx.device, &viewInfo, nullptr, &ctx.depthImageView),
             (VkInitResult{false, "Failed to create depth image view"}));

    FFE_LOG_INFO("Vulkan", "Depth resources created (%ux%u, format %d)",
                 ctx.swapchainExtent.width, ctx.swapchainExtent.height,
                 static_cast<int>(ctx.depthFormat));

    return {true, ""};
}

void destroyDepthResources(VulkanContext& ctx) {
    if (ctx.depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(ctx.device, ctx.depthImageView, nullptr);
        ctx.depthImageView = VK_NULL_HANDLE;
    }
    if (ctx.depthImage != VK_NULL_HANDLE && ctx.allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(ctx.allocator, ctx.depthImage, ctx.depthAllocation);
        ctx.depthImage      = VK_NULL_HANDLE;
        ctx.depthAllocation = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Swap Chain Creation
// ============================================================================

VkInitResult createSwapChain(VulkanContext& ctx, const u32 preferredImageCount) {
    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &capabilities),
             (VkInitResult{false, "Failed to query surface capabilities"}));

    // Skip if framebuffer is zero-size (window minimised)
    if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0) {
        FFE_LOG_WARN("Vulkan", "Zero-size framebuffer — skipping swap chain creation");
        ctx.swapImageCount = 0;
        return {true, ""};
    }

    // Choose surface format (prefer B8G8R8A8_SRGB)
    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &formatCount, nullptr);

    static constexpr u32 MAX_FORMATS = 32;
    if (formatCount > MAX_FORMATS) formatCount = MAX_FORMATS;
    VkSurfaceFormatKHR formats[MAX_FORMATS];
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &formatCount, formats);

    VkSurfaceFormatKHR chosenFormat = formats[0]; // fallback to first
    for (u32 i = 0; i < formatCount; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = formats[i];
            break;
        }
    }

    // Choose present mode
    u32 presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, nullptr);

    static constexpr u32 MAX_PRESENT_MODES = 8;
    if (presentModeCount > MAX_PRESENT_MODES) presentModeCount = MAX_PRESENT_MODES;
    VkPresentModeKHR presentModes[MAX_PRESENT_MODES];
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physicalDevice, ctx.surface, &presentModeCount, presentModes);

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
    if (!ctx.vsync) {
        // Prefer mailbox, fall back to immediate
        for (u32 i = 0; i < presentModeCount; ++i) {
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                chosenPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
            if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                chosenPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
        }
    }

    // Choose extent
    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        int fbWidth = 0;
        int fbHeight = 0;
        glfwGetFramebufferSize(ctx.window, &fbWidth, &fbHeight);
        extent.width = static_cast<u32>(fbWidth);
        extent.height = static_cast<u32>(fbHeight);

        if (extent.width < capabilities.minImageExtent.width)
            extent.width = capabilities.minImageExtent.width;
        if (extent.width > capabilities.maxImageExtent.width)
            extent.width = capabilities.maxImageExtent.width;
        if (extent.height < capabilities.minImageExtent.height)
            extent.height = capabilities.minImageExtent.height;
        if (extent.height > capabilities.maxImageExtent.height)
            extent.height = capabilities.maxImageExtent.height;
    }

    // Choose image count
    u32 imageCount = preferredImageCount;
    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount;
    }
    // minImageCount + 1 to avoid waiting on driver
    if (imageCount < capabilities.minImageCount + 1) {
        imageCount = capabilities.minImageCount + 1;
    }
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    if (imageCount > VulkanContext::MAX_SWAP_IMAGES) {
        imageCount = VulkanContext::MAX_SWAP_IMAGES;
    }

    // Create swap chain
    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface          = ctx.surface;
    swapInfo.minImageCount    = imageCount;
    swapInfo.imageFormat      = chosenFormat.format;
    swapInfo.imageColorSpace  = chosenFormat.colorSpace;
    swapInfo.imageExtent      = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const u32 queueFamilies[] = {ctx.graphicsFamily, ctx.presentFamily};
    if (ctx.graphicsFamily != ctx.presentFamily) {
        swapInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        swapInfo.queueFamilyIndexCount = 2;
        swapInfo.pQueueFamilyIndices   = queueFamilies;
    } else {
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapInfo.preTransform   = capabilities.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode    = chosenPresentMode;
    swapInfo.clipped        = VK_TRUE;
    swapInfo.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(ctx.device, &swapInfo, nullptr, &ctx.swapchain),
             (VkInitResult{false, "vkCreateSwapchainKHR failed"}));

    ctx.swapchainFormat = chosenFormat.format;
    ctx.swapchainExtent = extent;

    // Retrieve swap chain images
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &ctx.swapImageCount, nullptr);
    if (ctx.swapImageCount > VulkanContext::MAX_SWAP_IMAGES) {
        ctx.swapImageCount = VulkanContext::MAX_SWAP_IMAGES;
    }
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &ctx.swapImageCount, ctx.swapImages);

    // Create image views
    for (u32 i = 0; i < ctx.swapImageCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = ctx.swapImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = ctx.swapchainFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        VK_CHECK(vkCreateImageView(ctx.device, &viewInfo, nullptr, &ctx.swapImageViews[i]),
                 (VkInitResult{false, "Failed to create swap chain image view"}));
    }

    // Select depth format (M5)
    ctx.depthFormat = findDepthFormat(ctx.physicalDevice);

    // Create render pass with color + depth attachments (M5)
    VkAttachmentDescription attachments[2]{};

    // Attachment 0: color
    attachments[0].format         = ctx.swapchainFormat;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Attachment 1: depth
    attachments[1].format         = ctx.depthFormat;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments    = attachments;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dependency;

    VK_CHECK(vkCreateRenderPass(ctx.device, &rpInfo, nullptr, &ctx.clearPass),
             (VkInitResult{false, "Failed to create render pass"}));

    // Create depth resources (M5) — must be created before framebuffers
    {
        const VkInitResult depthResult = createDepthResources(ctx);
        if (!depthResult.success) {
            return depthResult;
        }
    }

    // Create framebuffers (color + depth attachments)
    for (u32 i = 0; i < ctx.swapImageCount; ++i) {
        const VkImageView fbAttachments[] = {ctx.swapImageViews[i], ctx.depthImageView};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = ctx.clearPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments    = fbAttachments;
        fbInfo.width           = ctx.swapchainExtent.width;
        fbInfo.height          = ctx.swapchainExtent.height;
        fbInfo.layers          = 1;

        VK_CHECK(vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &ctx.swapFramebuffers[i]),
                 (VkInitResult{false, "Failed to create framebuffer"}));
    }

    FFE_LOG_INFO("Vulkan", "Swap chain created: %ux%u, %u images, format %d",
                 extent.width, extent.height, ctx.swapImageCount, static_cast<int>(ctx.swapchainFormat));

    return {true, ""};
}

// ============================================================================
// Sync Objects
// ============================================================================

VkInitResult createSyncObjects(VulkanContext& ctx) {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so first frame doesn't wait forever

    for (u32 i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(ctx.device, &semInfo, nullptr, &ctx.imageAvailable[i]),
                 (VkInitResult{false, "Failed to create imageAvailable semaphore"}));
        VK_CHECK(vkCreateSemaphore(ctx.device, &semInfo, nullptr, &ctx.renderFinished[i]),
                 (VkInitResult{false, "Failed to create renderFinished semaphore"}));
        VK_CHECK(vkCreateFence(ctx.device, &fenceInfo, nullptr, &ctx.inFlightFences[i]),
                 (VkInitResult{false, "Failed to create in-flight fence"}));
    }

    FFE_LOG_INFO("Vulkan", "Sync objects created (%u frames in flight)", VulkanContext::MAX_FRAMES_IN_FLIGHT);
    return {true, ""};
}

// ============================================================================
// Command Buffers
// ============================================================================

VkInitResult createCommandBuffers(VulkanContext& ctx) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx.graphicsFamily;

    VK_CHECK(vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &ctx.commandPool),
             (VkInitResult{false, "Failed to create command pool"}));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = ctx.commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = VulkanContext::MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkAllocateCommandBuffers(ctx.device, &allocInfo, ctx.commandBuffers),
             (VkInitResult{false, "Failed to allocate command buffers"}));

    FFE_LOG_INFO("Vulkan", "Command pool and %u command buffers created", VulkanContext::MAX_FRAMES_IN_FLIGHT);
    return {true, ""};
}

// ============================================================================
// Swap Chain Recreation
// ============================================================================

void destroySwapChainResources(VulkanContext& ctx) {
    // Destroy depth resources before framebuffers (M5)
    destroyDepthResources(ctx);

    for (u32 i = 0; i < ctx.swapImageCount; ++i) {
        if (ctx.swapFramebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(ctx.device, ctx.swapFramebuffers[i], nullptr);
            ctx.swapFramebuffers[i] = VK_NULL_HANDLE;
        }
        if (ctx.swapImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(ctx.device, ctx.swapImageViews[i], nullptr);
            ctx.swapImageViews[i] = VK_NULL_HANDLE;
        }
        ctx.swapImages[i] = VK_NULL_HANDLE;
    }

    if (ctx.clearPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(ctx.device, ctx.clearPass, nullptr);
        ctx.clearPass = VK_NULL_HANDLE;
    }

    if (ctx.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr);
        ctx.swapchain = VK_NULL_HANDLE;
    }

    ctx.swapImageCount = 0;
}

VkInitResult recreateSwapChain(VulkanContext& ctx, const u32 preferredImageCount) {
    // Wait for device idle before destroying resources
    vkDeviceWaitIdle(ctx.device);

    destroySwapChainResources(ctx);
    return createSwapChain(ctx, preferredImageCount);
}

// ============================================================================
// Shutdown
// ============================================================================

void shutdownVulkan(VulkanContext& ctx) {
    if (ctx.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx.device);
    }

    // Command pool (implicitly frees command buffers)
    if (ctx.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
        ctx.commandPool = VK_NULL_HANDLE;
    }

    // Sync objects
    for (u32 i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ctx.renderFinished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx.device, ctx.renderFinished[i], nullptr);
        }
        if (ctx.imageAvailable[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(ctx.device, ctx.imageAvailable[i], nullptr);
        }
        if (ctx.inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(ctx.device, ctx.inFlightFences[i], nullptr);
        }
    }

    // Swap chain resources (framebuffers, render pass, image views, swap chain)
    destroySwapChainResources(ctx);

    // VMA allocator — destroyed after all buffers/textures, before device
    if (ctx.allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(ctx.allocator);
        ctx.allocator = VK_NULL_HANDLE;
    }

    // Logical device
    if (ctx.device != VK_NULL_HANDLE) {
        vkDestroyDevice(ctx.device, nullptr);
        ctx.device = VK_NULL_HANDLE;
    }

    // Debug messenger — destroyed after device (to catch teardown errors)
    // but before surface and instance
    if (ctx.debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(ctx.instance, ctx.debugMessenger, nullptr);
        ctx.debugMessenger = VK_NULL_HANDLE;
    }

    // Surface — destroyed after device, before instance
    if (ctx.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
        ctx.surface = VK_NULL_HANDLE;
    }

    // Instance
    if (ctx.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx.instance, nullptr);
        ctx.instance = VK_NULL_HANDLE;
    }

    FFE_LOG_INFO("Vulkan", "Vulkan shutdown complete");
}

} // namespace ffe::rhi::vk

#endif // FFE_BACKEND_VULKAN
