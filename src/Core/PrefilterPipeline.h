#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "EnvironmentRenderPass.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// =========================================================
// Prefilter push constants
//
// parameters.x = roughness
// parameters.yzw = reserved
// =========================================================

struct PrefilterPushConstants
{
    glm::mat4 viewProjection{ 1.0f };
    glm::vec4 parameters{ 0.0f };
};

static_assert(
    sizeof(PrefilterPushConstants) == 80,
    "PrefilterPushConstants must be exactly 80 bytes."
);

class PrefilterPipeline
{
public:
    PrefilterPipeline(
        const LogicalDevice& logicalDevice,
        const EnvironmentRenderPass& renderPass,
        uint32_t baseCubemapSize
    );

    ~PrefilterPipeline();

    PrefilterPipeline(
        const PrefilterPipeline&
    ) = delete;

    PrefilterPipeline& operator=(
        const PrefilterPipeline&
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

    uint32_t m_BaseCubemapSize = 0;

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