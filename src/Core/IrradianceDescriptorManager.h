#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "TextureImage.h"
#include "IrradiancePipeline.h"

class IrradianceDescriptorManager
{
public:
    IrradianceDescriptorManager(
        const LogicalDevice& logicalDevice,
        const IrradiancePipeline& pipeline,
        const TextureImage& environmentCubemap
    );

    ~IrradianceDescriptorManager();

    IrradianceDescriptorManager(
        const IrradianceDescriptorManager&
    ) = delete;

    IrradianceDescriptorManager& operator=(
        const IrradianceDescriptorManager&
    ) = delete;

    VkDescriptorSet GetDescriptorSet() const
    {
        return m_DescriptorSet;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const IrradiancePipeline& m_Pipeline;
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