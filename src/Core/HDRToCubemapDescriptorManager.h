#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "TextureImage.h"
#include "HDRToCubemapPipeline.h"

class HDRToCubemapDescriptorManager
{
public:
    HDRToCubemapDescriptorManager(
        const LogicalDevice& logicalDevice,
        const HDRToCubemapPipeline& pipeline,
        const TextureImage& hdrTexture
    );

    ~HDRToCubemapDescriptorManager();

    HDRToCubemapDescriptorManager(
        const HDRToCubemapDescriptorManager&
    ) = delete;

    HDRToCubemapDescriptorManager& operator=(
        const HDRToCubemapDescriptorManager&
    ) = delete;

    VkDescriptorSet GetDescriptorSet() const
    {
        return m_DescriptorSet;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const HDRToCubemapPipeline& m_Pipeline;
    const TextureImage& m_HDRTexture;

    VkDescriptorPool m_DescriptorPool =
        VK_NULL_HANDLE;

    VkDescriptorSet m_DescriptorSet =
        VK_NULL_HANDLE;

private:
    void CreateDescriptorPool();
    void AllocateDescriptorSet();
    void UpdateDescriptorSet();
};