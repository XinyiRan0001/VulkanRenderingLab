#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "RenderPass.h"
#include "Swapchain.h"

#include <array>
#include <vector>

class GraphicsPipeline
{
  public:
    GraphicsPipeline(const LogicalDevice &logicalDevice, const RenderPass &renderPass,
                     const Swapchain &swapchain, VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetLayout rayLayout=VK_NULL_HANDLE);

    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline &) = delete;

    GraphicsPipeline &operator=(const GraphicsPipeline &) = delete;

    VkPipeline GetPipeline() const
    {
        return m_GraphicsPipeline;
    }

    VkPipeline GetPipeline(bool doubleSided, bool blend) const
    {
        const int i = (doubleSided ? 1 : 0) + (blend ? 2 : 0);
        return i ? m_Variants[i - 1] : m_GraphicsPipeline;
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
    const LogicalDevice &m_LogicalDevice;
    const RenderPass &m_RenderPass;
    const Swapchain &m_Swapchain;

    // Non-owning handle. DescriptorSetLayout owns its lifetime.
    VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_RayLayout=VK_NULL_HANDLE;

    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    std::array<VkPipeline, 3> m_Variants{};
    VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

  private:
    void CreateGraphicsPipeline();

    VkShaderModule CreateShaderModule(const std::vector<char> &code);
};
