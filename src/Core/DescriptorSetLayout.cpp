#include "DescriptorSetLayout.h"
#include "LogicalDevice.h"

#include <array>
#include <iostream>
#include <stdexcept>

DescriptorSetLayout::DescriptorSetLayout(
    const LogicalDevice& logicalDevice
)
    : m_LogicalDevice(logicalDevice)
{
    CreateDescriptorSetLayout();

    std::cout
        << "[Descriptor] Shared descriptor set layout created."
        << std::endl;
}

DescriptorSetLayout::~DescriptorSetLayout()
{
    if (m_DescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(
            m_LogicalDevice.GetHandle(),
            m_DescriptorSetLayout,
            nullptr
        );

        m_DescriptorSetLayout =
            VK_NULL_HANDLE;
    }
}

void DescriptorSetLayout::CreateDescriptorSetLayout()
{
    // =========================================================
    // binding 0: Global uniform buffer
    // =========================================================

    VkDescriptorSetLayoutBinding uboBinding{};

    uboBinding.binding = 0;

    uboBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    uboBinding.descriptorCount = 1;

    uboBinding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT;

    uboBinding.pImmutableSamplers =
        nullptr;

    // =========================================================
    // binding 1: Diffuse texture
    // =========================================================

    VkDescriptorSetLayoutBinding diffuseBinding{};

    diffuseBinding.binding = 1;

    diffuseBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    diffuseBinding.descriptorCount = 1;

    diffuseBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    diffuseBinding.pImmutableSamplers =
        nullptr;

    // =========================================================
    // binding 2: Shadow map
    // =========================================================

    VkDescriptorSetLayoutBinding shadowBinding{};

    shadowBinding.binding = 2;

    shadowBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    shadowBinding.descriptorCount = 1;

    shadowBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    shadowBinding.pImmutableSamplers =
        nullptr;

    // =========================================================
    // binding 3: HDR equirectangular environment
    // =========================================================

    VkDescriptorSetLayoutBinding hdrBinding{};

    hdrBinding.binding = 3;

    hdrBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    hdrBinding.descriptorCount = 1;

    hdrBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    hdrBinding.pImmutableSamplers =
        nullptr;

    const std::array<
        VkDescriptorSetLayoutBinding,
        4
    > bindings =
    {
        uboBinding,
        diffuseBinding,
        shadowBinding,
        hdrBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};

    layoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

    layoutInfo.bindingCount =
        static_cast<uint32_t>(
            bindings.size()
        );

    layoutInfo.pBindings =
        bindings.data();

    if (vkCreateDescriptorSetLayout(
        m_LogicalDevice.GetHandle(),
        &layoutInfo,
        nullptr,
        &m_DescriptorSetLayout
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shared descriptor set layout."
        );
    }
}