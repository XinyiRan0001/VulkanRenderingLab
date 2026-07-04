#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "PhysicalDevice.h"

class LogicalDevice
{
public:
    explicit LogicalDevice(const PhysicalDevice& physicalDevice);
    ~LogicalDevice();

    LogicalDevice(const LogicalDevice&) = delete;
    LogicalDevice& operator=(const LogicalDevice&) = delete;

    VkDevice GetHandle() const;
    VkQueue GetGraphicsQueue() const;

private:
    const PhysicalDevice& m_PhysicalDevice;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

private:
    void CreateLogicalDevice();
};