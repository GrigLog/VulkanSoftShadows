#include "src/RayTracingSystem.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

#include "VulkanHelpers.h"
#include "src/Scene.h"


static VkDeviceAddress alignDeviceAddress(VkDeviceAddress address, VkDeviceSize alignment) {
    // Scratch addresses must satisfy device alignment requirements for AS builds.
    if (alignment == 0)
        return address;

    VkDeviceAddress mask = alignment - 1;
    return (address + mask) & ~mask;
}

//todo: pay attention to the fact that ray queries are not supported lol
RayTracingSystem::RayTracingSystem(VulkanContext &context, bool supported)
        : context(context) {
    loadFunctions();
    createGeometryBuffers();
    buildAccelerationStructures();
}

RayTracingSystem::~RayTracingSystem() {
    if (topLevelScratchBuffer) {
        vkDestroyBuffer(context.device, topLevelScratchBuffer, nullptr);
        topLevelScratchBuffer = VK_NULL_HANDLE;
    }

    if (topLevelScratchBufferMemory) {
        vkFreeMemory(context.device, topLevelScratchBufferMemory, nullptr);
        topLevelScratchBufferMemory = VK_NULL_HANDLE;
    }

    if (bottomLevelScratchBuffer) {
        vkDestroyBuffer(context.device, bottomLevelScratchBuffer, nullptr);
        bottomLevelScratchBuffer = VK_NULL_HANDLE;
    }

    if (bottomLevelScratchBufferMemory) {
        vkFreeMemory(context.device, bottomLevelScratchBufferMemory, nullptr);
        bottomLevelScratchBufferMemory = VK_NULL_HANDLE;
    }

    if (instanceBuffer) {
        vkDestroyBuffer(context.device, instanceBuffer, nullptr);
        instanceBuffer = VK_NULL_HANDLE;
    }

    if (instanceBufferMemory) {
        vkFreeMemory(context.device, instanceBufferMemory, nullptr);
        instanceBufferMemory = VK_NULL_HANDLE;
    }

    cleanupAccelerationStructure(topLevelAccelerationStructure);
    cleanupAccelerationStructure(bottomLevelAccelerationStructure);

    cleanupGeometryBuffers();
}


VkAccelerationStructureKHR RayTracingSystem::getTopLevelAccelerationStructure() {
    return topLevelAccelerationStructure.handle;
}

void RayTracingSystem::loadFunctions() {
    // KHR acceleration-structure APIs are provided as device-level extension entry points.
    fun_vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(context.device, "vkCreateAccelerationStructureKHR")
    );
    fun_vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(context.device, "vkDestroyAccelerationStructureKHR")
    );
    fun_vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(context.device, "vkGetAccelerationStructureBuildSizesKHR")
    );
    fun_vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(context.device, "vkCmdBuildAccelerationStructuresKHR")
    );
    fun_vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(context.device, "vkGetAccelerationStructureDeviceAddressKHR")
    );

    if (fun_vkCreateAccelerationStructureKHR == nullptr ||
        fun_vkDestroyAccelerationStructureKHR == nullptr ||
        fun_vkGetAccelerationStructureBuildSizesKHR == nullptr ||
        fun_vkCmdBuildAccelerationStructuresKHR == nullptr ||
        fun_vkGetAccelerationStructureDeviceAddressKHR == nullptr)
    {
        throw std::runtime_error("Failed to load ray tracing device functions.");
    }
}

void RayTracingSystem::createGeometryBuffers() {
    const SceneGeometryData& sceneGeometry = SceneGeometryData::getDuplicatedGeometry();
    std::vector<uint32_t> indices = sceneGeometry.indices;
    std::vector<glm::vec3> vertexPositions(sceneGeometry.vertices.size());
    for (int i = 0; i < sceneGeometry.vertices.size(); ++i)
        vertexPositions[i] = sceneGeometry.vertices[i].position;

    sceneVertexCount = static_cast<uint32_t>(vertexPositions.size());
    sceneIndexCount = static_cast<uint32_t>(indices.size());

    VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(sizeof(glm::vec3) * vertexPositions.size());
    VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(sizeof(uint32_t) * indices.size());

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    void* mappedData = nullptr;

    // Upload vertices through a host-visible staging buffer, then copy to device-local memory
    context.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    VK_CHECK(vkMapMemory(context.device, stagingBufferMemory, 0, vertexBufferSize, 0, &mappedData));
    std::memcpy(mappedData, vertexPositions.data(), static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(context.device, stagingBufferMemory);

    context.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        geometryVertexBuffer,
        geometryVertexMemory,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    );

    context.copyBuffer(stagingBuffer, geometryVertexBuffer, vertexBufferSize);
    vkDestroyBuffer(context.device, stagingBuffer, nullptr);
    vkFreeMemory(context.device, stagingBufferMemory, nullptr);

    // Repeat the same upload path for index data
    context.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    VK_CHECK(vkMapMemory(context.device, stagingBufferMemory, 0, indexBufferSize, 0, &mappedData));
    std::memcpy(mappedData, indices.data(), static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(context.device, stagingBufferMemory);

    context.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        geometryIndexBuffer,
        geometryIndexMemory,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    );

    context.copyBuffer(stagingBuffer, geometryIndexBuffer, indexBufferSize);
    vkDestroyBuffer(context.device, stagingBuffer, nullptr);
    vkFreeMemory(context.device, stagingBufferMemory, nullptr);
}

void RayTracingSystem::cleanupGeometryBuffers() {
    if (geometryIndexBuffer) {
        vkDestroyBuffer(context.device, geometryIndexBuffer, nullptr);
        geometryIndexBuffer = VK_NULL_HANDLE;
    }
    if (geometryIndexMemory) {
        vkFreeMemory(context.device, geometryIndexMemory, nullptr);
        geometryIndexMemory = VK_NULL_HANDLE;
    }
    if (geometryVertexBuffer) {
        vkDestroyBuffer(context.device, geometryVertexBuffer, nullptr);
        geometryVertexBuffer = VK_NULL_HANDLE;
    }
    if (geometryVertexMemory) {
        vkFreeMemory(context.device, geometryVertexMemory, nullptr);
        geometryVertexMemory = VK_NULL_HANDLE;
    }
}

void RayTracingSystem::buildAccelerationStructures() {
    VkDeviceAddress vertexAddress = context.getBufferDeviceAddress(geometryVertexBuffer);
    VkDeviceAddress indexAddress = context.getBufferDeviceAddress(geometryIndexBuffer);

    // BLAS geometry references the world-space triangle buffers directly via device addresses.
    VkAccelerationStructureGeometryTrianglesDataKHR bottomTriangles{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData{.deviceAddress = vertexAddress},
        .vertexStride = sizeof(glm::vec3),
        .maxVertex = sceneVertexCount - 1,
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData{.deviceAddress = indexAddress},
        .transformData{}
    };

    VkAccelerationStructureGeometryKHR bottomGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry{.triangles = bottomTriangles},
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };

    VkAccelerationStructureBuildGeometryInfoKHR bottomBuildInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &bottomGeometry,
    };

    uint32_t bottomPrimitiveCount = sceneIndexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR bottomSizeInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    fun_vkGetAccelerationStructureBuildSizesKHR(
        context.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &bottomBuildInfo,
        &bottomPrimitiveCount,
        &bottomSizeInfo
    );

    createAccelerationStructure(
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        bottomSizeInfo.accelerationStructureSize,
        bottomLevelAccelerationStructure
    );

    context.createBuffer(
        std::max<VkDeviceSize>(1, bottomSizeInfo.buildScratchSize),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        bottomLevelScratchBuffer,
        bottomLevelScratchBufferMemory,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    );

    VkAccelerationStructureDeviceAddressInfoKHR bottomAddressInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = bottomLevelAccelerationStructure.handle,
    };
    VkDeviceAddress bottomLevelAddress = fun_vkGetAccelerationStructureDeviceAddressKHR(context.device, &bottomAddressInfo);

    // TLAS contains one instance that points at the BLAS and uses an identity transform.
    VkAccelerationStructureInstanceKHR instance{
        .instanceCustomIndex = 0,
        .mask = 0xFF,
        .instanceShaderBindingTableRecordOffset = 0,
        .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = bottomLevelAddress,
    };
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    context.createBuffer(
        sizeof(instance),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    void* mappedData = nullptr;
    VK_CHECK(vkMapMemory(context.device, stagingBufferMemory, 0, sizeof(instance), 0, &mappedData));
    std::memcpy(mappedData, &instance, sizeof(instance));
    vkUnmapMemory(context.device, stagingBufferMemory);

    context.createBuffer(
        sizeof(instance),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        instanceBuffer,
        instanceBufferMemory,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    );

    context.copyBuffer(stagingBuffer, instanceBuffer, sizeof(instance));
    vkDestroyBuffer(context.device, stagingBuffer, nullptr);
    vkFreeMemory(context.device, stagingBufferMemory, nullptr);

    // TLAS geometry is a list of instances rather than raw triangles
    VkAccelerationStructureGeometryInstancesDataKHR topInstances{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
    };
    topInstances.data.deviceAddress = context.getBufferDeviceAddress(instanceBuffer);

    VkAccelerationStructureGeometryKHR topGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };
    topGeometry.geometry.instances = topInstances;

    VkAccelerationStructureBuildGeometryInfoKHR topBuildInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &topGeometry,
    };

    uint32_t topPrimitiveCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR topSizeInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    fun_vkGetAccelerationStructureBuildSizesKHR(
        context.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &topBuildInfo,
        &topPrimitiveCount,
        &topSizeInfo
    );

    createAccelerationStructure(
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        topSizeInfo.accelerationStructureSize,
        topLevelAccelerationStructure
    );

    context.createBuffer(
        std::max<VkDeviceSize>(1, topSizeInfo.buildScratchSize),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        topLevelScratchBuffer,
        topLevelScratchBufferMemory,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    );

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
    };

    VkPhysicalDeviceProperties2 properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &accelerationProperties,
    };
    vkGetPhysicalDeviceProperties2(context.physicalDevice, &properties);

    VkDeviceAddress bottomScratchAddress = context.getBufferDeviceAddress(bottomLevelScratchBuffer);
    VkDeviceAddress topScratchAddress = context.getBufferDeviceAddress(topLevelScratchBuffer);

    VkDeviceAddress alignedBottomScratchAddress = alignDeviceAddress(
        bottomScratchAddress,
        accelerationProperties.minAccelerationStructureScratchOffsetAlignment
    );
    VkDeviceAddress alignedTopScratchAddress = alignDeviceAddress(
        topScratchAddress,
        accelerationProperties.minAccelerationStructureScratchOffsetAlignment
    );

    bottomBuildInfo.dstAccelerationStructure = bottomLevelAccelerationStructure.handle;
    bottomBuildInfo.scratchData.deviceAddress = alignedBottomScratchAddress;

    topBuildInfo.dstAccelerationStructure = topLevelAccelerationStructure.handle;
    topBuildInfo.scratchData.deviceAddress = alignedTopScratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR bottomRangeInfo{
        .primitiveCount = bottomPrimitiveCount,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };

    VkAccelerationStructureBuildRangeInfoKHR topRangeInfo{
        .primitiveCount = topPrimitiveCount,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };

    VkAccelerationStructureBuildRangeInfoKHR const* bottomRangeInfos[] = {&bottomRangeInfo};
    VkAccelerationStructureBuildRangeInfoKHR const* topRangeInfos[] = {&topRangeInfo};

    VkCommandBuffer commandBuffer = context.beginOneTimeCmdBuffer();
    // Build BLAS first, then make its writes visible before TLAS build consumes it
    fun_vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &bottomBuildInfo, bottomRangeInfos);

    VkMemoryBarrier bottomToTopBarrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0,
        1,
        &bottomToTopBarrier,
        0,
        nullptr,
        0,
        nullptr
    );

    fun_vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &topBuildInfo, topRangeInfos);

    // Ensure completed AS writes are visible to fragment-stage ray queries.
    VkMemoryBarrier buildToFragmentBarrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        1,
        &buildToFragmentBarrier,
        0,
        nullptr,
        0,
        nullptr
    );

    context.endOneTimeCmdBuffer(commandBuffer);
}

void RayTracingSystem::createAccelerationStructure(
    VkAccelerationStructureTypeKHR type,
    VkDeviceSize size,
    RayTracingAccelerationStructure& accelerationStructure
) {
    // Create storage first, then bind that storage when creating the AS handle.
    context.createBuffer(
        size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        accelerationStructure.buffer,
        accelerationStructure.memory,
        VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    );

    VkAccelerationStructureCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = accelerationStructure.buffer,
        .offset = 0,
        .size = size,
        .type = type,
    };

    VK_CHECK(fun_vkCreateAccelerationStructureKHR(context.device, &createInfo, nullptr, &accelerationStructure.handle));
}

void RayTracingSystem::cleanupAccelerationStructure(
    RayTracingAccelerationStructure& accelerationStructure
) {
    if (accelerationStructure.handle) {
        fun_vkDestroyAccelerationStructureKHR(context.device, accelerationStructure.handle, nullptr);
        accelerationStructure.handle = VK_NULL_HANDLE;
    }

    if (accelerationStructure.buffer) {
        vkDestroyBuffer(context.device, accelerationStructure.buffer, nullptr);
        accelerationStructure.buffer = VK_NULL_HANDLE;
    }

    if (accelerationStructure.memory) {
        vkFreeMemory(context.device, accelerationStructure.memory, nullptr);
        accelerationStructure.memory = VK_NULL_HANDLE;
    }
}
