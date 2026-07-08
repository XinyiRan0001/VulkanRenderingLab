#include "LogicalDevice.h"

#include <set>
#include <iostream>
#include <stdexcept>
#include <vector>

LogicalDevice::LogicalDevice(const PhysicalDevice& physicalDevice)
    : m_PhysicalDevice(physicalDevice)
{
    CreateLogicalDevice();

    std::cout << "LogicalDevice created successfully." << std::endl;
}

LogicalDevice::~LogicalDevice()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }
}

VkDevice LogicalDevice::GetHandle() const
{
    return m_Device;
}

VkQueue LogicalDevice::GetGraphicsQueue() const
{
    return m_GraphicsQueue;
}

VkQueue LogicalDevice::GetPresentQueue() const 
{ 
    return m_PresentQueue; 
}

void LogicalDevice::CreateLogicalDevice()
{
    QueueFamilyIndices indices = m_PhysicalDevice.FindQueueFamilies();

    if (!indices.IsComplete())
    {
        throw std::runtime_error("Selected GPU does not support required queue families.");
    }

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    std::vector<const char*> deviceExtensions =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkResult result = vkCreateDevice(
        m_PhysicalDevice.GetHandle(),
        &createInfo,
        nullptr,
        &m_Device
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device.");
    }

    vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, indices.presentFamily.value(), 0, &m_PresentQueue);

    std::cout << "Graphics Queue Family: "
        << indices.graphicsFamily.value()
        << std::endl;

    std::cout << "Present Queue Family: "
        << indices.presentFamily.value()
        << std::endl;

}