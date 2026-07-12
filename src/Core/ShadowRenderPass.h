#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"

class ShadowRenderPass
{
public:
    explicit ShadowRenderPass(
        const LogicalDevice& logicalDevice
    );

    ~ShadowRenderPass();

    ShadowRenderPass(const ShadowRenderPass&) = delete;
    ShadowRenderPass& operator=(const ShadowRenderPass&) = delete;

    VkRenderPass GetHandle() const
    {
        return m_RenderPass;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;

private:
    void CreateRenderPass();
};