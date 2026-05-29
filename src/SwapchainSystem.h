#pragma once

#include <array>
#include <cstdint>

#include "src/RenderConstants.h"
#include "src/VulkanContext.h"
#include "src/WindowSystem.h"


class SwapchainSystem {
public:
    VulkanContext& context;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{
        .width = 0,
        .height = 0,
    };

    std::array<VkImage, NUM_SWAPCHAIN_IMAGES> swapchainImages{};
    std::array<VkImageView, NUM_SWAPCHAIN_IMAGES> swapchainImageViews{};
    std::array<VkSemaphore, NUM_SWAPCHAIN_IMAGES> swapchainImageSemaphores{};
    std::array<VkSemaphore, NUM_SWAPCHAIN_IMAGES> renderSemaphores{};
    std::array<VkFence, NUM_SWAPCHAIN_IMAGES> renderFences{};

    uint32_t currentFrame = 0;

public:
    SwapchainSystem(VulkanContext& context, WindowSystem& windowSystem);
    ~SwapchainSystem();

    void recreate(WindowSystem& windowSystem);

    void advanceFrame();

protected:
    static VkSurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<VkSurfaceFormatKHR> availableFormats);
    static VkPresentModeKHR chooseSwapPresentMode(std::vector<VkPresentModeKHR> availablePresentModes);
    static VkExtent2D chooseSwapExtent(VkSurfaceCapabilitiesKHR capabilities, WindowSystem& windowSystem);

    //todo: RAII wrappers around members?
    void createSwapchain(WindowSystem& windowSystem);
    void cleanupSwapchain();

    void createImageViews();
    void cleanupImageViews();

    void createSyncObjects();
    void cleanupSyncObjects();
};
