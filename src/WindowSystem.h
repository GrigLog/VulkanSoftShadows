#pragma once

#include <cstdint>
#include <string>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


class WindowSystem {
public:
    std::string appName = "Vulkan Soft Shadows";
    uint32_t windowWidth = 1600;
    uint32_t windowHeight = 900;
    GLFWwindow* window = nullptr;
    bool framebufferResized = false;

public:
    WindowSystem();
    ~WindowSystem();

    void pollEvents();
    bool shouldClose();
    void clearFramebufferResizedFlag();
    void waitForNonZeroFramebuffer();

protected:
    static void framebufferResizeCallback(GLFWwindow* windowValue, int widthValue, int heightValue);
};
