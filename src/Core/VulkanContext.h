#pragma once

#define GLFW_INCLUDE_VULKAN

#include "RenderObject.h"
#include "EnvironmentRenderPass.h"
#include "EnvironmentFramebuffer.h"
#include "HDRToCubemapPipeline.h"
#include "HDRToCubemapDescriptorManager.h"
#include "PrefilterFramebuffer.h"
#include "PrefilterPipeline.h"
#include "PrefilterDescriptorManager.h"
#include "BRDFLUTRenderPass.h"
#include "BRDFLUTFramebuffer.h"
#include "BRDFLUTPipeline.h"
#include "IrradianceRenderPass.h"
#include "IrradianceFramebuffer.h"
#include "IrradiancePipeline.h"
#include "IrradianceDescriptorManager.h"

#include <vector>
#include <string>
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
class DescriptorSetLayout;
class TextureImage;
class ShadowRenderPass;
class ShadowFramebuffer;
class ShadowPipeline;
class SkyboxPipeline;

class VulkanContext
{
public:
    explicit VulkanContext(Window& window, const std::string& scenePath = "");
    ~VulkanContext();
    void UpdateUniformBuffer();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkSurfaceKHR GetSurface() const { return m_Surface; }
    void DrawFrame();
    void UpdateWindowTitle(double deltaTime);
    void ToggleSkybox();
    void ConfigureShadowDiagnostic(int shadowMode, float yawDegrees, float pitchDegrees, bool paused);
    void CaptureNextFrame(const std::string& path);
    void SetInspection(int materialView, int lightingView, float environmentDegrees);

private:
    Window& m_Window;
    bool m_IsGltfScene = false;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkSampler m_ShadowSampler = VK_NULL_HANDLE;

    std::unique_ptr<PhysicalDevice> m_PhysicalDevice;
    std::unique_ptr<LogicalDevice> m_LogicalDevice;
    std::unique_ptr<Swapchain> m_Swapchain;
    std::unique_ptr<RenderPass> m_RenderPass;
    std::unique_ptr<DescriptorSetLayout>
    m_DescriptorSetLayout;
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
    std::vector<std::unique_ptr<TextureImage>> m_MaterialImages;
    std::unique_ptr<TextureImage> m_HDRTextureImage;
    std::unique_ptr<HDRToCubemapPipeline>
        m_HDRToCubemapPipeline;
    std::unique_ptr<HDRToCubemapDescriptorManager>
        m_HDRToCubemapDescriptorManager;
    std::unique_ptr<PrefilterFramebuffer>
        m_PrefilterFramebuffer;
    std::unique_ptr<PrefilterPipeline>
        m_PrefilterPipeline;
    std::unique_ptr<PrefilterDescriptorManager>
        m_PrefilterDescriptorManager;
    std::unique_ptr<TextureImage> m_EnvironmentCubemap;
    std::unique_ptr<TextureImage> m_PrefilteredEnvironmentCubemap;
    std::unique_ptr<TextureImage>
        m_BRDFLUT;
    std::unique_ptr<BRDFLUTRenderPass>
        m_BRDFLUTRenderPass;

    std::unique_ptr<BRDFLUTFramebuffer>
        m_BRDFLUTFramebuffer;
    std::unique_ptr<BRDFLUTPipeline>
        m_BRDFLUTPipeline;

    std::unique_ptr<TextureImage>
        m_IrradianceCubemap;

    std::unique_ptr<IrradianceRenderPass>
        m_IrradianceRenderPass;

    std::unique_ptr<IrradianceFramebuffer>
        m_IrradianceFramebuffer;

    std::unique_ptr<IrradiancePipeline>
        m_IrradiancePipeline;

    std::unique_ptr<IrradianceDescriptorManager>
        m_IrradianceDescriptorManager;

    std::unique_ptr<EnvironmentRenderPass>
        m_EnvironmentRenderPass;

    std::unique_ptr<EnvironmentFramebuffer>
        m_EnvironmentFramebuffer;
    std::unique_ptr<DescriptorManager> m_DescriptorManager;
    std::unique_ptr<ShadowPipeline> m_ShadowPipeline;
    std::unique_ptr<SkyboxPipeline> m_SkyboxPipeline;

    glm::vec3 m_CameraTarget{0.0f, 0.8f, 0.0f};
    int m_MaterialView = 0;
    int m_LightingView = 0;
    float m_EnvironmentRotation = 0.0f;
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
    bool m_EnableSkybox = true;
    bool m_EnableEnvironmentLighting = true;
    float m_Exposure = 1.0f;


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
    void GenerateEnvironmentCubemap();
    void GeneratePrefilteredEnvironmentCubemap();
    void GenerateBRDFLUT();
    void GenerateIrradianceCubemap();
};