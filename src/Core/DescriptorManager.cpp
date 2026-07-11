#include "DescriptorManager.h"
#include <array>
#include <iostream>
#include <stdexcept>

DescriptorManager::DescriptorManager(
    const LogicalDevice& logicalDevice,
    const GraphicsPipeline& graphicsPipeline,
    const Buffer& uniformBuffer,
    const TextureImage& textureImage
)
    : m_LogicalDevice(logicalDevice),
    m_GraphicsPipeline(graphicsPipeline),
    m_UniformBuffer(uniformBuffer),
    m_TextureImage(textureImage)
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

    poolSizes[1].descriptorCount = 1;

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
    bufferInfo.buffer = m_UniformBuffer.GetHandle();
    bufferInfo.offset = 0;
    bufferInfo.range = m_UniformBuffer.GetSize();

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfo.imageView =
        m_TextureImage.GetImageView();

    imageInfo.sampler =
        m_TextureImage.GetSampler();

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrites[0].dstSet = m_DescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrites[1].dstSet = m_DescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        static_cast<uint32_t>(descriptorWrites.size()),
        descriptorWrites.data(),
        0,
        nullptr
    );
}