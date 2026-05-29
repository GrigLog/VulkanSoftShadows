#pragma once

#include <array>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "src/RayTracingSystem.h"
#include "src/RenderConstants.h"
#include "src/SceneSimulator.h"
#include "src/SwapchainSystem.h"
#include "src/VulkanContext.h"


// Must match `SceneUbo` in scene shaders at set=0, binding=0 (same field order/alignment).
struct SceneUniformBufferObject {
    // World-to-clip transform used by the vertex shader.
    glm::mat4 viewProjection{};
    // xyz = sun center in world space, w = 1. I only pass w for proper padding, actually...
    glm::vec4 sunPosition{};
    // Per-instance model transforms, addressed by draw instance index.
    std::array<glm::mat4, MAX_SCENE_OBJECTS> model{};
    // Per-instance base color
    std::array<glm::vec4, MAX_SCENE_OBJECTS> color{};
};

class SceneRenderer {
public:
    VulkanContext& context;
    bool rayQuerySupported = false;
    RayTracingSystem rayTracingSystem;

    static constexpr glm::vec3 POS_CAMERA{7.0f, 6.0f, 10.0f};
    static constexpr glm::vec3 POS_CAMERA_TARGET{0.0f, 0.8f, 0.0f};

    VkFormat depthImageFormat = VK_FORMAT_UNDEFINED;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    VkDescriptorSetLayout sceneDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout = VK_NULL_HANDLE;
    VkPipeline scenePipeline = VK_NULL_HANDLE;
    VkDescriptorPool sceneDescriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, NUM_SWAPCHAIN_IMAGES> sceneDescriptorSets{};

    // One UBO per swapchain image so each in-flight frame has its own data.
    std::array<VkBuffer, NUM_SWAPCHAIN_IMAGES> sceneUniformBuffers{};
    std::array<VkDeviceMemory, NUM_SWAPCHAIN_IMAGES> sceneUniformBufferMemories{};
    std::array<void*, NUM_SWAPCHAIN_IMAGES> sceneUniformBuffersMapped{};

    VkBuffer meshVertexBuffer = VK_NULL_HANDLE;
    VkBuffer meshIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory meshVertexBufferMemory = VK_NULL_HANDLE;
    VkDeviceMemory meshIndexBufferMemory = VK_NULL_HANDLE;

    std::array<VkCommandBuffer, NUM_SWAPCHAIN_IMAGES> swapchainCommandBuffers{};

public:
    SceneRenderer(VulkanContext& context, SwapchainSystem& swapchainSystem);
    ~SceneRenderer();

    void recreateForSwapchain(SwapchainSystem& swapchainSystem);

    void updateUniformBuffer(
        SwapchainSystem& swapchainSystem,
        uint32_t frameIndex,
        SceneSimulationState const& simulationState
    );
    void recordCommandBuffer(
        SwapchainSystem& swapchainSystem,
        uint32_t swapchainImageIndex,
        uint32_t frameIndex
    );

protected:
    void createDepthImageView(SwapchainSystem& swapchainSystem);
    void createDescriptorSetLayout();
    void createScenePipeline(SwapchainSystem& swapchainSystem);
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createMeshBuffers();
    void createCommandBuffers();

    void cleanupScenePipeline();
    void cleanupUniformBuffers();
    void cleanupDescriptorResources();
    void cleanupMeshBuffers();
    void cleanupDepthResources();
    void cleanupSwapchainResources();
};
