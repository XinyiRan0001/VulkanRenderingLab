#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"

class IrradianceRenderPass
{
public:
    IrradianceRenderPass(
        const LogicalDevice& logicalDevice,
        VkFormat colorFormat
    );

    ~IrradianceRenderPass();

    IrradianceRenderPass(
        const IrradianceRenderPass&
    ) = delete;

    IrradianceRenderPass& operator=(
        const IrradianceRenderPass&
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