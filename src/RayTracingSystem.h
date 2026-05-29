#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "src/VulkanContext.h"


// Vulkan AS object + buffer/memory that stores its internal data
struct RayTracingAccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// see https://www.mathematik.uni-marburg.de/~thormae/lectures/graphics2/graphics_2_1_eng_web.html#1 for some context
class RayTracingSystem {
public:
    VulkanContext& context;

    // Static scene geometry used as Acceleration Structure build input.
    VkBuffer geometryVertexBuffer = VK_NULL_HANDLE;
    VkBuffer geometryIndexBuffer = VK_NULL_HANDLE;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkBuffer bottomLevelScratchBuffer = VK_NULL_HANDLE;
    VkBuffer topLevelScratchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory geometryVertexMemory = VK_NULL_HANDLE;
    VkDeviceMemory geometryIndexMemory = VK_NULL_HANDLE;
    VkDeviceMemory instanceBufferMemory = VK_NULL_HANDLE;
    VkDeviceMemory bottomLevelScratchBufferMemory = VK_NULL_HANDLE;
    VkDeviceMemory topLevelScratchBufferMemory = VK_NULL_HANDLE;

    uint32_t sceneVertexCount = 0;
    uint32_t sceneIndexCount = 0;

    RayTracingAccelerationStructure bottomLevelAccelerationStructure{};
    RayTracingAccelerationStructure topLevelAccelerationStructure{};

    PFN_vkCreateAccelerationStructureKHR fun_vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR fun_vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR fun_vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR fun_vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR fun_vkGetAccelerationStructureDeviceAddressKHR = nullptr;

public:
    RayTracingSystem(VulkanContext& context, bool supported);
    ~RayTracingSystem();

    VkAccelerationStructureKHR getTopLevelAccelerationStructure();

protected:
    void loadFunctions();
    // Uploads world-space occluder geometry into device-local buffers
    void createGeometryBuffers();
    void cleanupGeometryBuffers();

    // Builds BLAS from triangle data and TLAS from a single BLAS instance
    void buildAccelerationStructures();
    void createAccelerationStructure(
        VkAccelerationStructureTypeKHR type,
        VkDeviceSize size,
        RayTracingAccelerationStructure& accelerationStructure
    );
    void cleanupAccelerationStructure(RayTracingAccelerationStructure& accelerationStructure);
};
