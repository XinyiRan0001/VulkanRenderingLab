#include "VulkanContext.h"

#include "Window.h"
#include "PhysicalDevice.h"
#include "LogicalDevice.h"

#include <iostream>
#include <stdexcept>

VulkanContext::VulkanContext(Window& window)
    : m_Window(window)
{
    CreateInstance();

    m_PhysicalDevice = std::make_unique<PhysicalDevice>(m_Instance);
    m_LogicalDevice = std::make_unique<LogicalDevice>(*m_PhysicalDevice);

    std::cout << "VulkanContext created successfully." << std::endl;
}

VulkanContext::~VulkanContext()
{
    m_LogicalDevice.reset();
    m_PhysicalDevice.reset();

    if (m_Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Rendering Lab";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Mini Vulkan Renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

    if (!extensions || extensionCount == 0)
    {
        throw std::runtime_error("Failed to get required GLFW Vulkan extensions.");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
}