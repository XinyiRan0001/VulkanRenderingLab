#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <memory>

class Window;
class PhysicalDevice;
class LogicalDevice;
class Swapchain;
class RenderPass;
class Framebuffer;
class CommandManager;
class Renderer;

class VulkanContext
{
public:
    explicit VulkanContext(Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkSurfaceKHR GetSurface() const { return m_Surface; }
    void DrawFrame();

private:
    Window& m_Window;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

    std::unique_ptr<PhysicalDevice> m_PhysicalDevice;
    std::unique_ptr<LogicalDevice> m_LogicalDevice;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<RenderPass> m_RenderPass;
    std::unique_ptr<Framebuffer> m_Framebuffer;
    std::unique_ptr<CommandManager> m_CommandManager;
    std::unique_ptr<Renderer> m_Renderer;

private:
    void CreateInstance();
    void CreateSurface();
};