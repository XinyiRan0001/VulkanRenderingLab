#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "PhysicalDevice.h"
#include "LogicalDevice.h"
#include "Window.h"

#include <vector>

class Swapchain
{
public:
    Swapchain(Window& window,
        const PhysicalDevice& physicalDevice,
        const LogicalDevice& logicalDevice,
        VkSurfaceKHR surface);

    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    VkFormat GetImageFormat() const { return m_ImageFormat; }
    VkExtent2D GetExtent() const { return m_Extent; }
    VkSwapchainKHR GetHandle() const { return m_Swapchain; }

    const std::vector<VkImageView>& GetImageViews() const { return m_ImageViews; }
    size_t GetImageCount() const { return m_ImageViews.size(); }

private:
    Window& m_Window;
    const PhysicalDevice& m_PhysicalDevice;
    const LogicalDevice& m_LogicalDevice;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_Images;
    std::vector<VkImageView> m_ImageViews;

    VkFormat m_ImageFormat{};
    VkExtent2D m_Extent{};

private:
    void CreateSwapchain();
    void CreateImageViews();

    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes);
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};