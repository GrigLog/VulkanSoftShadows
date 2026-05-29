#include "src/App.h"

#include <iostream>

#include "VulkanHelpers.h"


App::App() : windowSystem(),
             context(windowSystem, shouldEnableValidation),
             swapchainSystem(context, windowSystem),
             sceneRenderer(context, swapchainSystem),
             sceneSimulator() {
    previousTickTime = std::chrono::steady_clock::now();
}

void App::run() {
    float fpsCounterTime = 0.0f;
    uint32_t framesThisSecond = 0;

    while (!windowSystem.shouldClose()) {
        windowSystem.pollEvents();

        std::chrono::steady_clock::time_point nowTickTime = std::chrono::steady_clock::now();
        float deltaSeconds = std::chrono::duration<float>(nowTickTime - previousTickTime).count();
        previousTickTime = nowTickTime;
        sceneSimulator.tick(deltaSeconds);
        drawFrame();

        fpsCounterTime += deltaSeconds;
        framesThisSecond++;
        if (fpsCounterTime >= 1.0f) {
            float fps = static_cast<float>(framesThisSecond) / fpsCounterTime;
            std::cout << "FPS: " << fps << '\n';
            fpsCounterTime = 0.0f;
            framesThisSecond = 0;
        }
    }

    context.waitIdle();
}

void App::drawFrame() { //todo: create proper methods in SwapchainSystem
    const uint32_t currentFrame = swapchainSystem.currentFrame;

    // wait for GPU to finish rendering
    VkFence currentFence = swapchainSystem.renderFences[swapchainSystem.currentFrame];
    VK_CHECK(vkWaitForFences(context.device, 1, &currentFence, VK_TRUE, UINT64_MAX));


    // wait for free swapchain image
    uint32_t freeImageIdx = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        context.device,
        swapchainSystem.swapchain,
        UINT64_MAX,
        swapchainSystem.swapchainImageSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &freeImageIdx
    );
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        VK_CHECK(acquireResult);


    // write render commands to the buffer and submit to the graphics queue
    sceneRenderer.updateUniformBuffer(swapchainSystem, currentFrame, sceneSimulator.state());
    sceneRenderer.recordCommandBuffer(swapchainSystem, freeImageIdx, currentFrame); //uses swapchainImages and swapchainImageViews

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &swapchainSystem.swapchainImageSemaphores[swapchainSystem.currentFrame],
        .pWaitDstStageMask = &waitStage,

        .commandBufferCount = 1,
        .pCommandBuffers = &sceneRenderer.swapchainCommandBuffers[swapchainSystem.currentFrame],

        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &swapchainSystem.renderSemaphores[swapchainSystem.currentFrame],
    };

    VK_CHECK(vkResetFences(context.device, 1, &currentFence));
    VK_CHECK(vkQueueSubmit(context.graphicsQueue, 1, &submitInfo, currentFence));


    // present the old swapchain image
    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &swapchainSystem.renderSemaphores[currentFrame],
        .swapchainCount = 1, .pSwapchains = &swapchainSystem.swapchain, .pImageIndices = &freeImageIdx,
    };
    VkResult presentResult = vkQueuePresentKHR(context.presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || windowSystem.framebufferResized) {
        windowSystem.clearFramebufferResizedFlag();
        recreateSwapchain();
        return;
    } else {
        VK_CHECK(presentResult);
    }

    swapchainSystem.advanceFrame();
}

void App::recreateSwapchain() {
    windowSystem.waitForNonZeroFramebuffer();
    context.waitIdle();

    swapchainSystem.recreate(windowSystem);
    sceneRenderer.recreateForSwapchain(swapchainSystem);
}
