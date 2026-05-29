#pragma once

#include <optional>
#include <string>
#include <vector>

#include "src/Header.h"
#include "src/WindowSystem.h"


class VulkanContext {
public:
    bool bValidation = false;
    bool bRayQuery = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    std::optional<uint32_t> graphicsQueueFamilyIndex;
    std::optional<uint32_t> presentQueueFamilyIndex;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;  //sends commands to the graphics queue. Used by the swapchain

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

public:
    VulkanContext(WindowSystem& windowSystem, bool enableValidation);
    ~VulkanContext();

    void waitIdle();


    //lots of convenience methods that need VkDevice or VkLogical device to work

    void createImageUndefinedLayout2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
        VkImage& outImage, VkDeviceMemory& outImageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory,
        VkMemoryAllocateFlags allocateFlags = 0
    );
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer);

    VkCommandBuffer beginOneTimeCmdBuffer();
    void endOneTimeCmdBuffer(VkCommandBuffer commandBuffer);

protected:
    void createInstance(std::string const& appName);
    void createDebugMessenger();
    void createSurface(WindowSystem& windowSystem);
    void choosePhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags requiredProperties);

    std::vector<const char*> getGlfwRequiredInstanceExtensions();
};
