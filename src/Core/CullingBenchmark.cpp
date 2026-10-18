#include "BenchmarkStats.h"
#include "GpuCulling.h"
#include "HiZ.h"
#include "Renderer.h"
#include "VulkanContext.h"
#include "Window.h"
#include <array>
#include <vector>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <iostream>
#include <stdexcept>

void VulkanContext::RunCullingBenchmark(const std::string &path, int warmup, int samples, int repeats,
                                        bool captures)
{
    if (!m_HiZ || !m_GpuCulling || m_VerifyCulling || m_CullingFrozen)
        throw std::runtime_error(
            "Benchmark requires the static wall stress scene, no verification or observer");
    std::ofstream raw(path);
    std::ofstream summary(path + ".summary.csv");
    if (!raw || !summary)
        throw std::runtime_error("Cannot open benchmark CSV output");
    const auto extent = m_Swapchain->GetExtent();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_PhysicalDevice->GetHandle(), &properties);
    const char *modes[] = {"direct", "frustum", "frustum_hiz"};
    const char *scenes[] = {"wall_front", "wall_edge", "side_view"};
    const float yaws[] = {0.f, 35.f, 90.f};
    const std::string header =
        "gpu,driver_version,width,height,instances,scene,yaw_deg,pitch_deg,distance,shadow_mode,mode,repeat,"
        "warmup_frames,sample,visible,frustum_survivors,occluded,hiz_ms,cull_ms,shadow_ms,main_ms,total_ms";
    raw << header << "\n" << std::fixed << std::setprecision(6);
    summary << "gpu,driver_version,width,height,instances,scene,yaw_deg,pitch_deg,distance,shadow_mode,mode,"
               "samples,total_mean_ms,total_median_ms,total_p95_ms,hiz_mean_ms,cull_mean_ms,shadow_mean_ms,"
               "main_mean_ms,visible\n"
            << std::fixed << std::setprecision(6);
    m_Renderer->SetQuiet(true);
    m_PointLightAnimationEnabled = false;
    // Lock interactive settings for reproducible static-camera samples.
    glfwSetKeyCallback(m_Window.GetHandle(), nullptr);
    glfwSetCursorPosCallback(m_Window.GetHandle(), nullptr);
    glfwSetMouseButtonCallback(m_Window.GetHandle(), nullptr);
    glfwSetScrollCallback(m_Window.GetHandle(), nullptr);
    const float pitch = m_CameraPitch, distance = m_CameraDistance;
    for (int scene = 0; scene < 3; ++scene)
    {
        std::array<std::vector<CommandManager::GpuTimingResult>, 3> timings;
        std::array<uint32_t, 3> visible{};
        for (int repeat = 0; repeat < repeats; ++repeat)
            for (int slot = 0; slot < 3; ++slot)
            {
                const int mode = (slot + repeat) % 3;
                m_Renderer->WaitForFrame();
                m_GpuCulling->ReadCompleted();
                m_BenchmarkDirect = mode == 0;
                m_CullingEnabled = mode != 0;
                m_OcclusionEnabled = mode == 2;
                // Re-record outside measurement. Device idle also protects descriptor mutation.
                m_CommandManager->SetBenchmarkMode(mode);
                std::cout << "[Culling Benchmark] " << scenes[scene] << " / " << modes[mode] << " / repeat "
                          << repeat + 1 << std::endl;
                for (int frame = 0; frame < warmup + samples; ++frame)
                {
                    m_Window.PollEvents();
                    if (m_Window.ShouldClose())
                        throw std::runtime_error("Benchmark cancelled; CSV may be incomplete");
                    m_CameraYaw = glm::radians(yaws[scene]);
                    m_CameraPitch = pitch;
                    m_CameraDistance = distance;
                    UpdateUniformBuffer();
                    if (captures && repeat == 0 && frame == 0)
                        CaptureNextFrame(path + "." + scenes[scene] + "." + modes[mode] + ".png");
                    DrawFrame();
                    // Queries and telemetry now belong to THIS submitted frame, not the prior mode.
                    m_Renderer->WaitForFrame();
                    if (mode != 0)
                        m_GpuCulling->ReadCompleted();
                    if (frame < warmup)
                        continue;
                    CommandManager::GpuTimingResult timing{};
                    if (!m_CommandManager->GetGpuTimingResult(timing))
                        throw std::runtime_error("GPU timestamps unavailable after fence");
                    if (mode != 2)
                        timing.hiZPassMs = 0; // skipped timestamp bracket is not a Hi-Z pass
                    if (mode == 0)
                        timing.cullingPassMs = 0;
                    timings[mode].push_back(timing);
                    const auto count = mode == 0 ? m_GpuCulling->TotalCount() : m_GpuCulling->VisibleCount();
                    visible[mode] = count;
                    raw << std::quoted(properties.deviceName) << ',' << properties.driverVersion << ','
                        << extent.width << ',' << extent.height << ',' << m_StressCount << ','
                        << scenes[scene] << ',' << yaws[scene] << ',' << glm::degrees(pitch) << ','
                        << distance << ',' << m_ShadowMode << ',' << modes[mode] << ',' << repeat + 1 << ','
                        << warmup << ',' << frame - warmup << ',' << count << ','
                        << (mode == 0 ? count : m_GpuCulling->FrustumCount()) << ','
                        << (mode == 0 ? 0 : m_GpuCulling->OccludedCount()) << ',' << timing.hiZPassMs << ','
                        << timing.cullingPassMs << ',' << timing.shadowPassMs << ',' << timing.mainPassMs
                        << ',' << timing.totalGpuMs << "\n";
                }
            }
        for (int mode = 0; mode < 3; ++mode)
        {
            std::vector<double> totals, hiz, cull, shadow, main;
            for (const auto &t : timings[mode])
            {
                totals.push_back(t.totalGpuMs);
                hiz.push_back(t.hiZPassMs);
                cull.push_back(t.cullingPassMs);
                shadow.push_back(t.shadowPassMs);
                main.push_back(t.mainPassMs);
            }
            const auto stats = SummarizeTimings(totals);
            summary << std::quoted(properties.deviceName) << ',' << properties.driverVersion << ','
                    << extent.width << ',' << extent.height << ',' << m_StressCount << ',' << scenes[scene]
                    << ',' << yaws[scene] << ',' << glm::degrees(pitch) << ',' << distance << ','
                    << m_ShadowMode << ',' << modes[mode] << ',' << totals.size() << ',' << stats.mean << ','
                    << stats.median << ',' << stats.p95 << ',' << SummarizeTimings(hiz).mean << ','
                    << SummarizeTimings(cull).mean << ',' << SummarizeTimings(shadow).mean << ','
                    << SummarizeTimings(main).mean << ',' << visible[mode] << "\n";
            std::cout << "[Culling Benchmark Result] " << scenes[scene] << " / " << modes[mode] << " mean "
                      << stats.mean << " ms, p95 " << stats.p95 << " ms" << std::endl;
        }
        raw.flush();
        summary.flush();
        if (!raw || !summary)
            throw std::runtime_error("Cannot write benchmark CSV");
    }
    std::cout << "[Culling Benchmark] Saved " << path << " and " << path << ".summary.csv" << std::endl;
}
