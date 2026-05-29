#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>


struct SceneVertex {
    glm::vec3 position;
    static VkVertexInputBindingDescription getBindingDescription();
    static VkVertexInputAttributeDescription getAttributeDescription();
};

struct SceneGeometryData {
    std::vector<SceneVertex> vertices;
    std::vector<uint32_t> indices;
    static constexpr uint32_t CUBE_INDEX_COUNT = 6 * 2 * 3;  // 6 sides with 2 triangles
    static constexpr uint32_t GROUND_INDEX_COUNT = 2 * 3;  // 1 side made of 2 triangles

    static SceneGeometryData const& getCombinedGeometry();
    static SceneGeometryData const& getDuplicatedGeometry();

    static std::array<glm::vec3, 4> getCubePositions();

private:
    SceneGeometryData(bool duplicated);
};
