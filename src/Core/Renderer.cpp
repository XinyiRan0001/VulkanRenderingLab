#include "Renderer.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <GLFW/glfw3.h>


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
    std::cout << "[DrawFrame] Begin" << std::endl;

    VkDevice device = m_LogicalDevice.GetHandle();

    VkResult waitResult = vkWaitForFences(
        device,
        1,
        &m_InFlightFence,
        VK_TRUE,
        UINT64_MAX
    );

    std::cout
        << "[DrawFrame] Fence wait result: "
        << static_cast<int>(waitResult)
        << std::endl;

    if (waitResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "vkWaitForFences failed: " +
            std::to_string(static_cast<int>(waitResult))
        );
    }

    if (m_HasSubmittedFrame)
    {
        CommandManager::GpuTimingResult timing{};

        if (m_CommandManager.GetGpuTimingResult(timing))
        {
            // =========================================================
            // Benchmark accumulation
            // =========================================================

            if (m_BenchmarkRunning)
            {
                m_BenchmarkShadowAccumulator +=
                    timing.shadowPassMs;

                m_BenchmarkMainAccumulator +=
                    timing.mainPassMs;

                m_BenchmarkTotalAccumulator +=
                    timing.totalGpuMs;

                m_BenchmarkSampleCount++;

                const double benchmarkElapsed =
                    glfwGetTime() -
                    m_BenchmarkStartTime;

                if (benchmarkElapsed >=
                    m_BenchmarkDuration)
                {
                    const double sampleCount =
                        static_cast<double>(
                            m_BenchmarkSampleCount
                            );

                    const double averageShadowMs =
                        m_BenchmarkShadowAccumulator /
                        sampleCount;

                    const double averageMainMs =
                        m_BenchmarkMainAccumulator /
                        sampleCount;

                    const double averageTotalMs =
                        m_BenchmarkTotalAccumulator /
                        sampleCount;

                    const char* benchmarkModeName =
                        "Unknown";

                    switch (m_BenchmarkShadowMode)
                    {
                    case 1:
                        benchmarkModeName =
                            "Hard Shadow";
                        break;

                    case 2:
                        benchmarkModeName =
                            "PCF";
                        break;

                    case 3:
                        benchmarkModeName =
                            "PCSS";
                        break;

                    case 4:
                        benchmarkModeName =
                            "Shadow Map Debug";
                        break;
                    }

                    std::cout
                        << "\n"
                        << "========================================"
                        << "\n"
                        << "[GPU Benchmark Result]"
                        << "\n"
                        << "Mode:        "
                        << benchmarkModeName
                        << "\n"
                        << std::fixed
                        << std::setprecision(3)
                        << "Shadow Pass: "
                        << averageShadowMs
                        << " ms"
                        << "\n"
                        << "Main Pass:   "
                        << averageMainMs
                        << " ms"
                        << "\n"
                        << "Total GPU:   "
                        << averageTotalMs
                        << " ms"
                        << "\n"
                        << "Samples:     "
                        << m_BenchmarkSampleCount
                        << "\n"
                        << "========================================"
                        << "\n"
                        << std::endl;

                    m_BenchmarkRunning = false;
                }
            }

            // =========================================================
            // Regular 0.5-second GPU timing average
            // =========================================================

            m_ShadowTimingAccumulator +=
                timing.shadowPassMs;

            m_MainTimingAccumulator +=
                timing.mainPassMs;

            m_GpuTimingAccumulator +=
                timing.totalGpuMs;

            m_GpuTimingSampleCount++;

            const double currentTime =
                glfwGetTime();

            if (currentTime -
                m_LastGpuTimingPrintTime >= 0.5)
            {
                const double sampleCount =
                    static_cast<double>(
                        m_GpuTimingSampleCount
                        );

                const double averageShadowMs =
                    m_ShadowTimingAccumulator /
                    sampleCount;

                const double averageMainMs =
                    m_MainTimingAccumulator /
                    sampleCount;

                const double averageTotalMs =
                    m_GpuTimingAccumulator /
                    sampleCount;

                std::cout
                    << std::fixed
                    << std::setprecision(3)
                    << "[GPU Timing Average] "
                    << "Shadow: "
                    << averageShadowMs
                    << " ms | Main: "
                    << averageMainMs
                    << " ms | Total: "
                    << averageTotalMs
                    << " ms | Samples: "
                    << m_GpuTimingSampleCount
                    << std::endl;

                m_ShadowTimingAccumulator = 0.0;
                m_MainTimingAccumulator = 0.0;
                m_GpuTimingAccumulator = 0.0;
                m_GpuTimingSampleCount = 0;

                m_LastGpuTimingPrintTime =
                    currentTime;
            }
        }
    }

    uint32_t imageIndex = 0;

    VkResult acquireResult = vkAcquireNextImageKHR(
        device,
        m_Swapchain.GetHandle(),
        UINT64_MAX,
        m_ImageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    std::cout
    << "[DrawFrame] Acquire result: "
    << static_cast<int>(acquireResult)
    << ", image index: "
    << imageIndex
    << std::endl;

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return;
    }

    if (acquireResult != VK_SUCCESS &&
        acquireResult != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error(
            "Failed to acquire swapchain image. VkResult = " +
            std::to_string(static_cast<int>(acquireResult))
        );
    }

    vkResetFences(
        device,
        1,
        &m_InFlightFence
    );

    if (acquireResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to acquire swapchain image."
        );
    }

    VkSemaphore waitSemaphores[] =
    {
        m_ImageAvailableSemaphore
    };

    VkPipelineStageFlags waitStages[] =
    {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkCommandBuffer commandBuffer =
        m_CommandManager.GetCommandBuffer(
            imageIndex
        );

    VkSubmitInfo submitInfo{};
    submitInfo.sType =
        VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores =
        waitSemaphores;

    submitInfo.pWaitDstStageMask =
        waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers =
        &commandBuffer;

    VkSemaphore signalSemaphores[] =
    {
        m_RenderFinishedSemaphore
    };

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores =
        signalSemaphores;

    std::cout << "[DrawFrame] Before queue submit" << std::endl;

    VkResult submitResult = vkQueueSubmit(
        m_LogicalDevice.GetGraphicsQueue(),
        1,
        &submitInfo,
        m_InFlightFence
    );

    std::cout
    << "[DrawFrame] Submit result: "
    << static_cast<int>(submitResult)
    << std::endl;

    if (submitResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to submit draw command buffer. VkResult = " +
            std::to_string(static_cast<int>(submitResult))
        );
    }

    m_HasSubmittedFrame = true;

    VkSwapchainKHR swapchains[] =
    {
        m_Swapchain.GetHandle()
    };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType =
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores =
        signalSemaphores;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains =
        swapchains;

    presentInfo.pImageIndices =
        &imageIndex;

    std::cout << "[DrawFrame] Before present" << std::endl;
    VkResult presentResult =
        vkQueuePresentKHR(
            m_LogicalDevice.GetPresentQueue(),
            &presentInfo
        );
    std::cout
        << "[DrawFrame] Present result: "
        << static_cast<int>(presentResult)
        << std::endl;
    if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to present swapchain image."
        );
    }
    std::cout << "[DrawFrame] End" << std::endl;
}

void Renderer::StartBenchmark(int shadowMode)
{
    if (m_BenchmarkRunning)
    {
        std::cout
            << "[Benchmark] Benchmark is already running."
            << std::endl;

        return;
    }

    m_BenchmarkRunning = true;
    m_BenchmarkShadowMode = shadowMode;

    m_BenchmarkStartTime =
        glfwGetTime();

    m_BenchmarkShadowAccumulator = 0.0;
    m_BenchmarkMainAccumulator = 0.0;
    m_BenchmarkTotalAccumulator = 0.0;

    m_BenchmarkSampleCount = 0;

    const char* benchmarkModeName =
        "Unknown";

    switch (m_BenchmarkShadowMode)
    {
    case 1:
        benchmarkModeName =
            "Hard Shadow";
        break;

    case 2:
        benchmarkModeName =
            "PCF";
        break;

    case 3:
        benchmarkModeName =
            "PCSS";
        break;

    case 4:
        benchmarkModeName =
            "Shadow Map Debug";
        break;
    }

    std::cout
        << "[Benchmark] Started "
        << m_BenchmarkDuration
        << "-second GPU benchmark"
        << " | Mode: "
        << benchmarkModeName
        << std::endl;
}