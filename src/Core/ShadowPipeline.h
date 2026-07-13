#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include "LogicalDevice.h"
#include "ShadowRenderPass.h"

class ShadowPipeline
{
public:
    ShadowPipeline(
        const LogicalDevice& logicalDevice,
        const ShadowRenderPass& shadowRenderPass,
        VkDescriptorSetLayout descriptorSetLayout
    );

    ~ShadowPipeline();

    ShadowPipeline(const ShadowPipeline&) = delete;
    ShadowPipeline& operator=(const ShadowPipeline&) = delete;

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
    const ShadowRenderPass& m_ShadowRenderPass;

    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DescriptorSetLayout =
        VK_NULL_HANDLE;

private:
    void CreatePipeline();
    VkShaderModule CreateShaderModule(
        const std::vector<char>& code
    );
};