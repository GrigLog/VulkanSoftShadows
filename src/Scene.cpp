#include "src/Scene.h"

#include <array>
#include <cstddef>

VkVertexInputBindingDescription SceneVertex::getBindingDescription() {
    VkVertexInputBindingDescription description{
        .binding = 0,
        .stride = sizeof(SceneVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    return description;
}

VkVertexInputAttributeDescription SceneVertex::getAttributeDescription() {
    VkVertexInputAttributeDescription description{
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<uint32_t>(offsetof(SceneVertex, position)),
    };
    return description;
}

SceneGeometryData const& SceneGeometryData::getCombinedGeometry() {
    static SceneGeometryData const combinedGeometry(false);
    return combinedGeometry;
}

SceneGeometryData const & SceneGeometryData::getDuplicatedGeometry() {
    static SceneGeometryData const duplicatedGeometry(true);
    return duplicatedGeometry;
}

SceneGeometryData::SceneGeometryData(bool duplicated) {
    const std::vector<SceneVertex> VERTEX_CUBE{
        {{-0.5f, -0.5f, -0.5f}},
        {{0.5f, -0.5f, -0.5f}},
        {{0.5f, 0.5f, -0.5f}},
        {{-0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, 0.5f}},
        {{0.5f, -0.5f, 0.5f}},
        {{0.5f, 0.5f, 0.5f}},
        {{-0.5f, 0.5f, 0.5f}}
    };

    const std::array<uint32_t, CUBE_INDEX_COUNT> TRIANGLE_INDEX_CUBE{
        0, 1, 2,   2, 3, 0,
        4, 5, 6,   6, 7, 4,
        0, 4, 7,   7, 3, 0,
        6, 5, 1,   1, 2, 6,
        6, 2, 3,   3, 7, 6,
        0, 1, 5,   5, 4, 0
    };

    const std::vector<SceneVertex> VERTEX_GROUND{
        {{-20.0f, 0.0f, -20.0f}},
        {{20.0f, 0.0f, -20.0f}},
        {{20.0f, 0.0f, 20.0f}},
        {{-20.0f, 0.0f, 20.0f}}
    };

    const std::array<uint32_t, GROUND_INDEX_COUNT> TRIANGLE_INDEX_GROUND{2, 1, 0,   0, 3, 2};


    if (!duplicated) { //only write data for 1 cube, the rest will be handled by SceneRenderer code
        vertices.insert(vertices.end(), VERTEX_CUBE.begin(), VERTEX_CUBE.end());
        vertices.insert(vertices.end(), VERTEX_GROUND.begin(), VERTEX_GROUND.end());

        indices.insert(indices.end(), TRIANGLE_INDEX_CUBE.begin(), TRIANGLE_INDEX_CUBE.end());
        for (uint32_t index : TRIANGLE_INDEX_GROUND)
            indices.push_back(VERTEX_CUBE.size() + index);
    } else {  //write data for all cubes to cast ray queries
        for (glm::vec3 cubePosition : getCubePositions()) {
            uint32_t baseIndex = vertices.size();
            for (uint32_t i = 0; i < 8; ++i) {
                glm::vec3 transformedPosition = VERTEX_CUBE[i].position + cubePosition;
                vertices.emplace_back(transformedPosition);
            }
            for (uint32_t i = 0; i < CUBE_INDEX_COUNT; ++i) {
                indices.push_back(baseIndex + TRIANGLE_INDEX_CUBE[i]);
            }
        }
        uint32_t groundFirstIndex = vertices.size();

        vertices.insert(vertices.end(), VERTEX_GROUND.begin(), VERTEX_GROUND.end());
        for (uint32_t i = 0; i < GROUND_INDEX_COUNT; ++i) {
            indices.push_back(groundFirstIndex + TRIANGLE_INDEX_GROUND[i]);
        }
    }
}

std::array<glm::vec3, 4> SceneGeometryData::getCubePositions() {
    return {
        glm::vec3(-2.0f, 1.0f, -2.0f),
        glm::vec3(2.0f, 1.0f, -2.0f),
        glm::vec3(-2.0f, 1.0f, 2.0f),
        glm::vec3(2.0f, 1.0f, 2.0f)
    };
}
