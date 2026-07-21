#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "Buffer.h"
#include "GraphicsPipeline.h"
#include "TextureImage.h"
#include "ShadowRenderPass.h"
#include "ShadowFramebuffer.h"
#include "ShadowPipeline.h"

class DescriptorManager
{
public:
    DescriptorManager(
    const LogicalDevice& logicalDevice,
    const GraphicsPipeline& graphicsPipeline,
    const Buffer& uniformBuffer,
    const TextureImage& textureImage,
    const TextureImage& hdrTextureImage,
    VkImageView shadowImageView,
    VkSampler shadowSampler
);

    ~DescriptorManager();

    DescriptorManager(const DescriptorManager&) = delete;
    DescriptorManager& operator=(const DescriptorManager&) = delete;

    VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }


private:
    const LogicalDevice& m_LogicalDevice;
    const GraphicsPipeline& m_GraphicsPipeline;
    const Buffer& m_UniformBuffer;
    const TextureImage& m_TextureImage;
    const TextureImage& m_HDRTextureImage;

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    VkImageView m_ShadowImageView = VK_NULL_HANDLE;
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

private:
    void CreateDescriptorPool();
    void AllocateDescriptorSet();
    void UpdateDescriptorSet();
};