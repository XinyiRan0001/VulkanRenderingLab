#include "DescriptorManager.h"
#include <array>
#include <iostream>
#include <stdexcept>

DescriptorManager::DescriptorManager(
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
)
    : m_LogicalDevice(logicalDevice),
    m_GraphicsPipeline(graphicsPipeline),
    m_UniformBuffer(uniformBuffer),
    m_InstanceBuffer(instanceBuffer),
    m_VisibleBuffer(visibleBuffer),
    m_TextureImage(textureImage),
    m_HDRTextureImage(hdrTextureImage),
    m_EnvironmentCubemap(environmentCubemap),
    m_PrefilteredEnvironmentCubemap(
        prefilteredEnvironmentCubemap
    ),
    m_BRDFLUT(brdfLUT),
    m_IrradianceCubemap(irradianceCubemap),
    m_ShadowImageView(shadowImageView),
    m_ShadowSampler(shadowSampler)
{
    std::cout
        << "[Descriptor] Creating descriptor pool..."
        << std::endl;

    m_MaterialTextures = materialTextures;
    CreateDescriptorPool();

    std::cout
        << "[Descriptor] Descriptor pool created."
        << std::endl;

    std::cout
        << "[Descriptor] Allocating descriptor set..."
        << std::endl;

    AllocateDescriptorSet();

    std::cout
        << "[Descriptor] Descriptor set allocated."
        << std::endl;

    std::cout
        << "[Descriptor] Updating descriptor set..."
        << std::endl;

    UpdateDescriptorSet();

    std::cout
        << "[Descriptor] Descriptor set updated."
        << std::endl;

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
    std::array<VkDescriptorPoolSize, 3> poolSizes{};

    poolSizes[0].type =
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    poolSizes[0].descriptorCount = static_cast<uint32_t>(m_MaterialTextures.size() + 1);

    poolSizes[1].type =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    // Diffuse, Shadow, HDR, Environment, Prefilter, BRDF LUT, Irradiance
    poolSizes[1].descriptorCount = 12 * static_cast<uint32_t>(m_MaterialTextures.size() + 1);

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 2 * static_cast<uint32_t>(m_MaterialTextures.size() + 1);
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;

    poolInfo.poolSizeCount =
        static_cast<uint32_t>(poolSizes.size());

    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(m_MaterialTextures.size() + 1);

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
    std::vector<VkDescriptorSetLayout> layouts(m_MaterialTextures.size() + 1, layout);
    std::vector<VkDescriptorSet> sets(layouts.size());
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        &allocInfo,
        sets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set.");
    }
    m_DescriptorSet = sets.back();
    m_MaterialSets.assign(sets.begin(), sets.end() - 1);
}

void DescriptorManager::UpdateDescriptorSet()
{
    std::cout
        << "[Descriptor] UpdateDescriptorSet entered."
        << std::endl;

    std::cout
        << "[Descriptor] Reading uniform buffer..."
        << std::endl;

    VkDescriptorBufferInfo bufferInfo{};

    bufferInfo.buffer =
        m_UniformBuffer.GetHandle();

    bufferInfo.offset = 0;

    bufferInfo.range =
        m_UniformBuffer.GetSize();

    std::cout
        << "[Descriptor] Uniform buffer ready."
        << std::endl;

    std::cout << "[Descriptor] Reading diffuse texture..." << std::endl;
    // binding 1: diffuse texture
    VkDescriptorImageInfo textureImageInfo{};
    textureImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    textureImageInfo.imageView =
        m_TextureImage.GetImageView();
    textureImageInfo.sampler =
        m_TextureImage.GetSampler();

    std::cout << "[Descriptor] Reading shadow map..." << std::endl;
    // binding 2: shadow map
    VkDescriptorImageInfo shadowImageInfo{};
    shadowImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowImageInfo.imageView =
        m_ShadowImageView;
    shadowImageInfo.sampler =
        m_ShadowSampler;

    std::cout << "[Descriptor] Reading HDR texture..." << std::endl;
    // binding 3: HDR environment
    VkDescriptorImageInfo hdrImageInfo{};
    hdrImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    hdrImageInfo.imageView =
        m_HDRTextureImage.GetImageView();
    hdrImageInfo.sampler =
        m_HDRTextureImage.GetSampler();

    std::cout << "[Descriptor] Generating environment cubemap..." << std::endl;   
    // binding 4: Generated environment cubemap
    VkDescriptorImageInfo environmentCubemapInfo{};

    environmentCubemapInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    environmentCubemapInfo.imageView =
        m_EnvironmentCubemap.GetImageView();

    environmentCubemapInfo.sampler =
        m_EnvironmentCubemap.GetSampler();  
        
    std::cout << "[Descriptor] Reading prefiltered cubemap..." << std::endl;    
    // binding 5: Prefiltered environment cubemap
    VkDescriptorImageInfo
        prefilteredEnvironmentInfo{};

    prefilteredEnvironmentInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    prefilteredEnvironmentInfo.imageView =
        m_PrefilteredEnvironmentCubemap
            .GetImageView();

    prefilteredEnvironmentInfo.sampler =
        m_PrefilteredEnvironmentCubemap
            .GetSampler();

    // binding 6: BRDF LUT
    std::cout
        << "[Descriptor] Reading BRDF LUT..."
        << std::endl;
    VkDescriptorImageInfo
        brdfLUTInfo{};

    brdfLUTInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    brdfLUTInfo.imageView =
        m_BRDFLUT.GetImageView();
    std::cout
        << "[Descriptor] BRDF LUT image view acquired."
        << std::endl;
    brdfLUTInfo.sampler =
    m_BRDFLUT.GetSampler();
    std::cout
        << "[Descriptor] BRDF LUT sampler acquired."
        << std::endl;

    // binding 7: IrradianceCubemap
    VkDescriptorImageInfo
    irradianceCubemapImageInfo{};

    irradianceCubemapImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    irradianceCubemapImageInfo.imageView =
        m_IrradianceCubemap.GetImageView();

    irradianceCubemapImageInfo.sampler =
        m_IrradianceCubemap.GetSampler();
    
    std::array<VkWriteDescriptorSet, 15> descriptorWrites{};

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

    // binding 4: Generated environment cubemap
    descriptorWrites[4].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrites[4].dstSet =
        m_DescriptorSet;

    descriptorWrites[4].dstBinding =
        4;

    descriptorWrites[4].dstArrayElement =
        0;

    descriptorWrites[4].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    descriptorWrites[4].descriptorCount =
        1;

    descriptorWrites[4].pImageInfo =
        &environmentCubemapInfo;

    // binding 5: Prefiltered environment cubemap
    descriptorWrites[5].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrites[5].dstSet =
        m_DescriptorSet;

    descriptorWrites[5].dstBinding =
        5;

    descriptorWrites[5].dstArrayElement =
        0;

    descriptorWrites[5].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    descriptorWrites[5].descriptorCount =
        1;

    descriptorWrites[5].pImageInfo =
        &prefilteredEnvironmentInfo;
        

    // binding 6: BRDF LUT

    descriptorWrites[6].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrites[6].dstSet =
        m_DescriptorSet;

    descriptorWrites[6].dstBinding =
        6;

    descriptorWrites[6].dstArrayElement =
        0;

    descriptorWrites[6].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    descriptorWrites[6].descriptorCount =
        1;

    descriptorWrites[6].pImageInfo =
        &brdfLUTInfo;

    // binding 7: IrradianceCubemap
    descriptorWrites[7].sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptorWrites[7].dstSet =
        m_DescriptorSet;

    descriptorWrites[7].dstBinding =
        7;

    descriptorWrites[7].dstArrayElement =
        0;

    descriptorWrites[7].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    descriptorWrites[7].descriptorCount =
        1;

    descriptorWrites[7].pImageInfo =
        &irradianceCubemapImageInfo;
        
    VkDescriptorBufferInfo instanceInfo{m_InstanceBuffer.GetHandle(), 0, m_InstanceBuffer.GetSize()};
    descriptorWrites[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[12].dstBinding = 12;
    descriptorWrites[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[12].descriptorCount = 1;
    descriptorWrites[12].pBufferInfo = &instanceInfo;
    VkDescriptorBufferInfo visibleInfo{m_VisibleBuffer.GetHandle(),0,m_VisibleBuffer.GetSize()};
    descriptorWrites[13].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[13].dstBinding=13;
    descriptorWrites[13].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[13].descriptorCount=1;
    descriptorWrites[13].pBufferInfo=&visibleInfo;
    VkDescriptorImageInfo disabledAo{m_TextureImage.GetSampler(),m_TextureImage.GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    descriptorWrites[14]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};descriptorWrites[14].dstBinding=14;
    descriptorWrites[14].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;descriptorWrites[14].descriptorCount=1;
    descriptorWrites[14].pImageInfo=&disabledAo;
    const VkDescriptorSet globalSet = m_DescriptorSet;
    for (size_t setIndex = 0; setIndex <= m_MaterialSets.size(); ++setIndex) {
        const bool isMaterial = setIndex < m_MaterialSets.size();
        const VkDescriptorSet target = isMaterial ? m_MaterialSets[setIndex] : globalSet;
        std::array<VkDescriptorImageInfo, 5> materialInfos{};
        for (size_t slot = 0; slot < 5; ++slot) {
            const auto* texture = isMaterial ? m_MaterialTextures[setIndex][slot] : &m_TextureImage;
            materialInfos[slot] = {texture->GetSampler(), texture->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        for (auto& write : descriptorWrites) write.dstSet = target;
        descriptorWrites[1].pImageInfo = &materialInfos[0];
        for (uint32_t slot = 1; slot < 5; ++slot) {
            auto& write = descriptorWrites[7 + slot];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = 7 + slot;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &materialInfos[slot];
        }
    vkUpdateDescriptorSets(
        m_LogicalDevice.GetHandle(),
        static_cast<uint32_t>(descriptorWrites.size()),
        descriptorWrites.data(),
        0,
        nullptr
    );
    }
}
void DescriptorManager::SetAoImage(VkImageView view,VkSampler sampler) {
    VkDescriptorImageInfo image{view?sampler:m_TextureImage.GetSampler(),view?view:m_TextureImage.GetImageView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::vector<VkDescriptorSet> sets=m_MaterialSets;sets.push_back(m_DescriptorSet);
    for(auto set:sets) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.dstBinding=14;
        write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.descriptorCount=1;write.pImageInfo=&image;
        vkUpdateDescriptorSets(m_LogicalDevice.GetHandle(),1,&write,0,nullptr);
    }
}
