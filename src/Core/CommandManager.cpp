#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "CommandManager.h"
#include "DescriptorManager.h"
#include "GraphicsPipeline.h"
#include "SkyboxPipeline.h"

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
    const SkyboxPipeline& skyboxPipeline,
    const ShadowRenderPass& shadowRenderPass,
    const ShadowFramebuffer& shadowFramebuffer,
    const ShadowPipeline& shadowPipeline,
    const Buffer& vertexBuffer,
    const Buffer& indexBuffer,
    const DescriptorManager& descriptorManager,
    const std::vector<SubMesh>& subMeshes,
    const std::vector<MaterialData>& materials,
    GpuCulling* culling, HiZ* hiZ, FrustumDebug* debug, HiZInspector* inspector, ScenePrepass* prepass, VkImage mainDepth
)
    : m_LogicalDevice(logicalDevice),
    m_PhysicalDevice(physicalDevice),
    m_Framebuffer(framebuffer),
    m_RenderPass(renderPass),
    m_Swapchain(swapchain),
    m_GraphicsPipeline(graphicsPipeline),
    m_SkyboxPipeline(skyboxPipeline),
    m_ShadowRenderPass(shadowRenderPass),
    m_ShadowFramebuffer(shadowFramebuffer),
    m_ShadowPipeline(shadowPipeline),
    m_VertexBuffer(vertexBuffer),
    m_IndexBuffer(indexBuffer),
    m_DescriptorManager(descriptorManager),
    m_Culling(culling),
    m_HiZ(hiZ),
    m_Debug(debug),
    m_Inspector(inspector),
    m_Prepass(prepass),m_MainDepth(mainDepth),
    m_SubMeshes(subMeshes),
    m_Materials(materials)
{
    CreateCommandPool();
    CreateTimestampQueryPool();

    VkPhysicalDeviceProperties deviceProperties{};

    vkGetPhysicalDeviceProperties(
        m_PhysicalDevice.GetHandle(),
        &deviceProperties
    );

    m_TimestampPeriod =
        deviceProperties.limits.timestampPeriod;

    std::cout
        << "[GPU Profiler] Timestamp period: "
        << m_TimestampPeriod
        << " ns"
        << std::endl;

    AllocateCommandBuffers();
    RecordCommandBuffers();

    std::cout
        << "CommandManager created successfully."
        << std::endl;
}

CommandManager::~CommandManager()
{
    if (m_TimestampQueryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(
            m_LogicalDevice.GetHandle(),
            m_TimestampQueryPool,
            nullptr
        );

        m_TimestampQueryPool = VK_NULL_HANDLE;
    }

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

void CommandManager::SetSkyboxEnabled(
    bool enabled
)
{
    if (m_SkyboxEnabled == enabled)
    {
        return;
    }

    vkDeviceWaitIdle(
        m_LogicalDevice.GetHandle()
    );

    m_SkyboxEnabled = enabled;

    RecordCommandBuffers();

    std::cout
        << "[Skybox] "
        << (m_SkyboxEnabled ? "Enabled" : "Disabled")
        << std::endl;
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

void CommandManager::CreateTimestampQueryPool()
{
    VkQueryPoolCreateInfo queryPoolInfo{};

    queryPoolInfo.sType =
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;

    queryPoolInfo.queryType =
        VK_QUERY_TYPE_TIMESTAMP;

    queryPoolInfo.queryCount = 14;

    VkResult result = vkCreateQueryPool(
        m_LogicalDevice.GetHandle(),
        &queryPoolInfo,
        nullptr,
        &m_TimestampQueryPool
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create timestamp query pool."
        );
    }

    std::cout
        << "[GPU Profiler] Timestamp query pool created successfully."
        << std::endl;
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
            VkResult resetResult =
        vkResetCommandBuffer(
            m_CommandBuffers[i],
            0
        );

    if (resetResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to reset command buffer."
        );
    }
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

        // Reset all timestamp queries before using them.
        vkCmdResetQueryPool(
            m_CommandBuffers[i],
            m_TimestampQueryPool,
            0,
            14
        );
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_TimestampQueryPool,8);
        if(m_Prepass && m_PrepassEnabled && !m_BenchmarkMode) m_Prepass->Record(m_CommandBuffers[i],m_VertexBuffer,m_IndexBuffer);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_TimestampQueryPool,9);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_TimestampQueryPool,10);
        if(m_Prepass && m_RawAoEnabled && !m_BenchmarkMode) m_Prepass->RecordAo(m_CommandBuffers[i]);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_TimestampQueryPool,11);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_TimestampQueryPool,12);
        if(m_Prepass && m_RawAoEnabled && !m_BenchmarkMode) m_Prepass->RecordBlur(m_CommandBuffers[i]);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_TimestampQueryPool,13);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_TimestampQueryPool,6);
        if (m_HiZ && m_RunHiZ) m_HiZ->Record(m_CommandBuffers[i],m_VertexBuffer,m_IndexBuffer);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_TimestampQueryPool,7);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m_TimestampQueryPool,4);
        if (m_Culling && m_RunCulling) m_Culling->Record(m_CommandBuffers[i]);
        if (m_Inspector && m_InspectorEnabled && !m_BenchmarkMode) m_Inspector->RecordQuery(m_CommandBuffers[i]);
        vkCmdWriteTimestamp(m_CommandBuffers[i],VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m_TimestampQueryPool,5);

        // =====================================================
        // Shadow Pass
        // Query 0 = Shadow start
        // Query 1 = Shadow end
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

        vkCmdWriteTimestamp(
            m_CommandBuffers[i],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            m_TimestampQueryPool,
            0
        );

        vkCmdBeginRenderPass(
            m_CommandBuffers[i],
            &shadowPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
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

        vkCmdBindPipeline(
            m_CommandBuffers[i],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_ShadowPipeline.GetPipeline()
        );

        for (const SubMesh& subMesh : m_SubMeshes)
        {
            vkCmdDrawIndexed(
                m_CommandBuffers[i],
                subMesh.indexCount,
                subMesh.instanceCount,
                subMesh.firstIndex,
                0,
                subMesh.firstInstance
            );
        }

        vkCmdEndRenderPass(
            m_CommandBuffers[i]
        );

        vkCmdWriteTimestamp(
            m_CommandBuffers[i],
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            m_TimestampQueryPool,
            1
        );

        // =====================================================
        // Main Camera Pass
        // Query 2 = Main pass start
        // Query 3 = Main pass end
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

        vkCmdWriteTimestamp(
            m_CommandBuffers[i],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            m_TimestampQueryPool,
            2
        );

        // =====================================================
        // Begin main render pass
        // =====================================================

        vkCmdBeginRenderPass(
            m_CommandBuffers[i],
            &renderPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        // =====================================================
        // Skybox
        // =====================================================

        VkDescriptorSet descriptorSet =
            m_DescriptorManager.GetDescriptorSet();

        if (m_SkyboxEnabled)
        {
            vkCmdBindPipeline(
                m_CommandBuffers[i],
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_SkyboxPipeline.GetPipeline()
            );

            vkCmdBindDescriptorSets(
                m_CommandBuffers[i],
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_SkyboxPipeline.GetLayout(),
                0,
                1,
                &descriptorSet,
                0,
                nullptr
            );

            vkCmdDraw(
                m_CommandBuffers[i],
                36,
                1,
                0,
                0
            );
        }

        // =====================================================
        // Main scene
        // =====================================================

        vkCmdBindPipeline(
            m_CommandBuffers[i],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_GraphicsPipeline.GetPipeline()
        );

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

        uint32_t indirectIndex = 0;
        for (const SubMesh& subMesh : m_SubMeshes)
        {
            VkDescriptorSet materialSet = m_DescriptorManager.GetDescriptorSet(subMesh.materialId);
            vkCmdBindDescriptorSets(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_GraphicsPipeline.GetLayout(), 0, 1, &materialSet, 0, nullptr);
            MaterialPushConstants materialPushConstants{};

            if (subMesh.materialId >= 0 &&
                static_cast<size_t>(subMesh.materialId) < m_Materials.size())
            {
                const MaterialData& material =
                    m_Materials[subMesh.materialId];

                materialPushConstants.flags.x =
                    material.hasDiffuseTexture ? 1 : 0;

                materialPushConstants.flags.y = 0;
                for (int slot = 1; slot < 5; ++slot)
                    if (material.textures[slot] >= 0) materialPushConstants.flags.y |= (1 << (slot-1));
                materialPushConstants.emissive = glm::vec4(material.emissive, 0.0f);
                materialPushConstants.textureSettings = glm::vec4(material.normalScale, material.occlusionStrength, 0, 0);
                materialPushConstants.baseColor =
                    glm::vec4(
                        material.diffuseColor[0],
                        material.diffuseColor[1],
                        material.diffuseColor[2],
                        1.0f
                    );

                materialPushConstants.properties =
                    glm::vec4(
                        material.metallic,
                        material.roughness,
                        material.ambientOcclusion,
                        0.0f
                    );
            }
            else
            {
                // Safe fallback material.
                materialPushConstants.flags.x = 0;

                materialPushConstants.baseColor =
                    glm::vec4(
                        1.0f,
                        1.0f,
                        1.0f,
                        1.0f
                    );

                materialPushConstants.properties =
                    glm::vec4(
                        0.0f,  // Metallic
                        0.5f,  // Roughness
                        1.0f,  // AO
                        0.0f
                    );
            }

            const bool gpuDriven = m_Culling && m_RunCulling && subMesh.firstInstance > 0;
            materialPushConstants.flags.z = gpuDriven ? 1 : 0;
            if (gpuDriven) materialPushConstants.flags.w = int(m_Culling->DrawAddress(indirectIndex).visibleBase);
            vkCmdPushConstants(
                m_CommandBuffers[i],
                m_GraphicsPipeline.GetLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(MaterialPushConstants),
                &materialPushConstants
            );

            if (gpuDriven) {
                vkCmdDrawIndexedIndirect(m_CommandBuffers[i],m_Culling->IndirectBuffer().GetHandle(),
                    VkDeviceSize(m_Culling->DrawAddress(indirectIndex++).command)*sizeof(VkDrawIndexedIndirectCommand),1,sizeof(VkDrawIndexedIndirectCommand));
            } else vkCmdDrawIndexed(
                m_CommandBuffers[i],
                subMesh.indexCount,
                subMesh.instanceCount,
                subMesh.firstIndex,
                0,
                subMesh.firstInstance
            );
        }

        if (m_Debug && !m_BenchmarkMode) m_Debug->Record(m_CommandBuffers[i]);
        if (m_Inspector && m_InspectorEnabled && !m_BenchmarkMode) m_Inspector->RecordPanel(m_CommandBuffers[i]);
        if(m_Prepass && m_PrepassEnabled && !m_BenchmarkMode) m_Prepass->RecordPreview(m_CommandBuffers[i]);
        vkCmdEndRenderPass(
            m_CommandBuffers[i]
        );

        if(m_Prepass && m_PrepassEnabled && !m_BenchmarkMode) m_Prepass->RecordReadback(m_CommandBuffers[i],m_MainDepth);

        vkCmdWriteTimestamp(
            m_CommandBuffers[i],
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            m_TimestampQueryPool,
            3
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

bool CommandManager::GetGpuTimingResult(
    GpuTimingResult& result
) const
{
    std::array<uint64_t, 14> timestamps{};

    VkResult queryResult = vkGetQueryPoolResults(
        m_LogicalDevice.GetHandle(),
        m_TimestampQueryPool,
        0,
        static_cast<uint32_t>(timestamps.size()),
        sizeof(timestamps),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (queryResult == VK_NOT_READY)
    {
        return false;
    }

    if (queryResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to retrieve GPU timestamp results."
        );
    }

    const double nanosecondsPerTick =
        static_cast<double>(m_TimestampPeriod);

    const double shadowNanoseconds =
        static_cast<double>(
            timestamps[1] - timestamps[0]
            ) * nanosecondsPerTick;

    const double mainNanoseconds =
        static_cast<double>(
            timestamps[3] - timestamps[2]
            ) * nanosecondsPerTick;

    const double totalNanoseconds =
        static_cast<double>(
            timestamps[3] - timestamps[8]
            ) * nanosecondsPerTick;

    constexpr double nanosecondsToMilliseconds =
        1.0 / 1000000.0;

    result.blurMs = m_RawAoEnabled && !m_BenchmarkMode ? double(timestamps[13]-timestamps[12])*nanosecondsPerTick*nanosecondsToMilliseconds : 0.0;
    result.ssaoMs = m_RawAoEnabled && !m_BenchmarkMode ? double(timestamps[11]-timestamps[10])*nanosecondsPerTick*nanosecondsToMilliseconds : 0.0;
    result.prepassMs = m_PrepassEnabled && !m_BenchmarkMode ? double(timestamps[9]-timestamps[8])*nanosecondsPerTick*nanosecondsToMilliseconds : 0.0;
    result.hiZPassMs = double(timestamps[7]-timestamps[6])*nanosecondsPerTick*nanosecondsToMilliseconds;
    result.cullingPassMs = double(timestamps[5]-timestamps[4])*nanosecondsPerTick*nanosecondsToMilliseconds;
    result.shadowPassMs =
        shadowNanoseconds *
        nanosecondsToMilliseconds;

    result.mainPassMs =
        mainNanoseconds *
        nanosecondsToMilliseconds;

    result.totalGpuMs =
        totalNanoseconds *
        nanosecondsToMilliseconds;

    return true;
}
// Diagnostic readback only: caller waits for the render fence and has not presented yet.
void CommandManager::CaptureFrame(uint32_t imageIndex, const std::string& path) const {
    if (!m_Swapchain.SupportsCapture()) throw std::runtime_error("Surface does not support readback");
    const auto format = m_Swapchain.GetImageFormat();
    const bool bgra = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM;
    if (!bgra && format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_R8G8B8A8_UNORM)
        throw std::runtime_error("Unsupported screenshot pixel format");
    const auto extent = m_Swapchain.GetExtent();
    Buffer readback(m_PhysicalDevice, m_LogicalDevice, VkDeviceSize(extent.width) * extent.height * 4,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = m_CommandPool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(m_LogicalDevice.GetHandle(), &alloc, &cmd) != VK_SUCCESS) throw std::runtime_error("Capture command allocation failed");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Swapchain.GetImage(imageIndex);
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {extent.width,extent.height,1};
    vkCmdCopyImageToBuffer(cmd, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.GetHandle(), 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback.GetHandle(); host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    const auto queue = m_LogicalDevice.GetGraphicsQueue();
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(queue) != VK_SUCCESS)
        throw std::runtime_error("Capture submission failed");
    std::vector<unsigned char> pixels(size_t(extent.width)*extent.height*4);
    readback.DownloadData(pixels.data(), pixels.size());
    if (bgra) for (size_t i=0; i<pixels.size(); i+=4) std::swap(pixels[i],pixels[i+2]);
    vkFreeCommandBuffers(m_LogicalDevice.GetHandle(), m_CommandPool, 1, &cmd);
    if (!stbi_write_png(path.c_str(), extent.width, extent.height, 4, pixels.data(), extent.width*4)) throw std::runtime_error("Cannot write screenshot");
    std::cout << "[Capture] " << path << std::endl;
}

void CommandManager::SetBenchmarkMode(int mode) {
    if (mode < 0 || mode > 2) throw std::runtime_error("Invalid culling benchmark mode");
    if (vkDeviceWaitIdle(m_LogicalDevice.GetHandle()) != VK_SUCCESS)
        throw std::runtime_error("Cannot wait before benchmark command recording");
    if (m_Culling) m_Culling->SetHiZSampling(mode == 2);
    m_BenchmarkMode = true;
    m_RunCulling = mode != 0;
    m_RunHiZ = mode == 2;
    RecordCommandBuffers();
}

void CommandManager::SetInspectorEnabled(bool enabled) {
    if(m_InspectorEnabled==enabled) return;
    if(vkDeviceWaitIdle(m_LogicalDevice.GetHandle())!=VK_SUCCESS) throw std::runtime_error("Cannot wait for inspector toggle");
    m_InspectorEnabled=enabled;
    RecordCommandBuffers();
}

void CommandManager::SetPrepassEnabled(bool enabled,bool rawAo,int debugMode) {
    if(m_PrepassEnabled==enabled && m_RawAoEnabled==rawAo && m_PrepassMode==debugMode) return;
    if(vkDeviceWaitIdle(m_LogicalDevice.GetHandle())!=VK_SUCCESS) throw std::runtime_error("Cannot wait for prepass toggle");
    m_PrepassEnabled=enabled;m_RawAoEnabled=rawAo;m_PrepassMode=debugMode;RecordCommandBuffers();
}
