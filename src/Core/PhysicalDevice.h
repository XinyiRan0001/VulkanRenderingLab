#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "QueueFamily.h"
#include "SwapchainSupport.h"

#include <string>

class PhysicalDevice
{
public:
    PhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    ~PhysicalDevice() = default;

    PhysicalDevice(const PhysicalDevice&) = delete;
    PhysicalDevice& operator=(const PhysicalDevice&) = delete;

    VkPhysicalDevice GetHandle() const;
    const VkPhysicalDeviceProperties& GetProperties() const;

    QueueFamilyIndices FindQueueFamilies() const;
    SwapchainSupportDetails QuerySwapchainSupport() const;

private:
    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_Properties{};

private:
    void PickPhysicalDevice();
    int ScoreDevice(VkPhysicalDevice device) const;
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device) const;
    std::string DeviceTypeToString(VkPhysicalDeviceType type) const;
};