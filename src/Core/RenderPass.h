#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "Swapchain.h"

class RenderPass
{
public:
    RenderPass(const LogicalDevice& logicalDevice,
        const Swapchain& swapchain, bool preserveDepth = false);

    ~RenderPass();

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    VkRenderPass GetHandle() const { return m_RenderPass; }

private:
    const LogicalDevice& m_LogicalDevice;
    const Swapchain& m_Swapchain;
    bool m_PreserveDepth = false;

    VkRenderPass m_RenderPass = VK_NULL_HANDLE;

private:
    void CreateRenderPass();
};