#include "DescriptorManager.h"
#include <array>
#include <iostream>
#include <stdexcept>

DescriptorManager::DescriptorManager(
    const LogicalDevice& logicalDevice,
    const GraphicsPipeline& graphicsPipeline,
    const Buffer& uniformBuffer,
    const TextureImage& textureImage,
    const TextureImage& hdrTextureImage,
    VkImageView shadowImageView,
    VkSampler shadowSampler
)
    : m_LogicalDevice(logicalDevice),
    m_GraphicsPipeline(graphicsPipeline),
    m_UniformBuffer(uniformBuffer),
    m_TextureImage(textureImage),
    m_HDRTextureImage(hdrTextureImage),
    m_ShadowImageView(shadowImageView),
    m_ShadowSampler(shadowSampler)
{
    CreateDescriptorPool();
    AllocateDescriptorSet();
    UpdateDescriptorSet();

    std::cout
        << "DescriptorManager created successfully."
        << std::endl;
}

DescriptorManager::~DescriptorManager()
{
    if (m_DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(
            m_LogicalDevice.GetHandle(),
            m_DescriptorPool,
            nullptr
        );
    }
}

void DescriptorManager::CreateDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    poolSizes[0].type =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    // Diffuse texture + Shadow map
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;

    poolInfo.poolSizeCount =
        static_cast<uint32_t>(poolSizes.size());

    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(
        m_LogicalDevice.GetHandle(),
        &poolInfo,
        nullptr,
        &m_DescriptorPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create descriptor pool."
        );
    }
}

void DescriptorManager::AllocateDescriptorSet()
{
    VkDescriptorSetLayout layout = m_GraphicsPipeline.GetDescriptorSetLayout();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        &allocInfo,
        &m_DescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set.");
    }
}

void DescriptorManager::UpdateDescriptorSet()
{
    VkDescriptorBufferInfo bufferInfo{};

    bufferInfo.buffer =
        m_UniformBuffer.GetHandle();

    bufferInfo.offset = 0;

    bufferInfo.range =
        m_UniformBuffer.GetSize();

    // binding 1: diffuse texture
    VkDescriptorImageInfo textureImageInfo{};
    textureImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    textureImageInfo.imageView =
        m_TextureImage.GetImageView();
    textureImageInfo.sampler =
        m_TextureImage.GetSampler();

    // binding 2: shadow map
    VkDescriptorImageInfo shadowImageInfo{};
    shadowImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowImageInfo.imageView =
        m_ShadowImageView;
    shadowImageInfo.sampler =
        m_ShadowSampler;

    // binding 3: HDR environment
    VkDescriptorImageInfo hdrImageInfo{};
    hdrImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrImageInfo.imageView =
        m_HDRTextureImage.GetImageView();
    hdrImageInfo.sampler =
        m_HDRTextureImage.GetSampler();

    std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

    // binding 0: uniform buffer
    descriptorWrites[0].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet =
        m_DescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo =
        &bufferInfo;

    // binding 1: diffuse texture
    descriptorWrites[1].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet =
        m_DescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo =
        &textureImageInfo;

    // binding 2: shadow map
    descriptorWrites[2].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet =
        m_DescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo =
        &shadowImageInfo;

    // binding 3: HDR environment
    descriptorWrites[3].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet =
        m_DescriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pImageInfo =
        &hdrImageInfo;

    vkUpdateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        static_cast<uint32_t>(descriptorWrites.size()),
        descriptorWrites.data(),
        0,
        nullptr
    );
}