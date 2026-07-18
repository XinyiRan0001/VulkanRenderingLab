#pragma once

#define GLFW_INCLUDE_VULKAN

#include "RenderObject.h"
#include <vector>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>

class Window;
class PhysicalDevice;
class LogicalDevice;
class Swapchain;
class RenderPass;
class Framebuffer;
class CommandManager;
class Renderer;
class GraphicsPipeline;
class Buffer;
class Image;
class DescriptorManager;
class TextureImage;
class ShadowRenderPass;
class ShadowFramebuffer;
class ShadowPipeline;

class VulkanContext
{
public:
    explicit VulkanContext(Window& window);
    ~VulkanContext();
    void UpdateUniformBuffer();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkSurfaceKHR GetSurface() const { return m_Surface; }
    void DrawFrame();
    void UpdateWindowTitle(double deltaTime);

private:
    Window& m_Window;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

    std::unique_ptr<PhysicalDevice> m_PhysicalDevice;
    std::unique_ptr<LogicalDevice> m_LogicalDevice;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<RenderPass> m_RenderPass;
    std::unique_ptr<GraphicsPipeline> m_GraphicsPipeline;
    std::unique_ptr<Image> m_DepthImage;
    std::unique_ptr<Image> m_ShadowDepthImage;
    std::unique_ptr<ShadowRenderPass> m_ShadowRenderPass;
    std::unique_ptr<ShadowFramebuffer> m_ShadowFramebuffer;
    std::unique_ptr<Framebuffer> m_Framebuffer;
    std::unique_ptr<CommandManager> m_CommandManager;
    std::unique_ptr<Renderer> m_Renderer;
    std::unique_ptr<Buffer> m_VertexBuffer;
    std::unique_ptr<Buffer> m_IndexBuffer;
    std::unique_ptr<Buffer> m_UniformBuffer;
    std::unique_ptr<TextureImage> m_TextureImage;
    std::unique_ptr<DescriptorManager> m_DescriptorManager;
    std::unique_ptr<ShadowPipeline> m_ShadowPipeline;

    float m_CameraYaw = 0.0f;
    float m_CameraPitch = 0.15f;
    float m_CameraDistance = 3.5f;

    bool m_IsDragging = false;

    double m_LastMouseX = 0.0;
    double m_LastMouseY = 0.0;

    int m_ShadowMode = 3;

    double m_LastTitleUpdateTime = 0.0;
    double m_FrameTimeAccumulator = 0.0;
    int m_FrameCount = 0;

    bool m_PointLightEnabled = true;
    bool m_PointLightAnimationEnabled = true;

    float m_PointLightAngle = 0.0f;
    double m_LastLightUpdateTime = 0.0;
    
    glm::vec3 m_ManualPointLightPosition{
    2.0f,
    1.5f,
    0.0f
    };

    bool m_ManualPointLightControl = false;
private:
    void CreateInstance();
    void CreateSurface();
    void SetupCameraControls();
    void CreateShadowSampler();
};