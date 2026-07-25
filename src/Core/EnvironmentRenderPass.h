#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"

class EnvironmentRenderPass
{
public:
    EnvironmentRenderPass(
        const LogicalDevice& logicalDevice,
        VkFormat colorFormat
    );

    ~EnvironmentRenderPass();

    EnvironmentRenderPass(
        const EnvironmentRenderPass&
    ) = delete;

    EnvironmentRenderPass& operator=(
        const EnvironmentRenderPass&
    ) = delete;

    VkRenderPass GetHandle() const
    {
        return m_RenderPass;
    }

    VkFormat GetColorFormat() const
    {
        return m_ColorFormat;
    }

private:
    const LogicalDevice& m_LogicalDevice;

    VkFormat m_ColorFormat =
        VK_FORMAT_UNDEFINED;

    VkRenderPass m_RenderPass =
        VK_NULL_HANDLE;

private:
    void CreateRenderPass();
};