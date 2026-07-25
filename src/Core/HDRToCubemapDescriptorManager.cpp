#include "HDRToCubemapDescriptorManager.h"

#include <iostream>
#include <stdexcept>

HDRToCubemapDescriptorManager::
HDRToCubemapDescriptorManager(
    const LogicalDevice& logicalDevice,
    const HDRToCubemapPipeline& pipeline,
    const TextureImage& hdrTexture
)
    : m_LogicalDevice(logicalDevice),
    m_Pipeline(pipeline),
    m_HDRTexture(hdrTexture)
{
    CreateDescriptorPool();
    AllocateDescriptorSet();
    UpdateDescriptorSet();

    std::cout
        << "[IBL] HDR-to-cubemap descriptor set created."
        << std::endl;
}

HDRToCubemapDescriptorManager::
~HDRToCubemapDescriptorManager()
{
    if (m_DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(
            m_LogicalDevice.GetHandle(),
            m_DescriptorPool,
            nullptr
        );

        m_DescriptorPool =
            VK_NULL_HANDLE;
    }
}

void HDRToCubemapDescriptorManager::
CreateDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};

    poolSize.type =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    poolSize.descriptorCount =
        1;

    VkDescriptorPoolCreateInfo poolInfo{};

    poolInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;

    poolInfo.poolSizeCount =
        1;

    poolInfo.pPoolSizes =
        &poolSize;

    poolInfo.maxSets =
        1;

    if (vkCreateDescriptorPool(
        m_LogicalDevice.GetHandle(),
        &poolInfo,
        nullptr,
        &m_DescriptorPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create HDR-to-cubemap descriptor pool."
        );
    }
}

void HDRToCubemapDescriptorManager::
AllocateDescriptorSet()
{
    VkDescriptorSetLayout layout =
        m_Pipeline.GetDescriptorSetLayout();

    VkDescriptorSetAllocateInfo allocationInfo{};

    allocationInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

    allocationInfo.descriptorPool =
        m_DescriptorPool;

    allocationInfo.descriptorSetCount =
        1;

    allocationInfo.pSetLayouts =
        &layout;

    if (vkAllocateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        &allocationInfo,
        &m_DescriptorSet
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to allocate HDR-to-cubemap descriptor set."
        );
    }
}

void HDRToCubemapDescriptorManager::
UpdateDescriptorSet()
{
    VkDescriptorImageInfo imageInfo{};

    imageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfo.imageView =
        m_HDRTexture.GetImageView();

    imageInfo.sampler =
        m_HDRTexture.GetSampler();

    VkWriteDescriptorSet descriptorWrite{};

    descriptorWrite.sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrite.dstSet =
        m_DescriptorSet;

    descriptorWrite.dstBinding =
        0;

    descriptorWrite.dstArrayElement =
        0;

    descriptorWrite.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    descriptorWrite.descriptorCount =
        1;

    descriptorWrite.pImageInfo =
        &imageInfo;

    vkUpdateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        1,
        &descriptorWrite,
        0,
        nullptr
    );
}