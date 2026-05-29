#include "VulkanHelpers.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "VulkanContext.h"

VKAPI_ATTR bool VulkanEnvironmentHelper::checkValidationLayerSupport() {
    uint32_t layerCount = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
    std::vector<VkLayerProperties> availableLayerProperties(layerCount);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, availableLayerProperties.data()));

    for (auto& requestedLayerName : VALIDATION_LAYERS) {
        bool foundLayer = false;
        for (const auto& availableLayer : availableLayerProperties) {
            if (std::strcmp(requestedLayerName, availableLayer.layerName) == 0) {
                foundLayer = true;
                break;
            }
        }
        if (!foundLayer)
            return false;
    }
    return true;
}

bool VulkanEnvironmentHelper::checkDeviceExtensionsSupport(VkPhysicalDevice physDev, std::vector<const char *> const &requestedExtensions) {
    uint32_t extensionCount = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, nullptr));
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extensionCount, availableExtensions.data()));

    std::set<std::string> missingExtensions{};
    for (char const* extensionName : requestedExtensions)
        missingExtensions.insert(extensionName);

    for (VkExtensionProperties const& availableExtension : availableExtensions)
        missingExtensions.erase(availableExtension.extensionName);

    return missingExtensions.empty();
}

bool VulkanEnvironmentHelper::checkRayTracingSupport(VkPhysicalDevice physDev) {
    if (!checkDeviceExtensionsSupport(physDev, RAY_TRACING_EXTENSIONS))
        return false;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    };

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &rayQueryFeatures,
    };

    VkPhysicalDeviceVulkan12Features vulkan12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &accelerationStructureFeatures,
    };

    VkPhysicalDeviceFeatures2 queriedFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan12Features,
    };
    vkGetPhysicalDeviceFeatures2(physDev, &queriedFeatures);

    return vulkan12Features.bufferDeviceAddress && accelerationStructureFeatures.accelerationStructure && rayQueryFeatures.rayQuery;
}

bool VulkanEnvironmentHelper::checkDynamicRenderingSupport(VkPhysicalDevice physDev) {
    VkPhysicalDeviceVulkan13Features queriedFeatures1{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    VkPhysicalDeviceFeatures2 queriedFeatures2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &queriedFeatures1
    };
    vkGetPhysicalDeviceFeatures2(physDev, &queriedFeatures2);
    return queriedFeatures1.dynamicRendering;
}

bool VulkanEnvironmentHelper::checkSwapchainSupport(VkPhysicalDevice physDev, VkSurfaceKHR surface) {
    if (!checkDeviceExtensionsSupport(physDev, SWAPCHAIN_EXTENSIONS))
        return false;

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &surfaceCapabilities));

    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &formatCount, nullptr));
    uint32_t presentModeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &presentModeCount, nullptr));

    return formatCount > 0 && presentModeCount > 0;

}

VkFormat VulkanEnvironmentHelper::getSupportedFormat(VkPhysicalDevice physDev, std::vector<VkFormat> candidates, VkFormatFeatureFlags requiredFeatures) {
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physDev, format, &properties);
        if ((properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures)
            return format;
    }
    throw std::runtime_error("No supported image format found.");
}

std::vector<VkQueueFamilyProperties> VulkanEnvironmentHelper::getQueueFamilies(VkPhysicalDevice physDev) {
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &familyCount, queueFamilies.data());
    return queueFamilies;
}

std::optional<int> VulkanEnvironmentHelper::getGraphicsQFI(const std::vector<VkQueueFamilyProperties> &families) {
    for (int i = 0; i < families.size(); ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            return i;
    }
    return {};
}

std::optional<int> VulkanEnvironmentHelper::getPresentQFI(const std::vector<VkQueueFamilyProperties> &families, VkPhysicalDevice physDev, VkSurfaceKHR surface) {
    for (int i = 0; i < families.size(); ++i) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physDev, i, surface, &presentSupport);
        if (presentSupport)
            return i;
    }
    return {};
}

bool VulkanEnvironmentHelper::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


VkDebugUtilsMessengerCreateInfoEXT VulkanInitializationHelper::makeDebugMessengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData = nullptr,
    };
    return createInfo;
}


std::vector<char> VulkanShaderHelper::readShaderFile(std::string const& filePath) {
    std::vector<std::filesystem::path> candidatePaths{
        std::filesystem::path(filePath),
        std::filesystem::path("../") / filePath,
        std::filesystem::path("../../") / filePath,
        std::filesystem::path("build") / filePath,
        std::filesystem::path("build/windows-debug") / filePath,
        std::filesystem::path("cmake-build-debug") / filePath
    };

    for (std::filesystem::path const& candidatePath : candidatePaths) {
        std::ifstream file(candidatePath, std::ios::ate | std::ios::binary);
        if (!file.is_open())
            continue;

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();

        return buffer;
    }

    throw std::runtime_error("Failed to open shader file: " + filePath);
}

VkShaderModule VulkanShaderHelper::createShaderModule(std::vector<char> code, VkDevice device) {
    VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<uint32_t const*>(code.data()),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}


VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT,
    VkDebugUtilsMessengerCallbackDataEXT const* callbackData,
    void*
) {
    char const* messageText = callbackData != nullptr ? callbackData->pMessage : "unknown validation message";
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "[Vulkan Validation][ERROR] " << messageText << std::endl;
        std::abort();
    }
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[Vulkan Validation][WARNING] " << messageText << std::endl;
    return VK_FALSE;
}


