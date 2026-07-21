#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "RenderPass.h"
#include "Swapchain.h"

#include <vector>

class SkyboxPipeline
{
public:
    SkyboxPipeline(
        const LogicalDevice& logicalDevice,
        const RenderPass& renderPass,
        const Swapchain& swapchain,
        VkDescriptorSetLayout descriptorSetLayout
    );

    ~SkyboxPipeline();

    SkyboxPipeline(
        const SkyboxPipeline&
    ) = delete;

    SkyboxPipeline& operator=(
        const SkyboxPipeline&
    ) = delete;

    VkPipeline GetPipeline() const
    {
        return m_Pipeline;
    }

    VkPipelineLayout GetLayout() const
    {
        return m_PipelineLayout;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const RenderPass& m_RenderPass;
    const Swapchain& m_Swapchain;

    // Non-owning handle.
    VkDescriptorSetLayout m_DescriptorSetLayout =
        VK_NULL_HANDLE;

    VkPipelineLayout m_PipelineLayout =
        VK_NULL_HANDLE;

    VkPipeline m_Pipeline =
        VK_NULL_HANDLE;

private:
    void CreatePipeline();

    VkShaderModule CreateShaderModule(
        const std::vector<char>& code
    );
};