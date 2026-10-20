#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "PhysicalDevice.h"
#include "Framebuffer.h"
#include "RenderPass.h"
#include "Swapchain.h"
#include "GraphicsPipeline.h"
#include "Buffer.h"
#include "DescriptorManager.h"
#include "MeshData.h"
#include "RenderObject.h"
#include "ShadowRenderPass.h"
#include "ShadowFramebuffer.h"
#include "ShadowPipeline.h"
#include "SkyboxPipeline.h"

#include <vector>
#include "GpuCulling.h"
#include "HiZ.h"
#include "ScenePrepass.h"
#include "GpuSkinning.h"
#include "AnimationPanel.h"
#include "RayQueryReflections.h"
#include "HiZInspector.h"
#include "FrustumDebug.h"

class CommandManager
{
public:
    struct GpuTimingResult
    {
        double skinningMs = 0;
        double accelerationUpdateMs = 0;
        double prepassMs = 0.0;
        double ssaoMs = 0.0;
        double blurMs = 0.0;
        double resolveMs = 0.0;
        double temporalMs = 0.0;
        double hiZPassMs = 0.0;
        double cullingPassMs = 0.0;
        double shadowPassMs = 0.0;
        double mainPassMs = 0.0;
        double totalGpuMs = 0.0;
    };

    CommandManager(
        const LogicalDevice& logicalDevice,
        const PhysicalDevice& physicalDevice,
        const Framebuffer& framebuffer,
        const RenderPass& renderPass,
        const Swapchain& swapchain,
        const GraphicsPipeline& graphicsPipeline,
        const SkyboxPipeline& skyboxPipeline,
        const ShadowRenderPass& shadowRenderPass,
        const ShadowFramebuffer& shadowFramebuffer,
        const ShadowPipeline& shadowPipeline,
        const Buffer& vertexBuffer,
        const Buffer& indexBuffer,
        const DescriptorManager& descriptorManager,
        const std::vector<SubMesh>& subMeshes,
        const std::vector<MaterialData>& materials,
        GpuCulling* culling = nullptr, HiZ* hiZ = nullptr, FrustumDebug* debug = nullptr, HiZInspector* inspector = nullptr, ScenePrepass* prepass = nullptr, VkImage mainDepth = VK_NULL_HANDLE, GpuSkinning* skinning=nullptr, AnimationPanel* animationPanel=nullptr, RayQueryReflections* reflections=nullptr
    );
    ~CommandManager();

    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    VkCommandPool GetCommandPool() const
    {
        return m_CommandPool;
    }

    VkCommandBuffer GetCommandBuffer(uint32_t imageIndex) const
    {
        return m_CommandBuffers[imageIndex];
    }

    bool GetGpuTimingResult(
        GpuTimingResult& result
    ) const;

    void CaptureFrame(uint32_t imageIndex, const std::string& path) const;
    void SetSkyboxEnabled(bool enabled);
    void SetDrawOrder(const std::vector<uint32_t>& order);
    void Refresh(){RecordCommandBuffers();}
    void SetPrepassEnabled(bool enabled, bool rawAo=false, int debugMode=0);
    void SetInspectorEnabled(bool enabled);
    void SetBenchmarkMode(int mode); // 0 direct, 1 frustum, 2 frustum + full Hi-Z


private:
    const LogicalDevice& m_LogicalDevice;
    const PhysicalDevice& m_PhysicalDevice;
    const Framebuffer& m_Framebuffer;
    const RenderPass& m_RenderPass;
    const Swapchain& m_Swapchain;
    const GraphicsPipeline& m_GraphicsPipeline;
    const SkyboxPipeline& m_SkyboxPipeline;
    const ShadowRenderPass& m_ShadowRenderPass;
    const ShadowFramebuffer& m_ShadowFramebuffer;
    const ShadowPipeline& m_ShadowPipeline;
    const Buffer& m_VertexBuffer;
    const Buffer& m_IndexBuffer;
    const DescriptorManager& m_DescriptorManager;

    GpuCulling* m_Culling = nullptr;
    HiZ* m_HiZ = nullptr;
    ScenePrepass* m_Prepass = nullptr;
    GpuSkinning* m_Skinning=nullptr;
    AnimationPanel* m_AnimationPanel=nullptr;
    RayQueryReflections* m_Reflections=nullptr;
    std::vector<uint32_t> m_DrawOrder;
    VkImage m_MainDepth = VK_NULL_HANDLE;
    bool m_PrepassEnabled = false, m_RawAoEnabled=false;
    int m_PrepassMode=0;
    FrustumDebug* m_Debug = nullptr;
    HiZInspector* m_Inspector = nullptr;
    bool m_InspectorEnabled = false;
    std::vector<SubMesh> m_SubMeshes;
    std::vector<MaterialData> m_Materials;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;

    float m_TimestampPeriod = 0.0f;

    std::vector<VkCommandBuffer> m_CommandBuffers;
    bool m_SkyboxEnabled = true;
    bool m_RunCulling = true, m_RunHiZ = true;
    bool m_BenchmarkMode = false;

private:
    void CreateCommandPool();
    void CreateTimestampQueryPool();
    void AllocateCommandBuffers();
    void RecordCommandBuffers();
};
