#include "Image.h"

#include <stdexcept>

Image::Image(const PhysicalDevice& physicalDevice,
             const LogicalDevice& logicalDevice,
             uint32_t width,
             uint32_t height,
             VkFormat format,
             VkImageTiling tiling,
             VkImageUsageFlags usage,
             VkMemoryPropertyFlags properties,
             VkImageAspectFlags aspectFlags)
    : m_PhysicalDevice(physicalDevice),
      m_LogicalDevice(logicalDevice),
      m_Format(format)
{
    CreateImage(width, height, format, tiling, usage, properties);
    CreateImageView(aspectFlags);
}

Image::~Image()
{
    VkDevice device = m_LogicalDevice.GetHandle();

    if (m_ImageView != VK_NULL_HANDLE)
        vkDestroyImageView(device, m_ImageView, nullptr);

    if (m_Image != VK_NULL_HANDLE)
        vkDestroyImage(device, m_Image, nullptr);

    if (m_Memory != VK_NULL_HANDLE)
        vkFreeMemory(device, m_Memory, nullptr);
}

void Image::CreateImage(uint32_t width,
                        uint32_t height,
                        VkFormat format,
                        VkImageTiling tiling,
                        VkImageUsageFlags usage,
                        VkMemoryPropertyFlags properties)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_LogicalDevice.GetHandle();

    if (vkCreateImage(device, &imageInfo, nullptr, &m_Image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image.");

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(device, m_Image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memRequirements.memoryTypeBits,
        properties
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate image memory.");

    vkBindImageMemory(device, m_Image, m_Memory, 0);
}

void Image::CreateImageView(VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_Format;

    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(
            m_LogicalDevice.GetHandle(),
            &viewInfo,
            nullptr,
            &m_ImageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image view.");
    }
}

uint32_t Image::FindMemoryType(uint32_t typeFilter,
                               VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(
        m_PhysicalDevice.GetHandle(),
        &memProperties
    );

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable image memory type.");
}