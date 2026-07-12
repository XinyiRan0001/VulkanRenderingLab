#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "ShadowRenderPass.h"

class ShadowFramebuffer
{
public:
    ShadowFramebuffer(
        const LogicalDevice& logicalDevice,
        const ShadowRenderPass& shadowRenderPass,
        VkImageView shadowDepthImageView,
        uint32_t width,
        uint32_t height
    );

    ~ShadowFramebuffer();

    ShadowFramebuffer(const ShadowFramebuffer&) = delete;
    ShadowFramebuffer& operator=(const ShadowFramebuffer&) = delete;

    VkFramebuffer GetHandle() const
    {
        return m_Framebuffer;
    }

private:
    const LogicalDevice& m_LogicalDevice;

    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;

private:
    void CreateFramebuffer(
        const ShadowRenderPass& shadowRenderPass,
        VkImageView shadowDepthImageView,
        uint32_t width,
        uint32_t height
    );
};