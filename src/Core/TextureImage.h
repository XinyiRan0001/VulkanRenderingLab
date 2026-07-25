#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include "TextureData.h"

struct EmptyTextureImageCreateInfo
{
    uint32_t width = 1;
    uint32_t height = 1;

    VkFormat format =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateFlags flags = 0;

    VkImageViewType viewType =
        VK_IMAGE_VIEW_TYPE_2D;

    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;

    VkSamplerAddressMode addressMode =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
};

class TextureImage
{
public:
    TextureImage(
        const PhysicalDevice& physicalDevice,
        const LogicalDevice& logicalDevice,
        const TextureData& textureData
    );

    TextureImage(
        const PhysicalDevice& physicalDevice,
        const LogicalDevice& logicalDevice,
        const HDRTextureData& textureData
    );

    TextureImage(
    const PhysicalDevice& physicalDevice,
    const LogicalDevice& logicalDevice,
    const EmptyTextureImageCreateInfo& createInfo
);

    ~TextureImage();

    TextureImage(const TextureImage&) = delete;
    TextureImage& operator=(const TextureImage&) = delete;

    VkImage GetImage() const
    {
        return m_Image;
    }

    VkImageView GetImageView() const
    {
        return m_ImageView;
    }

    VkImageView GetLayerImageView(
        uint32_t faceIndex,
        uint32_t mipLevel = 0
    ) const;

    VkSampler GetSampler() const
    {
        return m_Sampler;
    }

    VkFormat GetFormat() const
    {
        return m_Format;
    }

private:
    const PhysicalDevice& m_PhysicalDevice;
    const LogicalDevice& m_LogicalDevice;
    uint32_t m_MipLevels =
        1;

    uint32_t m_ArrayLayers =
        1;
    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    std::vector<VkImageView>
    m_LayerImageViews;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;

private:
    void CreateImage(
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageCreateFlags flags = 0,
        uint32_t mipLevels = 1,
        uint32_t arrayLayers = 1
    );
    void CreateCubemapLayerImageViews(
        uint32_t mipLevels
    );

    void CreateImageView();

    void CreateImageView(
        VkImageViewType viewType,
        uint32_t mipLevels,
        uint32_t arrayLayers
    );

    void CreateSampler();

    void CreateSampler(
        VkSamplerAddressMode addressMode,
        float maximumLod
    );
    void UploadPixels(const TextureData& textureData);

    void UploadPixels(
        const HDRTextureData& textureData
    );

   void TransitionImageLayout(
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t baseMipLevel = 0,
        uint32_t mipLevelCount = VK_REMAINING_MIP_LEVELS,
        uint32_t baseArrayLayer = 0,
        uint32_t arrayLayerCount = VK_REMAINING_ARRAY_LAYERS
    );

    void CopyBufferToImage(
        VkBuffer buffer,
        uint32_t width,
        uint32_t height
    );

    VkCommandBuffer BeginSingleTimeCommands(
        VkCommandPool commandPool
    ) const;

    void EndSingleTimeCommands(
        VkCommandBuffer commandBuffer,
        VkCommandPool commandPool
    ) const;

    uint32_t FindMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties
    ) const;

    
};
