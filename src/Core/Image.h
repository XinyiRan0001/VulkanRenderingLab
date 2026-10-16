#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "PhysicalDevice.h"
#include "LogicalDevice.h"

class Image
{
public:
    Image(const PhysicalDevice& physicalDevice,
          const LogicalDevice& logicalDevice,
          uint32_t width,
          uint32_t height,
          VkFormat format,
          VkImageTiling tiling,
          VkImageUsageFlags usage,
          VkMemoryPropertyFlags properties,
          VkImageAspectFlags aspectFlags, uint32_t mipLevels = 1);

    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkImage GetHandle() const { return m_Image; }
    VkImageView GetView() const { return m_ImageView; }
    VkFormat GetFormat() const { return m_Format; }

private:
    const PhysicalDevice& m_PhysicalDevice;
    const LogicalDevice& m_LogicalDevice;

    VkImage m_Image = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkFormat m_Format{};
    uint32_t m_MipLevels = 1;

private:
    void CreateImage(uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties);

    void CreateImageView(VkImageAspectFlags aspectFlags);

    uint32_t FindMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags properties);
};