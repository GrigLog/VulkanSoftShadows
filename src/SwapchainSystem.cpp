#include "src/SwapchainSystem.h"

#include <algorithm>

#include "VulkanHelpers.h"


SwapchainSystem::SwapchainSystem(VulkanContext &context, WindowSystem &windowSystem)
        : context(context) {
    createSwapchain(windowSystem);
    createImageViews();
    createSyncObjects();
}

SwapchainSystem::~SwapchainSystem() {
    cleanupSwapchain();
    cleanupImageViews();
    cleanupSyncObjects();
}

void SwapchainSystem::recreate(WindowSystem& windowSystem) {
    cleanupSwapchain();
    cleanupImageViews();
    cleanupSyncObjects();
    createSwapchain(windowSystem);
    createImageViews();
    createSyncObjects();
    currentFrame = 0;
}

void SwapchainSystem::advanceFrame() {
    currentFrame = (currentFrame + 1) % NUM_SWAPCHAIN_IMAGES;
}

VkSurfaceFormatKHR SwapchainSystem::chooseSwapSurfaceFormat(std::vector<VkSurfaceFormatKHR> availableFormats) {
    for (VkSurfaceFormatKHR availableFormat : availableFormats) {
        if (
            availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        ) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR SwapchainSystem::chooseSwapPresentMode(std::vector<VkPresentModeKHR> availablePresentModes) {
    for (VkPresentModeKHR presentModeValue : availablePresentModes) {
        if (presentModeValue == VK_PRESENT_MODE_MAILBOX_KHR) //recommended by https://vkguide.dev/docs/new_chapter_1/vulkan_init_flow/
            return presentModeValue;
    }

    return VK_PRESENT_MODE_IMMEDIATE_KHR; // no delays and a lot of tearing (should not be an issue)
}

VkExtent2D SwapchainSystem::chooseSwapExtent(VkSurfaceCapabilitiesKHR capabilities, WindowSystem& windowSystem) {
    if (capabilities.currentExtent.width != UINT32_MAX)
        return capabilities.currentExtent;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(windowSystem.window, &framebufferWidth, &framebufferHeight);

    VkExtent2D resultExtent{
        .width = static_cast<uint32_t>(framebufferWidth),
        .height = static_cast<uint32_t>(framebufferHeight),
    };
    resultExtent.width = std::clamp(
        resultExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );
    resultExtent.height = std::clamp(
        resultExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    return resultExtent;
}

void SwapchainSystem::createSwapchain(WindowSystem& windowSystem) {
    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice, context.surface, &surfaceCapabilities));

    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, surfaceFormats.data()));

    uint32_t presentModeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, nullptr));
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, presentModes.data()));

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(surfaceFormats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    VkExtent2D swapExtent = chooseSwapExtent(surfaceCapabilities, windowSystem);

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = swapExtent;

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = context.surface,
        .minImageCount = NUM_SWAPCHAIN_IMAGES, // I don't think I need more than 2
        .imageFormat = swapchainImageFormat,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE
    };

    uint32_t graphicsFamily = context.graphicsQueueFamilyIndex.value();
    uint32_t presentFamily = context.presentQueueFamilyIndex.value();
    uint32_t queueFamilyIndices[] = {graphicsFamily, presentFamily};

    if (graphicsFamily != presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(context.device, &createInfo, nullptr, &swapchain));

    uint32_t swapchainImageCount = NUM_SWAPCHAIN_IMAGES;
    VkResult result = vkGetSwapchainImagesKHR(context.device, swapchain, &swapchainImageCount, swapchainImages.data());
    if (result != VK_INCOMPLETE) //It's fine if we don't use all the available images
        VK_CHECK(result);
}

void SwapchainSystem::cleanupSwapchain() {
    swapchainImages.fill(VK_NULL_HANDLE);
    if (swapchain) {
        vkDestroySwapchainKHR(context.device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void SwapchainSystem::createImageViews() {
    for (size_t i = 0; i < NUM_SWAPCHAIN_IMAGES; ++i)
        swapchainImageViews[i] = context.createImageView(swapchainImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SwapchainSystem::cleanupImageViews() {
    for (VkImageView& imageViewValue : swapchainImageViews) {
        if (imageViewValue) {
            vkDestroyImageView(context.device, imageViewValue, nullptr);
            imageViewValue = VK_NULL_HANDLE;
        }
    }
}

void SwapchainSystem::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < NUM_SWAPCHAIN_IMAGES; ++i) {
        VK_CHECK(vkCreateSemaphore(context.device, &semaphoreCreateInfo, nullptr, &swapchainImageSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(context.device, &semaphoreCreateInfo, nullptr, &renderSemaphores[i]));
        VK_CHECK(vkCreateFence(context.device, &fenceCreateInfo, nullptr, &renderFences[i]));
    }
}

void SwapchainSystem::cleanupSyncObjects() {
    for (VkFence& fence : renderFences) {
        if (fence)
            vkDestroyFence(context.device, fence, nullptr);\
        fence = VK_NULL_HANDLE;
    }

    for (VkSemaphore& sem : renderSemaphores) {
        if (sem)
            vkDestroySemaphore(context.device, sem, nullptr);
        sem = VK_NULL_HANDLE;
    }

    for (VkSemaphore& sem : swapchainImageSemaphores) {
        if (sem)
            vkDestroySemaphore(context.device, sem, nullptr);
        sem = VK_NULL_HANDLE;
    }
}
