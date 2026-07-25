#include "PrefilterDescriptorManager.h"

#include <iostream>
#include <stdexcept>

PrefilterDescriptorManager::PrefilterDescriptorManager(
    const LogicalDevice& logicalDevice,
    const PrefilterPipeline& pipeline,
    const TextureImage& environmentCubemap
)
    : m_LogicalDevice(logicalDevice),
    m_Pipeline(pipeline),
    m_EnvironmentCubemap(environmentCubemap)
{
    CreateDescriptorPool();
    AllocateDescriptorSet();
    UpdateDescriptorSet();

    std::cout
        << "[IBL] Prefilter descriptor set created."
        << std::endl;
}

PrefilterDescriptorManager::~PrefilterDescriptorManager()
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

        m_DescriptorSet =
            VK_NULL_HANDLE;
    }
}

void PrefilterDescriptorManager::CreateDescriptorPool()
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
            "Failed to create prefilter descriptor pool."
        );
    }
}

void PrefilterDescriptorManager::AllocateDescriptorSet()
{
    VkDescriptorSetLayout descriptorSetLayout =
        m_Pipeline.GetDescriptorSetLayout();

    VkDescriptorSetAllocateInfo allocateInfo{};

    allocateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;

    allocateInfo.descriptorPool =
        m_DescriptorPool;

    allocateInfo.descriptorSetCount =
        1;

    allocateInfo.pSetLayouts =
        &descriptorSetLayout;

    if (vkAllocateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        &allocateInfo,
        &m_DescriptorSet
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to allocate prefilter descriptor set."
        );
    }
}

void PrefilterDescriptorManager::UpdateDescriptorSet()
{
    VkDescriptorImageInfo environmentImageInfo{};

    environmentImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    environmentImageInfo.imageView =
        m_EnvironmentCubemap.GetImageView();

    environmentImageInfo.sampler =
        m_EnvironmentCubemap.GetSampler();

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
        &environmentImageInfo;

    vkUpdateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        1,
        &descriptorWrite,
        0,
        nullptr
    );
}