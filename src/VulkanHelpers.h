#pragma once

#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>


#define VK_CHECK(call)                                                                    \
    do {                                                                                  \
        const VkResult vkCheckResult = (call);                                            \
        if (vkCheckResult != VK_SUCCESS) {                                                \
            throw std::runtime_error("Vulkan error: " + std::to_string(vkCheckResult));   \
        }                                                                                 \
    } while (0)


//Boilerplate Vulkan functions
namespace VulkanEnvironmentHelper {
    const std::vector<const char*> VALIDATION_LAYERS{"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> RAY_TRACING_EXTENSIONS{
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };
    const std::vector<const char*> SWAPCHAIN_EXTENSIONS{VK_KHR_SWAPCHAIN_EXTENSION_NAME};


    bool checkValidationLayerSupport();

    bool checkDeviceExtensionsSupport(VkPhysicalDevice physDev, std::vector<const char*> const& requestedExtensions);
    bool checkRayTracingSupport(VkPhysicalDevice physDev);
    bool checkDynamicRenderingSupport(VkPhysicalDevice physDev);
    bool checkSwapchainSupport(VkPhysicalDevice physDev, VkSurfaceKHR surface);

    VkFormat getSupportedFormat(VkPhysicalDevice physDev, std::vector<VkFormat> candidates, VkFormatFeatureFlags requiredFeatures);

    std::vector<VkQueueFamilyProperties> getQueueFamilies(VkPhysicalDevice physDev);
    std::optional<int> getGraphicsQFI(const std::vector<VkQueueFamilyProperties>& families);
    std::optional<int> getPresentQFI(const std::vector<VkQueueFamilyProperties>& families, VkPhysicalDevice physDev, VkSurfaceKHR surface);

    bool hasStencilComponent(VkFormat format);
}

//Shorthands for structure initializations
namespace VulkanInitializationHelper {
    VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerInfo();
}

namespace VulkanShaderHelper {
    std::vector<char> readShaderFile(std::string const& filePath);
    VkShaderModule createShaderModule(std::vector<char> code, VkDevice device);
}


static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT,
    VkDebugUtilsMessengerCallbackDataEXT const* callbackData,
    void*
);