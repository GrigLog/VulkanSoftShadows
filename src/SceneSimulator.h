#pragma once

#include <glm/vec3.hpp>


struct SceneSimulationState {
    glm::vec3 sunPosition;
};

class SceneSimulator {
protected:
    static constexpr float SUN_SPEED = 0.5f;
    float elapsedSeconds = 0.0f;
    SceneSimulationState simulationState{};

public:
    SceneSimulator();
    void tick(float deltaSeconds);
    SceneSimulationState const& state() const;
};
