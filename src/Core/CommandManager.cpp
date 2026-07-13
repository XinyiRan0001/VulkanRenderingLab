#include "CommandManager.h"
#include "DescriptorManager.h"
#include "GraphicsPipeline.h"

#include <iostream>
#include <stdexcept>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    constexpr uint32_t SHADOW_MAP_WIDTH = 2048;
    constexpr uint32_t SHADOW_MAP_HEIGHT = 2048;
}

CommandManager::CommandManager(
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
)
    : m_LogicalDevice(logicalDevice),
    m_PhysicalDevice(physicalDevice),
    m_Framebuffer(framebuffer),
    m_RenderPass(renderPass),
    m_Swapchain(swapchain),
    m_GraphicsPipeline(graphicsPipeline),
    m_ShadowRenderPass(shadowRenderPass),
    m_ShadowFramebuffer(shadowFramebuffer),
    m_ShadowPipeline(shadowPipeline),
    m_VertexBuffer(vertexBuffer),
    m_IndexBuffer(indexBuffer),
    m_DescriptorManager(descriptorManager),
    m_SubMeshes(subMeshes),
    m_Materials(materials)
{
    CreateCommandPool();
    AllocateCommandBuffers();
    RecordCommandBuffers();

    std::cout
        << "CommandManager created successfully."
        << std::endl;
}

CommandManager::~CommandManager()
{
    if (m_CommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(
            m_LogicalDevice.GetHandle(),
            m_CommandPool,
            nullptr
        );

        m_CommandPool = VK_NULL_HANDLE;
    }
}

void CommandManager::CreateCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = m_PhysicalDevice.FindQueueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(
        m_LogicalDevice.GetHandle(),
        &poolInfo,
        nullptr,
        &m_CommandPool
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool.");
    }
}

void CommandManager::AllocateCommandBuffers()
{
    m_CommandBuffers.resize(m_Framebuffer.GetHandles().size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount =
        static_cast<uint32_t>(m_CommandBuffers.size());

    VkResult result = vkAllocateCommandBuffers(
        m_LogicalDevice.GetHandle(),
        &allocInfo,
        m_CommandBuffers.data()
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers.");
    }
}

void CommandManager::RecordCommandBuffers()
{
    const auto& framebuffers =
        m_Framebuffer.GetHandles();

    for (size_t i = 0;
        i < m_CommandBuffers.size();
        ++i)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(
            m_CommandBuffers[i],
            &beginInfo
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to begin recording command buffer."
            );
        }

        // =====================================================
        // Shadow Pass
        // =====================================================

        VkClearValue shadowClearValue{};
        shadowClearValue.depthStencil =
        {
            1.0f,
            0
        };

        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType =
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

        shadowPassInfo.renderPass =
            m_ShadowRenderPass.GetHandle();

        shadowPassInfo.framebuffer =
            m_ShadowFramebuffer.GetHandle();

        shadowPassInfo.renderArea.offset =
        {
            0,
            0
        };

        shadowPassInfo.renderArea.extent =
        {
            SHADOW_MAP_WIDTH,
            SHADOW_MAP_HEIGHT
        };

        shadowPassInfo.clearValueCount = 1;

        shadowPassInfo.pClearValues =
            &shadowClearValue;

        vkCmdBeginRenderPass(
            m_CommandBuffers[i],
            &shadowPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        vkCmdBindPipeline(
            m_CommandBuffers[i],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_ShadowPipeline.GetPipeline()
        );

        VkDescriptorSet shadowDescriptorSet =
            m_DescriptorManager.GetDescriptorSet();

        vkCmdBindDescriptorSets(
            m_CommandBuffers[i],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_ShadowPipeline.GetLayout(),
            0,
            1,
            &shadowDescriptorSet,
            0,
            nullptr
        );

        VkBuffer shadowVertexBuffers[] =
        {
            m_VertexBuffer.GetHandle()
        };

        VkDeviceSize shadowOffsets[] =
        {
            0
        };

        vkCmdBindVertexBuffers(
            m_CommandBuffers[i],
            0,
            1,
            shadowVertexBuffers,
            shadowOffsets
        );

        vkCmdBindIndexBuffer(
            m_CommandBuffers[i],
            m_IndexBuffer.GetHandle(),
            0,
            VK_INDEX_TYPE_UINT32
        );

        for (const SubMesh& subMesh : m_SubMeshes)
        {
            vkCmdDrawIndexed(
                m_CommandBuffers[i],
                subMesh.indexCount,
                1,
                subMesh.firstIndex,
                0,
                0
            );
        }

        vkCmdEndRenderPass(
            m_CommandBuffers[i]
        );

        // =====================================================
        // Main Camera Pass
        // =====================================================

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType =
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

        renderPassInfo.renderPass =
            m_RenderPass.GetHandle();

        renderPassInfo.framebuffer =
            framebuffers[i];

        renderPassInfo.renderArea.offset =
        {
            0,
            0
        };

        renderPassInfo.renderArea.extent =
            m_Swapchain.GetExtent();

        std::array<VkClearValue, 2> clearValues{};

        clearValues[0].color =
        {
            {
                0.02f,
                0.02f,
                0.04f,
                1.0f
            }
        };

        clearValues[1].depthStencil =
        {
            1.0f,
            0
        };

        renderPassInfo.clearValueCount =
            static_cast<uint32_t>(
                clearValues.size()
                );

        renderPassInfo.pClearValues =
            clearValues.data();

        vkCmdBeginRenderPass(
            m_CommandBuffers[i],
            &renderPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        vkCmdBindPipeline(
            m_CommandBuffers[i],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_GraphicsPipeline.GetPipeline()
        );

        VkDescriptorSet descriptorSet =
            m_DescriptorManager.GetDescriptorSet();

        vkCmdBindDescriptorSets(
            m_CommandBuffers[i],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_GraphicsPipeline.GetLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );

        VkBuffer vertexBuffers[] =
        {
            m_VertexBuffer.GetHandle()
        };

        VkDeviceSize offsets[] =
        {
            0
        };

        vkCmdBindVertexBuffers(
            m_CommandBuffers[i],
            0,
            1,
            vertexBuffers,
            offsets
        );

        vkCmdBindIndexBuffer(
            m_CommandBuffers[i],
            m_IndexBuffer.GetHandle(),
            0,
            VK_INDEX_TYPE_UINT32
        );

        for (const SubMesh& subMesh : m_SubMeshes)
        {
            int useTexture = 0;

            if (subMesh.materialId >= 0 &&
                static_cast<size_t>(
                    subMesh.materialId
                    ) < m_Materials.size())
            {
                useTexture =
                    m_Materials[
                        subMesh.materialId
                    ].hasDiffuseTexture
                    ? 1
                            : 0;
            }

            vkCmdPushConstants(
                m_CommandBuffers[i],
                m_GraphicsPipeline.GetLayout(),
                VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(int),
                &useTexture
            );

            vkCmdDrawIndexed(
                m_CommandBuffers[i],
                subMesh.indexCount,
                1,
                subMesh.firstIndex,
                0,
                0
            );
        }

        vkCmdEndRenderPass(
            m_CommandBuffers[i]
        );

        // =====================================================
        // Finish Command Buffer
        // =====================================================

        if (vkEndCommandBuffer(
            m_CommandBuffers[i]
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to record command buffer."
            );
        }
    }

    std::cout
        << "Command buffers recorded successfully."
        << std::endl;
}