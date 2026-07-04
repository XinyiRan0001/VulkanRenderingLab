#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <memory>

class Window;
class PhysicalDevice;
class LogicalDevice;

class VulkanContext
{
public:
    explicit VulkanContext(Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

private:
    Window& m_Window;

    VkInstance m_Instance = VK_NULL_HANDLE;

    std::unique_ptr<PhysicalDevice> m_PhysicalDevice;
    std::unique_ptr<LogicalDevice> m_LogicalDevice;

private:
    void CreateInstance();
};