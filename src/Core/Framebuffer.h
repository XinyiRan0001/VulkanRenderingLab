#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "Swapchain.h"
#include "RenderPass.h"

#include <vector>

class Framebuffer
{
public:
    Framebuffer(const LogicalDevice& logicalDevice,
        const Swapchain& swapchain,
        const RenderPass& renderPass,
        VkImageView depthImageView);

    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    const std::vector<VkFramebuffer>& GetHandles() const { return m_Framebuffers; }

private:
    const LogicalDevice& m_LogicalDevice;
    const Swapchain& m_Swapchain;
    const RenderPass& m_RenderPass;
    VkImageView m_DepthImageView = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_Framebuffers;

private:
    void CreateFramebuffers();
};