#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "IrradianceRenderPass.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct IrradiancePushConstants
{
    glm::mat4 viewProjection;
};

static_assert(
    sizeof(IrradiancePushConstants) ==
    sizeof(glm::mat4),
    "IrradiancePushConstants must contain exactly one mat4."
);

class IrradiancePipeline
{
public:
    IrradiancePipeline(
        const LogicalDevice& logicalDevice,
        const IrradianceRenderPass& renderPass,
        uint32_t cubemapSize
    );

    ~IrradiancePipeline();

    IrradiancePipeline(
        const IrradiancePipeline&
    ) = delete;

    IrradiancePipeline& operator=(
        const IrradiancePipeline&
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
    const IrradianceRenderPass& m_RenderPass;

    uint32_t m_CubemapSize =
        0;

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