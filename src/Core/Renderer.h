#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "Swapchain.h"
#include "CommandManager.h"

class Renderer
{
public:
    Renderer(const LogicalDevice& logicalDevice,
        const Swapchain& swapchain,
        const CommandManager& commandManager);

    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void DrawFrame();
    void StartBenchmark(int shadowMode);

private:
    const LogicalDevice& m_LogicalDevice;
    const Swapchain& m_Swapchain;
    const CommandManager& m_CommandManager;

    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence m_InFlightFence = VK_NULL_HANDLE;

    bool m_HasSubmittedFrame = false;
    int m_BenchmarkShadowMode = 0;

    double m_GpuTimingAccumulator = 0.0;
    double m_ShadowTimingAccumulator = 0.0;
    double m_MainTimingAccumulator = 0.0;

    double m_LastGpuTimingPrintTime = 0.0;

    uint32_t m_GpuTimingSampleCount = 0;

    bool m_BenchmarkRunning = false;

    double m_BenchmarkStartTime = 0.0;
    double m_BenchmarkDuration = 5.0;

    double m_BenchmarkShadowAccumulator = 0.0;
    double m_BenchmarkMainAccumulator = 0.0;
    double m_BenchmarkTotalAccumulator = 0.0;

    uint32_t m_BenchmarkSampleCount = 0;

private:
    void CreateSyncObjects();
};