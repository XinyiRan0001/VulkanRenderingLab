#include "PhysicalDevice.h"

#include <iostream>
#include <stdexcept>
#include <vector>

PhysicalDevice::PhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
    : m_Instance(instance), m_Surface(surface)
{
    PickPhysicalDevice();
}

VkPhysicalDevice PhysicalDevice::GetHandle() const
{
    return m_PhysicalDevice;
}

const VkPhysicalDeviceProperties& PhysicalDevice::GetProperties() const
{
    return m_Properties;
}

QueueFamilyIndices PhysicalDevice::FindQueueFamilies() const
{
    return FindQueueFamilies(m_PhysicalDevice);
}

void PhysicalDevice::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        throw std::runtime_error("No Vulkan-capable GPU found.");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    int bestScore = -1;

    std::cout << "Available Vulkan GPUs:" << std::endl;

    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        QueueFamilyIndices indices = FindQueueFamilies(device);

        SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(device);
        bool swapchainAdequate = swapchainSupport.IsAdequate();

        int score = indices.IsComplete() && swapchainAdequate ? ScoreDevice(device) : -1;

        std::cout << "  - " << properties.deviceName
            << " | Type: " << DeviceTypeToString(properties.deviceType)
            << " | Vulkan API: "
            << VK_VERSION_MAJOR(properties.apiVersion) << "."
            << VK_VERSION_MINOR(properties.apiVersion) << "."
            << VK_VERSION_PATCH(properties.apiVersion)
            << " | Graphics Queue: "
            << (indices.graphicsFamily.has_value() ? std::to_string(indices.graphicsFamily.value()) : "None")
            << " | Present Queue: "
            << (indices.presentFamily.has_value() ? std::to_string(indices.presentFamily.value()) : "None")
            << " | Swapchain: "
            << (swapchainAdequate ? "Yes" : "No")
            << " | Score: " << score
            << std::endl;

        if (score > bestScore)
        {
            bestScore = score;
            m_PhysicalDevice = device;
            m_Properties = properties;
        }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to select a suitable GPU.");
    }

    std::cout << "Selected GPU: " << m_Properties.deviceName << std::endl;
}

int PhysicalDevice::ScoreDevice(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};

    vkGetPhysicalDeviceProperties(device, &properties);
    vkGetPhysicalDeviceFeatures(device, &features);

    int score = 0;

    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;

    if (features.geometryShader)
        score += 100;

    if (features.samplerAnisotropy)
        score += 100;

    score += static_cast<int>(properties.limits.maxImageDimension2D) / 1024;

    return score;
}

QueueFamilyIndices PhysicalDevice::FindQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            device,
            i,
            m_Surface,
            &presentSupport
        );

        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.IsComplete())
        {
            break;
        }
    }

    return indices;
}

SwapchainSupportDetails PhysicalDevice::QuerySwapchainSupport() const
{
    return QuerySwapchainSupport(m_PhysicalDevice);
}

SwapchainSupportDetails PhysicalDevice::QuerySwapchainSupport(VkPhysicalDevice device) const
{
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device,
        m_Surface,
        &details.capabilities
    );

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device,
        m_Surface,
        &formatCount,
        nullptr
    );

    if (formatCount > 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device,
            m_Surface,
            &formatCount,
            details.formats.data()
        );
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device,
        m_Surface,
        &presentModeCount,
        nullptr
    );

    if (presentModeCount > 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device,
            m_Surface,
            &presentModeCount,
            details.presentModes.data()
        );
    }

    return details;
}

std::string PhysicalDevice::DeviceTypeToString(VkPhysicalDeviceType type) const
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "CPU";
    default:
        return "Other";
    }
}