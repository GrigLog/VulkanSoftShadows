#include "src/WindowSystem.h"

#include <stdexcept>


WindowSystem::WindowSystem() {
    if (glfwInit() == GLFW_FALSE)
        throw std::runtime_error("GLFW initialization failed.");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(
        static_cast<int>(windowWidth),
        static_cast<int>(windowHeight),
        appName.c_str(),
        nullptr,
        nullptr
    );

    if (window == nullptr)
        throw std::runtime_error("GLFW window creation failed.");

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, WindowSystem::framebufferResizeCallback);
}

WindowSystem::~WindowSystem() {
    if (window != nullptr) {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    glfwTerminate();
}

void WindowSystem::pollEvents() {
    glfwPollEvents();
}

bool WindowSystem::shouldClose() {
    return glfwWindowShouldClose(window);
}

void WindowSystem::clearFramebufferResizedFlag() {
    framebufferResized = false;
}

void WindowSystem::waitForNonZeroFramebuffer() {
    int framebufferWidth = 0;
    int framebufferHeight = 0;

    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    while (framebufferWidth == 0 || framebufferHeight == 0) {
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glfwWaitEvents();
    }
}

void WindowSystem::framebufferResizeCallback(GLFWwindow* windowValue, int, int) {
    WindowSystem* windowSystem = static_cast<WindowSystem*>(glfwGetWindowUserPointer(windowValue));
    if (windowSystem != nullptr)
        windowSystem->framebufferResized = true;
}
