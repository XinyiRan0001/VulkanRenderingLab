#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "PhysicalDevice.h"
#include "LogicalDevice.h"

class Buffer
{
public:
    Buffer(const PhysicalDevice& physicalDevice,
        const LogicalDevice& logicalDevice,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties);

    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer GetHandle() const { return m_Buffer; }
    VkDeviceSize GetSize() const { return m_Size; }
    VkDeviceAddress Address() const;

    void DownloadData(void* data, VkDeviceSize size) const;
    void UploadData(const void* data, VkDeviceSize size);

private:
    const PhysicalDevice& m_PhysicalDevice;
    const LogicalDevice& m_LogicalDevice;

    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    VkDeviceSize m_Size = 0;

private:
    void CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties);

    uint32_t FindMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags properties);
};
