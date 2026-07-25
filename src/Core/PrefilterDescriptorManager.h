#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "TextureImage.h"
#include "PrefilterPipeline.h"

class PrefilterDescriptorManager
{
public:
    PrefilterDescriptorManager(
        const LogicalDevice& logicalDevice,
        const PrefilterPipeline& pipeline,
        const TextureImage& environmentCubemap
    );

    ~PrefilterDescriptorManager();

    PrefilterDescriptorManager(
        const PrefilterDescriptorManager&
    ) = delete;

    PrefilterDescriptorManager& operator=(
        const PrefilterDescriptorManager&
    ) = delete;

    VkDescriptorSet GetDescriptorSet() const
    {
        return m_DescriptorSet;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const PrefilterPipeline& m_Pipeline;
    const TextureImage& m_EnvironmentCubemap;

    VkDescriptorPool m_DescriptorPool =
        VK_NULL_HANDLE;

    VkDescriptorSet m_DescriptorSet =
        VK_NULL_HANDLE;

private:
    void CreateDescriptorPool();
    void AllocateDescriptorSet();
    void UpdateDescriptorSet();
};