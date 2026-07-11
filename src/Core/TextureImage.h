#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include "TextureData.h"

class TextureImage
{
public:
    TextureImage(
        const PhysicalDevice& physicalDevice,
        const LogicalDevice& logicalDevice,
        const TextureData& textureData
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

    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_ImageMemory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_R8G8B8A8_SRGB;

private:
    void CreateImage(
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage
    );

    void CreateImageView();
    void CreateSampler();
    void UploadPixels(const TextureData& textureData);

    void TransitionImageLayout(
        VkImageLayout oldLayout,
        VkImageLayout newLayout
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
