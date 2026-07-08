#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "Swapchain.h"
#include "CommandManager.h"

class Renderer
{
public:
    Renderer(const LogicalDevice& logicalDevice,
        const Swapchain& swapchain,
        const CommandManager& commandManager);

    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void DrawFrame();

private:
    const LogicalDevice& m_LogicalDevice;
    const Swapchain& m_Swapchain;
    const CommandManager& m_CommandManager;

    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_InFlightFence = VK_NULL_HANDLE;

private:
    void CreateSyncObjects();
};