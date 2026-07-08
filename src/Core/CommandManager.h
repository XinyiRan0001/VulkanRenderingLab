#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include "Framebuffer.h"
#include "RenderPass.h"
#include "Swapchain.h"

#include <vector>

class CommandManager
{
public:
    CommandManager(const LogicalDevice& logicalDevice,
        const PhysicalDevice& physicalDevice,
        const Framebuffer& framebuffer,
        const RenderPass& renderPass,
        const Swapchain& swapchain);

    ~CommandManager();

    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    VkCommandPool GetCommandPool() const { return m_CommandPool; }
    VkCommandBuffer GetCommandBuffer(uint32_t imageIndex) const
    {
        return m_CommandBuffers[imageIndex];
    }
    const std::vector<VkCommandBuffer>& GetCommandBuffers() const { return m_CommandBuffers; }

private:
    const LogicalDevice& m_LogicalDevice;
    const PhysicalDevice& m_PhysicalDevice;
    const Framebuffer& m_Framebuffer;
    const RenderPass& m_RenderPass;
    const Swapchain& m_Swapchain;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_CommandBuffers;

private:
    void CreateCommandPool();
    void AllocateCommandBuffers();
    void RecordCommandBuffers();
};
