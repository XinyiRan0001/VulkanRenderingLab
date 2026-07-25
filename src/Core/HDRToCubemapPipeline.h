#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "EnvironmentRenderPass.h"

#include <cstdint>
#include <vector>

class HDRToCubemapPipeline
{
public:
    HDRToCubemapPipeline(
        const LogicalDevice& logicalDevice,
        const EnvironmentRenderPass& renderPass,
        uint32_t cubemapSize
    );

    ~HDRToCubemapPipeline();

    HDRToCubemapPipeline(
        const HDRToCubemapPipeline&
    ) = delete;

    HDRToCubemapPipeline& operator=(
        const HDRToCubemapPipeline&
    ) = delete;

    VkPipeline GetPipeline() const
    {
        return m_Pipeline;
    }

    VkPipelineLayout GetLayout() const
    {
        return m_PipelineLayout;
    }

    VkDescriptorSetLayout GetDescriptorSetLayout() const
    {
        return m_DescriptorSetLayout;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const EnvironmentRenderPass& m_RenderPass;

    uint32_t m_CubemapSize = 0;

    VkDescriptorSetLayout m_DescriptorSetLayout =
        VK_NULL_HANDLE;

    VkPipelineLayout m_PipelineLayout =
        VK_NULL_HANDLE;

    VkPipeline m_Pipeline =
        VK_NULL_HANDLE;

private:
    void CreateDescriptorSetLayout();
    void CreatePipeline();

    VkShaderModule CreateShaderModule(
        const std::vector<char>& code
    ) const;
};