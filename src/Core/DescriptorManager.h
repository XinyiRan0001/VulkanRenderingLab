#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include <array>
#include <vector>
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
    const Buffer& instanceBuffer,
    const Buffer& visibleBuffer,
    const TextureImage& textureImage,
    const TextureImage& hdrTextureImage,
    const TextureImage& environmentCubemap,
    const TextureImage& prefilteredEnvironmentCubemap,
    const TextureImage& brdfLUT,
    const TextureImage& irradianceCubemap,
    VkImageView shadowImageView,
    VkSampler shadowSampler,
    const std::vector<std::array<const TextureImage*, 5>>& materialTextures
);

    ~DescriptorManager();
    // Caller waits for GPU idle, then re-records command buffers after descriptor changes.
    void SetAoImage(VkImageView view, VkSampler sampler);

    DescriptorManager(const DescriptorManager&) = delete;
    DescriptorManager& operator=(const DescriptorManager&) = delete;

    VkDescriptorSet GetDescriptorSet(int material = -1) const {
        return material >= 0 && size_t(material) < m_MaterialSets.size() ? m_MaterialSets[material] : m_DescriptorSet;
    }


private:
    const LogicalDevice& m_LogicalDevice;
    const GraphicsPipeline& m_GraphicsPipeline;
    const Buffer& m_UniformBuffer;
    const Buffer& m_InstanceBuffer;
    const Buffer& m_VisibleBuffer;
    const TextureImage& m_TextureImage;
    const TextureImage& m_HDRTextureImage;
    const TextureImage& m_EnvironmentCubemap;
    const TextureImage&
        m_PrefilteredEnvironmentCubemap;
    const TextureImage&
        m_BRDFLUT;
    const TextureImage&
        m_IrradianceCubemap;
    std::vector<std::array<const TextureImage*, 5>> m_MaterialTextures;
    std::vector<VkDescriptorSet> m_MaterialSets;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    VkImageView m_ShadowImageView = VK_NULL_HANDLE;
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

private:
    void CreateDescriptorPool();
    void AllocateDescriptorSet();
    void UpdateDescriptorSet();
};