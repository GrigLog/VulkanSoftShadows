#include "src/SceneRenderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "VulkanHelpers.h"
#include "src/Scene.h"


SceneRenderer::SceneRenderer(VulkanContext& context, SwapchainSystem& swapchainSystem)
        : context(context), rayTracingSystem(context, rayQuerySupported) {
    rayQuerySupported = context.bRayQuery;
    depthImageFormat = VulkanEnvironmentHelper::getSupportedFormat(
        context.physicalDevice,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );

    createDescriptorSetLayout();
    createScenePipeline(swapchainSystem);
    createDepthImageView(swapchainSystem);
    createMeshBuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
}

SceneRenderer::~SceneRenderer() {
    cleanupSwapchainResources();
    cleanupDescriptorResources();
    cleanupUniformBuffers();
    cleanupMeshBuffers();
}

void SceneRenderer::recreateForSwapchain(SwapchainSystem& swapchainSystem) {
    cleanupSwapchainResources();
    createScenePipeline(swapchainSystem);
    createDepthImageView(swapchainSystem);
    createCommandBuffers();
}

void SceneRenderer::updateUniformBuffer(
    SwapchainSystem& swapchainSystem,
    uint32_t frameIndex,
    SceneSimulationState const& simulationState
) {
    SceneUniformBufferObject sceneUbo{};

    glm::mat4 view = glm::lookAt(POS_CAMERA, POS_CAMERA_TARGET, glm::vec3(0.0f, 1.0f, 0.0f));

    VkExtent2D extent = swapchainSystem.swapchainExtent;
    glm::mat4 projection = glm::perspective(
        glm::radians(50.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        0.1f,
        200.0f
    );
    // GLM uses OpenGL-style clip space; Vulkan expects inverted Y in clip coordinates.
    projection[1][1] *= -1.0f;

    sceneUbo.viewProjection = projection * view;
    sceneUbo.sunPosition = glm::vec4(simulationState.sunPosition, 1.0f);
    sceneUbo.model.fill(glm::mat4(1.0f));

    // Instance 0 is ground (matches draw call below with `firstInstance = 0`).
    uint32_t groundObjectIndex = 0;
    sceneUbo.model[groundObjectIndex] = glm::mat4(1.0f);
    sceneUbo.color[groundObjectIndex] = glm::vec4(1.0f);

    std::array<glm::vec3, 4> cubePositions = SceneGeometryData::getCubePositions();

    // Cube instances occupy indices 1..4 (used as `firstInstance` in the cube draw loop).
    for (uint32_t i = 0; i < cubePositions.size(); ++i) {
        uint32_t objectIndex = 1 + i;
        sceneUbo.model[objectIndex] = glm::translate(glm::mat4(1.0f), cubePositions[i]);
        sceneUbo.color[objectIndex] = glm::vec4(1.0f);
    }

    glm::mat4 sunTransform = glm::translate(glm::mat4(1.0f), simulationState.sunPosition);
    sunTransform = sunTransform * glm::scale(glm::mat4(1.0f), glm::vec3(0.35f));

    uint32_t sunObjectIndex = 5;
    sceneUbo.model[sunObjectIndex] = sunTransform;
    sceneUbo.color[sunObjectIndex] = glm::vec4(1.0f, 0.9f, 0.2f, 1.0f);

    // Keep remaining slots deterministic (even though they are not rendered for now)
    for (uint32_t i = SCENE_OBJECT_COUNT; i < MAX_SCENE_OBJECTS; ++i) {
        sceneUbo.model[i] = glm::mat4(1.0f);
        sceneUbo.color[i] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    std::memcpy(sceneUniformBuffersMapped[frameIndex], &sceneUbo, sizeof(sceneUbo));
}

void SceneRenderer::recordCommandBuffer(
    SwapchainSystem& swapchainSystem,
    uint32_t swapchainImageIndex,
    uint32_t frameIndex
) {
    VkCommandBuffer commandBufferValue = swapchainCommandBuffers[swapchainImageIndex];

    VK_CHECK(vkResetCommandBuffer(commandBufferValue, 0));

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(commandBufferValue, &beginInfo));

    VkImageMemoryBarrier barrierToColorAttachment{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainSystem.swapchainImages[swapchainImageIndex],
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(
        commandBufferValue,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrierToColorAttachment
    );

    VkImageMemoryBarrier depthToAttachment{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depthImage,
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    if (VulkanEnvironmentHelper::hasStencilComponent(depthImageFormat))
        depthToAttachment.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    vkCmdPipelineBarrier(
        commandBufferValue,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &depthToAttachment
    );

    VkClearValue clearColor{
        .color = {{0.02f, 0.02f, 0.03f, 1.0f}}
    };

    VkClearValue clearDepth{
        .depthStencil = {1.0f, 0}
    };

    VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainSystem.swapchainImageViews[swapchainImageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearColor,
    };

    VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clearDepth,
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{
            .offset = {0, 0},
            .extent = swapchainSystem.swapchainExtent
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };

    vkCmdBeginRendering(commandBufferValue, &renderingInfo);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapchainSystem.swapchainExtent.width),
        .height = static_cast<float>(swapchainSystem.swapchainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(commandBufferValue, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = swapchainSystem.swapchainExtent
    };
    vkCmdSetScissor(commandBufferValue, 0, 1, &scissor);

    vkCmdBindPipeline(commandBufferValue, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);

    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBufferValue, 0, 1, &meshVertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBufferValue, meshIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    VkDescriptorSet descriptorSet = sceneDescriptorSets[frameIndex];
    vkCmdBindDescriptorSets(
        commandBufferValue,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        scenePipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    SceneGeometryData const& geometry = SceneGeometryData::getCombinedGeometry();

    vkCmdDrawIndexed(commandBufferValue, SceneGeometryData::GROUND_INDEX_COUNT, 1, SceneGeometryData::CUBE_INDEX_COUNT, 0, 0);

    for (uint32_t cubeIndex = 0; cubeIndex < 4; ++cubeIndex) {
        vkCmdDrawIndexed(
            commandBufferValue,
            SceneGeometryData::CUBE_INDEX_COUNT,
            1,
            0,
            0,
            static_cast<int32_t>(1 + cubeIndex)
        );
    }

    vkCmdDrawIndexed(commandBufferValue, SceneGeometryData::CUBE_INDEX_COUNT, 1, 0, 0, 5);

    vkCmdEndRendering(commandBufferValue);

    VkImageMemoryBarrier barrierToPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainSystem.swapchainImages[swapchainImageIndex],
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(
        commandBufferValue,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrierToPresent
    );

    VK_CHECK(vkEndCommandBuffer(commandBufferValue));
}

void SceneRenderer::createDepthImageView(SwapchainSystem& swapchainSystem) {
    context.createImageUndefinedLayout2D(
        swapchainSystem.swapchainExtent.width,
        swapchainSystem.swapchainExtent.height,
        depthImageFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory
    );

    depthImageView = context.createImageView(depthImage, depthImageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    context.transitionImageLayout(depthImage, depthImageFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    );
}

void SceneRenderer::createDescriptorSetLayout() {
    // Binding 0: SceneUniformBufferObject, visible in vertex and fragment shaders.
    VkDescriptorSetLayoutBinding sceneUboBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };

    // Binding 1: TLAS handle for fragment-stage ray queries
    VkDescriptorSetLayoutBinding tlasBinding{
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0] = sceneUboBinding;
    bindings[1] = tlasBinding;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = rayQuerySupported ? 2u : 1u,
        .pBindings = bindings.data(),
    };

    VK_CHECK(vkCreateDescriptorSetLayout(context.device, &descriptorSetLayoutInfo, nullptr, &sceneDescriptorSetLayout));
}

void SceneRenderer::createScenePipeline(SwapchainSystem& swapchainSystem) {
    cleanupScenePipeline();

    std::vector<char> vertexShaderCode = VulkanShaderHelper::readShaderFile("shaders/scene.vert.spv");
    std::vector<char> fragmentShaderCode = VulkanShaderHelper::readShaderFile(
        rayQuerySupported ? "shaders/scene_rt.frag.spv" : "shaders/scene.frag.spv"
    );

    VkShaderModule vertexShaderModule = VulkanShaderHelper::createShaderModule(vertexShaderCode, context.device);
    VkShaderModule fragmentShaderModule = VulkanShaderHelper::createShaderModule(fragmentShaderCode, context.device);

    VkPipelineShaderStageCreateInfo vertexShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertexShaderModule,
        .pName = "main",
    };

    VkPipelineShaderStageCreateInfo fragmentShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragmentShaderModule,
        .pName = "main",
    };

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
        vertexShaderStageInfo,
        fragmentShaderStageInfo
    };

    VkVertexInputBindingDescription bindingDescription = SceneVertex::getBindingDescription();
    VkVertexInputAttributeDescription attributeDescription = SceneVertex::getAttributeDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attributeDescription,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_FALSE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sceneDescriptorSetLayout,
    };

    VK_CHECK(vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &scenePipelineLayout));

    VkFormat colorFormat = swapchainSystem.swapchainImageFormat;
    VkPipelineRenderingCreateInfo pipelineRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = depthImageFormat,
    };

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipelineRenderingInfo,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicStateInfo,
        .layout = scenePipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VK_CHECK(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &scenePipeline));

    vkDestroyShaderModule(context.device, fragmentShaderModule, nullptr);
    vkDestroyShaderModule(context.device, vertexShaderModule, nullptr);
}

void SceneRenderer::createUniformBuffers() {
    VkDeviceSize uniformBufferSize = sizeof(SceneUniformBufferObject);

    for (uint32_t i = 0; i < NUM_SWAPCHAIN_IMAGES; ++i) {
        context.createBuffer(
            uniformBufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sceneUniformBuffers[i],
            sceneUniformBufferMemories[i]
        );

        VK_CHECK(vkMapMemory(
            context.device,
            sceneUniformBufferMemories[i],
            0,
            uniformBufferSize,
            0,
            &sceneUniformBuffersMapped[i]
        ));
    }
}

void SceneRenderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = NUM_SWAPCHAIN_IMAGES;

    if (rayQuerySupported) {
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        poolSizes[1].descriptorCount = NUM_SWAPCHAIN_IMAGES;
    }

    VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = NUM_SWAPCHAIN_IMAGES,
        .poolSizeCount = rayQuerySupported ? 2u : 1u,
        .pPoolSizes = poolSizes.data(),
    };

    VK_CHECK(vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &sceneDescriptorPool));
}

void SceneRenderer::createDescriptorSets() {
    std::array<VkDescriptorSetLayout, NUM_SWAPCHAIN_IMAGES> layouts{};
    layouts.fill(sceneDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = sceneDescriptorPool,
        .descriptorSetCount = NUM_SWAPCHAIN_IMAGES,
        .pSetLayouts = layouts.data(),
    };

    VK_CHECK(vkAllocateDescriptorSets(context.device, &allocInfo, sceneDescriptorSets.data()));

    for (uint32_t i = 0; i < NUM_SWAPCHAIN_IMAGES; ++i) {
        // Descriptor range equals the full SceneUniformBufferObject payload.
        VkDescriptorBufferInfo bufferInfo{
            .buffer = sceneUniformBuffers[i],
            .offset = 0,
            .range = sizeof(SceneUniformBufferObject),
        };

        VkWriteDescriptorSet sceneUboWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = sceneDescriptorSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &bufferInfo,
            .pTexelBufferView = nullptr,
        };

        if (!rayQuerySupported) {
            vkUpdateDescriptorSets(context.device, 1, &sceneUboWrite, 0, nullptr);
            continue;
        }

        VkAccelerationStructureKHR tlasHandle = rayTracingSystem.getTopLevelAccelerationStructure();
        VkWriteDescriptorSetAccelerationStructureKHR tlasWriteInfo{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &tlasHandle,
        };

        VkWriteDescriptorSet tlasWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &tlasWriteInfo,
            .dstSet = sceneDescriptorSets[i],
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .pImageInfo = nullptr,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = sceneUboWrite;
        writes[1] = tlasWrite;
        vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void SceneRenderer::createMeshBuffers() {
    SceneGeometryData const& geometry = SceneGeometryData::getCombinedGeometry();

    VkDeviceSize vertexBufferSize = sizeof(SceneVertex) * geometry.vertices.size();
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * geometry.indices.size();

    //temporary buffer that can be accessed from CPU
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

    context.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //these 2 flags come together pretty much always...
        stagingBuffer,
        stagingBufferMemory
    );

    void* mappedData = nullptr;
    VK_CHECK(vkMapMemory(context.device, stagingBufferMemory, 0, vertexBufferSize, 0, &mappedData));
    std::memcpy(mappedData, geometry.vertices.data(), static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(context.device, stagingBufferMemory);

    context.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,  //...but for DEVICE_LOCAL we usually need another buffer in a different memory heap
        meshVertexBuffer,
        meshVertexBufferMemory
    );

    context.copyBuffer(stagingBuffer, meshVertexBuffer, vertexBufferSize);
    vkDestroyBuffer(context.device, stagingBuffer, nullptr);
    vkFreeMemory(context.device, stagingBufferMemory, nullptr);

    context.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory
    );

    VK_CHECK(vkMapMemory(context.device, stagingBufferMemory, 0, indexBufferSize, 0, &mappedData));
    std::memcpy(mappedData, geometry.indices.data(), static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(context.device, stagingBufferMemory);

    context.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        meshIndexBuffer,
        meshIndexBufferMemory
    );

    context.copyBuffer(stagingBuffer, meshIndexBuffer, indexBufferSize);
    vkDestroyBuffer(context.device, stagingBuffer, nullptr);
    vkFreeMemory(context.device, stagingBufferMemory, nullptr);
}

void SceneRenderer::createCommandBuffers() {
    if (swapchainCommandBuffers[0] != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(
            context.device,
            context.commandPool,
            NUM_SWAPCHAIN_IMAGES,
            swapchainCommandBuffers.data()
        );
        swapchainCommandBuffers.fill(VK_NULL_HANDLE);
    }

    VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = NUM_SWAPCHAIN_IMAGES,
    };

    VK_CHECK(vkAllocateCommandBuffers(context.device, &allocateInfo, swapchainCommandBuffers.data()));
}

void SceneRenderer::cleanupScenePipeline() {
    if (scenePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context.device, scenePipeline, nullptr);
        scenePipeline = VK_NULL_HANDLE;
    }

    if (scenePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context.device, scenePipelineLayout, nullptr);
        scenePipelineLayout = VK_NULL_HANDLE;
    }
}

void SceneRenderer::cleanupUniformBuffers() {
    for (uint32_t i = 0; i < NUM_SWAPCHAIN_IMAGES; ++i) {
        if (sceneUniformBuffersMapped[i] != nullptr) {
            vkUnmapMemory(context.device, sceneUniformBufferMemories[i]);
            sceneUniformBuffersMapped[i] = nullptr;
        }

        if (sceneUniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(context.device, sceneUniformBuffers[i], nullptr);
            sceneUniformBuffers[i] = VK_NULL_HANDLE;
        }

        if (sceneUniformBufferMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(context.device, sceneUniformBufferMemories[i], nullptr);
            sceneUniformBufferMemories[i] = VK_NULL_HANDLE;
        }
    }
}

void SceneRenderer::cleanupDescriptorResources() {
    if (sceneDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, sceneDescriptorPool, nullptr);
        sceneDescriptorPool = VK_NULL_HANDLE;
    }

    if (sceneDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context.device, sceneDescriptorSetLayout, nullptr);
        sceneDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void SceneRenderer::cleanupMeshBuffers() {
    if (meshIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context.device, meshIndexBuffer, nullptr);
        meshIndexBuffer = VK_NULL_HANDLE;
    }

    if (meshIndexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(context.device, meshIndexBufferMemory, nullptr);
        meshIndexBufferMemory = VK_NULL_HANDLE;
    }

    if (meshVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(context.device, meshVertexBuffer, nullptr);
        meshVertexBuffer = VK_NULL_HANDLE;
    }

    if (meshVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(context.device, meshVertexBufferMemory, nullptr);
        meshVertexBufferMemory = VK_NULL_HANDLE;
    }
}

void SceneRenderer::cleanupDepthResources() {
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(context.device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }

    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(context.device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }

    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(context.device, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }
}

void SceneRenderer::cleanupSwapchainResources() {
    if (swapchainCommandBuffers[0] != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(
            context.device,
            context.commandPool,
            NUM_SWAPCHAIN_IMAGES,
            swapchainCommandBuffers.data()
        );
        swapchainCommandBuffers.fill(VK_NULL_HANDLE);
    }

    cleanupScenePipeline();
    cleanupDepthResources();
}
