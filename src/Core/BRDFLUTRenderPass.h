#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class LogicalDevice;

class BRDFLUTRenderPass
{
public:
    BRDFLUTRenderPass(
        const LogicalDevice& logicalDevice,
        VkFormat colorFormat
    );

    ~BRDFLUTRenderPass();

    BRDFLUTRenderPass(
        const BRDFLUTRenderPass&
    ) = delete;

    BRDFLUTRenderPass& operator=(
        const BRDFLUTRenderPass&
    ) = delete;

    VkRenderPass GetHandle() const
    {
        return m_RenderPass;
    }

private:
    const LogicalDevice& m_LogicalDevice;

    VkRenderPass m_RenderPass =
        VK_NULL_HANDLE;

private:
    void CreateRenderPass(
        VkFormat colorFormat
    );
};