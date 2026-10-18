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

    for (auto semaphore : m_RenderFinishedSemaphores)
        if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device,semaphore,nullptr);

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

    try {
        if (vkCreateSemaphore(device,&semaphoreInfo,nullptr,&m_ImageAvailableSemaphore)!=VK_SUCCESS ||
            vkCreateFence(device,&fenceInfo,nullptr,&m_InFlightFence)!=VK_SUCCESS)
            throw std::runtime_error("Failed to create frame synchronization objects");
        m_RenderFinishedSemaphores.resize(m_Swapchain.GetImageCount(),VK_NULL_HANDLE);
        for(auto& semaphore : m_RenderFinishedSemaphores)
            if(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&semaphore)!=VK_SUCCESS)
                throw std::runtime_error("Failed to create per-image present semaphore");
    } catch(...) {
        for(auto semaphore : m_RenderFinishedSemaphores)
            if(semaphore) vkDestroySemaphore(device,semaphore,nullptr);
        if(m_ImageAvailableSemaphore) vkDestroySemaphore(device,m_ImageAvailableSemaphore,nullptr);
        if(m_InFlightFence) vkDestroyFence(device,m_InFlightFence,nullptr);
        throw;
    }
}

void Renderer::WaitForFrame() const
{
    if (vkWaitForFences(m_LogicalDevice.GetHandle(), 1, &m_InFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        throw std::runtime_error("Failed waiting for the frame before updating uniforms.");
}

void Renderer::DrawFrame()
{

    VkDevice device = m_LogicalDevice.GetHandle();

    VkResult waitResult = vkWaitForFences(
        device,
        1,
        &m_InFlightFence,
        VK_TRUE,
        UINT64_MAX
    );

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
                m_BenchmarkHiZAccumulator += timing.hiZPassMs;
                m_BenchmarkCullingAccumulator += timing.cullingPassMs;
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
                        << "HiZ Setup:   " << m_BenchmarkHiZAccumulator / sampleCount << " ms\n"
                        << "Cull/Compact: " << m_BenchmarkCullingAccumulator / sampleCount << " ms\n"
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

            m_HiZTimingAccumulator += timing.hiZPassMs;
            m_CullingTimingAccumulator += timing.cullingPassMs;
            m_ShadowTimingAccumulator +=
                timing.shadowPassMs;

            m_MainTimingAccumulator +=
                timing.mainPassMs;

            m_GpuTimingAccumulator +=
                timing.totalGpuMs;

            m_GpuTimingSampleCount++;

            const double currentTime =
                glfwGetTime();

            if (!m_Quiet && currentTime -
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
                    << "HiZ Setup: " << m_HiZTimingAccumulator / sampleCount << " ms | Cull/Compact: " << m_CullingTimingAccumulator / sampleCount << " ms | Shadow: "
                    << averageShadowMs
                    << " ms | Main: "
                    << averageMainMs
                    << " ms | Total: "
                    << averageTotalMs
                    << " ms | Samples: "
                    << m_GpuTimingSampleCount
                    << std::endl;

                m_HiZTimingAccumulator = 0.0;
                m_CullingTimingAccumulator = 0.0;
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

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (m_Quiet) throw std::runtime_error("Swapchain changed during benchmark; rerun with a fixed window");
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
        m_RenderFinishedSemaphores.at(imageIndex)
    };

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores =
        signalSemaphores;

    VkResult submitResult = vkQueueSubmit(
        m_LogicalDevice.GetGraphicsQueue(),
        1,
        &submitInfo,
        m_InFlightFence
    );

    if (submitResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to submit draw command buffer. VkResult = " +
            std::to_string(static_cast<int>(submitResult))
        );
    }

    m_HasSubmittedFrame = true;
    if (!m_CapturePath.empty()) {
        WaitForFrame();
        m_CommandManager.CaptureFrame(imageIndex, m_CapturePath);
        m_CapturePath.clear();
    }

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

    VkResult presentResult =
        vkQueuePresentKHR(
            m_LogicalDevice.GetPresentQueue(),
            &presentInfo
        );
    if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to present swapchain image."
        );
    }
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

    m_BenchmarkHiZAccumulator = 0.0;
    m_BenchmarkCullingAccumulator = 0.0;
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