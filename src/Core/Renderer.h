#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "Swapchain.h"
#include "CommandManager.h"
#include <vector>

class Renderer
{
public:
    Renderer(const LogicalDevice& logicalDevice,
        const Swapchain& swapchain,
        const CommandManager& commandManager);

    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool DrawFrame();
    bool NeedsRebuild() const { return m_NeedsRebuild; }
    void CaptureNextFrame(const std::string& path) { m_CapturePath = path; }
    void WaitForFrame() const;
    void StartBenchmark(int shadowMode);
    void SetQuiet(bool quiet) { m_Quiet = quiet; }

private:
    const LogicalDevice& m_LogicalDevice;
    const Swapchain& m_Swapchain;
    const CommandManager& m_CommandManager;

    VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_RenderFinishedSemaphores; // one per swapchain image, not per CPU frame
    VkFence m_InFlightFence = VK_NULL_HANDLE;

    std::string m_CapturePath;
    bool m_HasSubmittedFrame = false;
    bool m_Quiet = false;
    bool m_NeedsRebuild = false;
    int m_BenchmarkShadowMode = 0;

    double m_GpuTimingAccumulator = 0.0;
    double m_BlurTimingAccumulator = 0.0;
    double m_SsaoTimingAccumulator = 0.0;
    double m_PrepassTimingAccumulator = 0.0;
    double m_HiZTimingAccumulator = 0.0;
    double m_CullingTimingAccumulator = 0.0;
    double m_ShadowTimingAccumulator = 0.0;
    double m_MainTimingAccumulator = 0.0;

    double m_LastGpuTimingPrintTime = 0.0;

    uint32_t m_GpuTimingSampleCount = 0;

    bool m_BenchmarkRunning = false;

    double m_BenchmarkStartTime = 0.0;
    double m_BenchmarkDuration = 5.0;

    double m_BenchmarkHiZAccumulator = 0.0;
    double m_BenchmarkCullingAccumulator = 0.0;
    double m_BenchmarkShadowAccumulator = 0.0;
    double m_BenchmarkMainAccumulator = 0.0;
    double m_BenchmarkTotalAccumulator = 0.0;

    uint32_t m_BenchmarkSampleCount = 0;

private:
    void CreateSyncObjects();
};