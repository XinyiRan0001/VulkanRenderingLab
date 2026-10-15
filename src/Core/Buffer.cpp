#include "Buffer.h"

#include <stdexcept>
#include <cstring>

Buffer::Buffer(const PhysicalDevice& physicalDevice,
    const LogicalDevice& logicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties)
    : m_PhysicalDevice(physicalDevice),
    m_LogicalDevice(logicalDevice),
    m_Size(size)
{
    CreateBuffer(size, usage, properties);
}

Buffer::~Buffer()
{
    VkDevice device = m_LogicalDevice.GetHandle();

    if (m_Buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, m_Buffer, nullptr);

    if (m_Memory != VK_NULL_HANDLE)
        vkFreeMemory(device, m_Memory, nullptr);
}

void Buffer::CreateBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_LogicalDevice.GetHandle();

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer.");
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(device, m_Buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        memRequirements.memoryTypeBits,
        properties
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate buffer memory.");
    }

    vkBindBufferMemory(device, m_Buffer, m_Memory, 0);
}

uint32_t Buffer::FindMemoryType(uint32_t typeFilter,
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

    throw std::runtime_error("Failed to find suitable memory type.");
}

void Buffer::UploadData(const void* data, VkDeviceSize size)
{
    void* mappedData = nullptr;

    vkMapMemory(
        m_LogicalDevice.GetHandle(),
        m_Memory,
        0,
        size,
        0,
        &mappedData
    );

    std::memcpy(mappedData, data, static_cast<size_t>(size));

    vkUnmapMemory(
        m_LogicalDevice.GetHandle(),
        m_Memory
    );
}
void Buffer::DownloadData(void* data, VkDeviceSize size) const {
    if (size > m_Size) throw std::runtime_error("Readback exceeds buffer size");
    void* mapped = nullptr;
    if (vkMapMemory(m_LogicalDevice.GetHandle(), m_Memory, 0, size, 0, &mapped) != VK_SUCCESS)
        throw std::runtime_error("Cannot map readback buffer");
    std::memcpy(data, mapped, static_cast<size_t>(size));
    vkUnmapMemory(m_LogicalDevice.GetHandle(), m_Memory);
}
