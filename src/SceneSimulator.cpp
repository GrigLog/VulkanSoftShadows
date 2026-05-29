#include "src/SceneSimulator.h"

#include <cmath>


SceneSimulator::SceneSimulator() {
    tick(0);
}

void SceneSimulator::tick(float deltaSeconds) {
    elapsedSeconds += deltaSeconds;

    float sunAngle = elapsedSeconds * SUN_SPEED;
    simulationState.sunPosition = glm::vec3(
        std::cos(sunAngle) * 6.0f,
        4.0f,
        std::sin(sunAngle) * 6.0f
    );
}

SceneSimulationState const& SceneSimulator::state() const {
    return simulationState;
}
