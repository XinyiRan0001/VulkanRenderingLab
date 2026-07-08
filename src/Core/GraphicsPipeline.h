#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "RenderPass.h"
#include "Swapchain.h"

class GraphicsPipeline
{
public:
    GraphicsPipeline(const LogicalDevice& logicalDevice,
        const RenderPass& renderPass,
        const Swapchain& swapchain);

    ~GraphicsPipeline();

    VkPipeline GetPipeline() const { return m_GraphicsPipeline; }
    VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

private:
    const LogicalDevice& m_LogicalDevice;
    const RenderPass& m_RenderPass;
    const Swapchain& m_Swapchain;

    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

private:
    void CreateGraphicsPipeline();
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
};