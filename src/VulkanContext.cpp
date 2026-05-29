#include "src/VulkanContext.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

#include "VulkanHelpers.h"


static VkResult call_vkCreateDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerCreateInfoEXT const* createInfo,
    VkAllocationCallbacks const* allocator,
    VkDebugUtilsMessengerEXT* debugMessenger
) {
    PFN_vkCreateDebugUtilsMessengerEXT function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
    );
    if (function == nullptr)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return function(instance, createInfo, allocator, debugMessenger);
}

static void call_vkDestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    VkAllocationCallbacks const* allocator
) {
    PFN_vkDestroyDebugUtilsMessengerEXT function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
    );
    if (function != nullptr)
        function(instance, debugMessenger, allocator);
}


VulkanContext::VulkanContext(WindowSystem &windowSystem, bool enableValidation) {
    bValidation = enableValidation;
    createInstance(windowSystem.appName);
    createDebugMessenger();
    createSurface(windowSystem);
    choosePhysicalDevice();
    createLogicalDevice();
    createCommandPool();
}

VulkanContext::~VulkanContext() {
    if (device)
        vkDeviceWaitIdle(device);

    if (commandPool && device) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (surface && instance) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (debugMessenger && instance) {
        call_vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::waitIdle() {
    vkDeviceWaitIdle(device);
}

void VulkanContext::createInstance(std::string const& appName) {
    if (bValidation && !VulkanEnvironmentHelper::checkValidationLayerSupport())
        throw std::runtime_error("Validation layer is not available.");

    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = appName.c_str(),
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "NoEngine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    std::vector<const char*> requiredExtensions = getGlfwRequiredInstanceExtensions();

    VkInstanceCreateInfo instanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
        .ppEnabledExtensionNames = requiredExtensions.data()
    };

    if (bValidation) {
        auto debugMessengerInfo = VulkanInitializationHelper::makeDebugMessengerInfo();
        instanceCreateInfo.enabledLayerCount = VulkanEnvironmentHelper::VALIDATION_LAYERS.size();
        instanceCreateInfo.ppEnabledLayerNames = VulkanEnvironmentHelper::VALIDATION_LAYERS.data();
        instanceCreateInfo.pNext = &debugMessengerInfo;
    }

    VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
}

void VulkanContext::createDebugMessenger() {
    if (bValidation) {
        VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo = VulkanInitializationHelper::makeDebugMessengerInfo();
        VK_CHECK(call_vkCreateDebugUtilsMessengerEXT(instance, &debugMessengerInfo, nullptr, &debugMessenger));
    }
}

void VulkanContext::createSurface(WindowSystem& windowSystem) {
    VK_CHECK(glfwCreateWindowSurface(instance, windowSystem.window, nullptr, &surface));
}

void VulkanContext::choosePhysicalDevice() {
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    if (deviceCount == 0)
        throw std::runtime_error("No Vulkan-compatible GPU found.");

    std::vector<VkPhysicalDevice> physDevs(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, physDevs.data()));

    for (VkPhysicalDevice physDev : physDevs) {
        auto families = VulkanEnvironmentHelper::getQueueFamilies(physDev);
        auto graphicsQFI = VulkanEnvironmentHelper::getGraphicsQFI(families);
        auto presentQFI = VulkanEnvironmentHelper::getPresentQFI(families, physDev, surface);
        if (!graphicsQFI || !presentQFI)
            continue;
        if (!VulkanEnvironmentHelper::checkDynamicRenderingSupport(physDev))
            continue;
        if (!VulkanEnvironmentHelper::checkSwapchainSupport(physDev, surface))
            continue;

        physicalDevice = physDev;
        graphicsQueueFamilyIndex = graphicsQFI;
        presentQueueFamilyIndex = presentQFI;
        bRayQuery = VulkanEnvironmentHelper::checkRayTracingSupport(physDev);
        return;
    }

    throw std::runtime_error("No suitable Vulkan physical device found.");
}

void VulkanContext::createLogicalDevice() {
    std::set<uint32_t> uniqueQueueFamilies{
        graphicsQueueFamilyIndex.value(),
        presentQueueFamilyIndex.value()
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamilies.size());

    for (uint32_t familyIndex : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = familyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceVulkan13Features vulkan13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };

    VkPhysicalDeviceVulkan12Features vulkan12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    };

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
    };

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    };

    VkPhysicalDeviceFeatures2 deviceFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13Features,
    };

    std::vector<const char*> enabledDeviceExtensions = VulkanEnvironmentHelper::SWAPCHAIN_EXTENSIONS;

    if (bRayQuery) {
        enabledDeviceExtensions.insert(
            enabledDeviceExtensions.end(),
            VulkanEnvironmentHelper::RAY_TRACING_EXTENSIONS.begin(),
            VulkanEnvironmentHelper::RAY_TRACING_EXTENSIONS.end()
        );

        vulkan13Features.pNext = &vulkan12Features;
        vulkan12Features.bufferDeviceAddress = VK_TRUE;
        vulkan12Features.pNext = &accelerationStructureFeatures;
        accelerationStructureFeatures.accelerationStructure = VK_TRUE;
        accelerationStructureFeatures.pNext = &rayQueryFeatures;
        rayQueryFeatures.rayQuery = VK_TRUE;
    }

    VkDeviceCreateInfo deviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &deviceFeatures,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExtensions.size()),
        .ppEnabledExtensionNames = enabledDeviceExtensions.data(),
    };

    if (bValidation) {
        deviceCreateInfo.enabledLayerCount = VulkanEnvironmentHelper::VALIDATION_LAYERS.size();
        deviceCreateInfo.ppEnabledLayerNames = VulkanEnvironmentHelper::VALIDATION_LAYERS.data();
    }

    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

    if (bRayQuery)
        std::cout << "[Vulkan] Ray query shadows enabled." << std::endl;
    else
        std::cout << "[Vulkan] Ray query not supported. Falling back to raster-only path." << std::endl;

    vkGetDeviceQueue(device, graphicsQueueFamilyIndex.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentQueueFamilyIndex.value(), 0, &presentQueue);
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamilyIndex.value(),
    };

    VK_CHECK(vkCreateCommandPool(device, &createInfo, nullptr, &commandPool));
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags requiredProperties) {
    VkPhysicalDeviceMemoryProperties memoryProperties{
        .memoryTypeCount = 0,
    };
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if (
            (typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties
        ) {
            return i;
        }
    }

    throw std::runtime_error("No compatible memory type found.");
}

void VulkanContext::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory,
    VkMemoryAllocateFlags allocateFlags
) {
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements memoryRequirements{
        .size = 0,
    };
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties),
    };

    VkMemoryAllocateFlagsInfo allocationFlagsInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
    };
    if (allocateFlags != 0) {
        allocationFlagsInfo.flags = allocateFlags;
        allocationInfo.pNext = &allocationFlagsInfo;
    }

    VK_CHECK(vkAllocateMemory(device, &allocationInfo, nullptr, &bufferMemory));
    VK_CHECK(vkBindBufferMemory(device, buffer, bufferMemory, 0));
}

void VulkanContext::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = beginOneTimeCmdBuffer();

    VkBufferCopy copyRegion{.srcOffset = 0, .dstOffset = 0, .size = size};
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endOneTimeCmdBuffer(commandBuffer);
}

VkDeviceAddress VulkanContext::getBufferDeviceAddress(VkBuffer buffer) {
    VkBufferDeviceAddressInfo addressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer,
    };
    return vkGetBufferDeviceAddress(device, &addressInfo);
}

VkCommandBuffer VulkanContext::beginOneTimeCmdBuffer() {
    VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    return commandBuffer;
}

void VulkanContext::endOneTimeCmdBuffer(VkCommandBuffer commandBuffer) {
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(graphicsQueue));

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanContext::createImageUndefinedLayout2D(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage& outImage,
    VkDeviceMemory& outImageMemory
) {
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent {
            .width = width,
            .height = height,
            .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, // the only valid values are here are UNDEFINED, ZERO_INITIALIZED_EXT and PREINITIALIZED :(
    };
    VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &outImage));

    VkMemoryRequirements memoryRequirements{
        .size = 0,
    };
    vkGetImageMemoryRequirements(device, outImage, &memoryRequirements);

    VkMemoryAllocateInfo allocationInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties)
    };
    VK_CHECK(vkAllocateMemory(device, &allocationInfo, nullptr, &outImageMemory));
    VK_CHECK(vkBindImageMemory(device, outImage, outImageMemory, 0));
}

VkImageView VulkanContext::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    VkImageView imageView;
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
    return imageView;
}

void VulkanContext::transitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
) {
    VkCommandBuffer commandBuffer = beginOneTimeCmdBuffer();
    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    if (VulkanEnvironmentHelper::hasStencilComponent(format))
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    endOneTimeCmdBuffer(commandBuffer);
}

std::vector<const char*> VulkanContext::getGlfwRequiredInstanceExtensions() {
    uint32_t extensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (glfwExtensions == nullptr)
        throw std::runtime_error("glfwGetRequiredInstanceExtensions returned null.");

    std::vector<const char*> requiredExtensions(glfwExtensions, glfwExtensions + extensionCount);
    if (bValidation)
        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return requiredExtensions;
}
