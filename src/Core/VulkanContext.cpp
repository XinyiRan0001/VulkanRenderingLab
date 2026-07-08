#include "VulkanContext.h"

#include "Window.h"
#include "PhysicalDevice.h"
#include "LogicalDevice.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "CommandManager.h"
#include "Renderer.h"
#include "GraphicsPipeline.h"

#include <iostream>
#include <stdexcept>

VulkanContext::VulkanContext(Window& window)
    : m_Window(window)
{
    CreateInstance();
    CreateSurface();

    m_PhysicalDevice = std::make_unique<PhysicalDevice>(m_Instance, m_Surface);
    m_LogicalDevice = std::make_unique<LogicalDevice>(*m_PhysicalDevice);
    m_Swapchain = std::make_unique<Swapchain>(
        m_Window,
        *m_PhysicalDevice,
        *m_LogicalDevice,
        m_Surface
    );

    m_RenderPass = std::make_unique<RenderPass>(
        *m_LogicalDevice,
        *m_Swapchain
    );

    m_Framebuffer = std::make_unique<Framebuffer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_RenderPass
    );

    m_GraphicsPipeline = std::make_unique<GraphicsPipeline>(
        *m_LogicalDevice,
        *m_RenderPass,
        *m_Swapchain
    );

    m_CommandManager = std::make_unique<CommandManager>(
        *m_LogicalDevice,
        *m_PhysicalDevice,
        *m_Framebuffer,
        *m_RenderPass,
        *m_Swapchain,
        *m_GraphicsPipeline
    );

    m_Renderer = std::make_unique<Renderer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_CommandManager
    );

    std::cout << "VulkanContext created successfully." << std::endl;
}

VulkanContext::~VulkanContext()
{
    m_Renderer.reset();
    m_CommandManager.reset();
    m_Framebuffer.reset();
    m_RenderPass.reset();
    m_Swapchain.reset();
    m_LogicalDevice.reset();
    m_PhysicalDevice.reset();

    if (m_Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::CreateSurface()
{
    VkResult result = glfwCreateWindowSurface(
        m_Instance,
        m_Window.GetHandle(),
        nullptr,
        &m_Surface
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface.");
    }

    std::cout << "Window surface created successfully." << std::endl;
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

void VulkanContext::DrawFrame()
{
    m_Renderer->DrawFrame();
}