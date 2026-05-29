#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "src/Header.h"
#include "src/SceneRenderer.h"
#include "src/SceneSimulator.h"
#include "src/SwapchainSystem.h"
#include "src/VulkanContext.h"
#include "src/WindowSystem.h"


class App {
    bool shouldEnableValidation = defaultValidationEnabled && isDebugBuild;

    WindowSystem windowSystem;
    VulkanContext context;
    SwapchainSystem swapchainSystem;
    SceneRenderer sceneRenderer;
    SceneSimulator sceneSimulator;

    std::chrono::steady_clock::time_point previousTickTime;

public:
    App();
    ~App() = default;

    void run();

protected:
    void drawFrame();
    void recreateSwapchain();
};
