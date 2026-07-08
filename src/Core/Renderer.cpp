#include "Renderer.h"

#include <iostream>
#include <stdexcept>

Renderer::Renderer(const LogicalDevice& logicalDevice,
    const Swapchain& swapchain,
    const CommandManager& commandManager)
    : m_LogicalDevice(logicalDevice),
    m_Swapchain(swapchain),
    m_CommandManager(commandManager)
{
    CreateSyncObjects();

    std::cout << "Renderer created successfully." << std::endl;
}

Renderer::~Renderer()
{
    VkDevice device = m_LogicalDevice.GetHandle();

    if (m_InFlightFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, m_InFlightFence, nullptr);
    }

    if (m_RenderFinishedSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, m_RenderFinishedSemaphore, nullptr);
    }

    if (m_ImageAvailableSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(device, m_ImageAvailableSemaphore, nullptr);
    }
}

void Renderer::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkDevice device = m_LogicalDevice.GetHandle();

    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &m_InFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create renderer synchronization objects.");
    }
}

void Renderer::DrawFrame()
{
    VkDevice device = m_LogicalDevice.GetHandle();

    vkWaitForFences(device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &m_InFlightFence);

    uint32_t imageIndex = 0;

    VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        m_Swapchain.GetHandle(),
        UINT64_MAX,
        m_ImageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] =
    {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkCommandBuffer commandBuffer =
        m_CommandManager.GetCommandBuffer(imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(
        m_LogicalDevice.GetGraphicsQueue(),
        1,
        &submitInfo,
        m_InFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer.");
    }

    VkSwapchainKHR swapchains[] = { m_Swapchain.GetHandle() };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(
        m_LogicalDevice.GetPresentQueue(),
        &presentInfo
    );

    if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image.");
    }
}