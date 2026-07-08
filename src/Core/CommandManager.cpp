#include "CommandManager.h"

#include <iostream>
#include <stdexcept>

CommandManager::CommandManager(const LogicalDevice& logicalDevice,
    const PhysicalDevice& physicalDevice,
    const Framebuffer& framebuffer,
    const RenderPass& renderPass,
    const Swapchain& swapchain)
    : m_LogicalDevice(logicalDevice),
    m_PhysicalDevice(physicalDevice),
    m_Framebuffer(framebuffer),
    m_RenderPass(renderPass),
    m_Swapchain(swapchain)
{
    CreateCommandPool();
    AllocateCommandBuffers();
    RecordCommandBuffers();

    std::cout << "CommandManager created successfully." << std::endl;
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
    const auto& framebuffers = m_Framebuffer.GetHandles();

    for (size_t i = 0; i < m_CommandBuffers.size(); i++)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(m_CommandBuffers[i], &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin recording command buffer.");
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_RenderPass.GetHandle();
        renderPassInfo.framebuffer = framebuffers[i];

        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = m_Swapchain.GetExtent();

        VkClearValue clearColor{};
        clearColor.color = { {0.02f, 0.02f, 0.04f, 1.0f} };

        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(
            m_CommandBuffers[i],
            &renderPassInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        vkCmdEndRenderPass(m_CommandBuffers[i]);

        if (vkEndCommandBuffer(m_CommandBuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer.");
        }
    }

    std::cout << "Command buffers recorded successfully." << std::endl;
}