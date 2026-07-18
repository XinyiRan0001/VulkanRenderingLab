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

#include <vector>

class CommandManager
{
public:
    struct GpuTimingResult
    {
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
        const ShadowRenderPass& shadowRenderPass,
        const ShadowFramebuffer& shadowFramebuffer,
        const ShadowPipeline& shadowPipeline,
        const Buffer& vertexBuffer,
        const Buffer& indexBuffer,
        const DescriptorManager& descriptorManager,
        const std::vector<SubMesh>& subMeshes,
        const std::vector<MaterialData>& materials
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

private:
    const LogicalDevice& m_LogicalDevice;
    const PhysicalDevice& m_PhysicalDevice;
    const Framebuffer& m_Framebuffer;
    const RenderPass& m_RenderPass;
    const Swapchain& m_Swapchain;
    const GraphicsPipeline& m_GraphicsPipeline;
    const ShadowRenderPass& m_ShadowRenderPass;
    const ShadowFramebuffer& m_ShadowFramebuffer;
    const ShadowPipeline& m_ShadowPipeline;
    const Buffer& m_VertexBuffer;
    const Buffer& m_IndexBuffer;
    const DescriptorManager& m_DescriptorManager;

    std::vector<SubMesh> m_SubMeshes;
    std::vector<MaterialData> m_Materials;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;

    float m_TimestampPeriod = 0.0f;

    std::vector<VkCommandBuffer> m_CommandBuffers;

private:
    void CreateCommandPool();
    void CreateTimestampQueryPool();
    void AllocateCommandBuffers();
    void RecordCommandBuffers();
};