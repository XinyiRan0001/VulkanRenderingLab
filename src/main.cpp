#include "Core/VulkanContext.h"
#include "Core/Window.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include "../third_party/renderdoc/renderdoc_app.h"
#include <windows.h>
#endif

#include <GLFW/glfw3.h>

int main(int argc, char **argv)
{
    try
    {
        bool renderdocCapture = false;
        bool aoCycleTest = false;
        int prepassMode = 0;
        bool verifyPrepass = false;
        float depthRange = 10.f;
        float aoRadius = .5f, aoBias = .025f;
        int aoSamples = 32;
        int width = 1280, height = 720, resizeAfter = 0, resizeWidth = 1024, resizeHeight = 768;
        std::string scenePath;
        std::string benchmarkPath;
        bool benchmarkCaptures = false;
        int benchmarkWarmup = 30, benchmarkSamples = 120, benchmarkRepeats = 3;
        int frameLimit = 0;
        uint32_t stressCount = 0;
        std::string capturePath;
        int shadowMode = 3;
        float yaw = 0.0f, pitch = 8.594367f;
        bool paused = false;
        bool cullingObserver = false;
        bool hiZInspector = false;
        int inspectInstance = -1, inspectMip = 0, inspectGroup = 0;
        bool mixedScene = false;
        bool culling = true, verifyCulling = false;
        bool occlusionScene = false, occlusion = true, pitchSpecified = false;
        float cameraDistance = 0.0f, targetOffsetX = 0.0f, cameraSweep = 0.0f;
        int materialView = 0, lightingView = 0;
        float environmentDegrees = 0.0f;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--ao-cycle-test")
                aoCycleTest = true;
            else if (option == "--renderdoc-capture")
                renderdocCapture = true;
            else if (option == "--ssao")
                prepassMode = 7;
            else if (option == "--ssao-blurred")
                prepassMode = 6;
            else if (option == "--no-ssao")
                prepassMode = 0;
            else if (option == "--ssao-view")
                prepassMode = 5;
            else if (option == "--ao-radius" && i + 1 < argc)
                aoRadius = std::stof(argv[++i]);
            else if (option == "--ao-bias" && i + 1 < argc)
                aoBias = std::stof(argv[++i]);
            else if (option == "--ao-samples" && i + 1 < argc)
                aoSamples = std::stoi(argv[++i]);
            else if (option == "--prepass")
                prepassMode = 4;
            else if (option == "--verify-prepass")
            {
                verifyPrepass = true;
                if (prepassMode == 0)
                    prepassMode = 4;
            }
            else if (option == "--prepass-view" && i + 1 < argc)
            {
                const std::string view = argv[++i];
                if (view == "normal")
                    prepassMode = 1;
                else if (view == "depth")
                    prepassMode = 2;
                else if (view == "world-normal")
                    prepassMode = 3;
                else
                    throw std::runtime_error("--prepass-view must be normal, depth or world-normal");
            }
            else if (option == "--depth-range" && i + 1 < argc)
                depthRange = std::stof(argv[++i]);
            else if (option == "--width" && i + 1 < argc)
                width = std::stoi(argv[++i]);
            else if (option == "--height" && i + 1 < argc)
                height = std::stoi(argv[++i]);
            else if (option == "--resize-after" && i + 1 < argc)
                resizeAfter = std::stoi(argv[++i]);
            else if (option == "--resize-width" && i + 1 < argc)
                resizeWidth = std::stoi(argv[++i]);
            else if (option == "--resize-height" && i + 1 < argc)
                resizeHeight = std::stoi(argv[++i]);
            else if (option == "--benchmark-culling" && i + 1 < argc)
                benchmarkPath = argv[++i];
            else if (option == "--benchmark-captures")
                benchmarkCaptures = true;
            else if (option == "--benchmark-warmup" && i + 1 < argc)
                benchmarkWarmup = std::stoi(argv[++i]);
            else if (option == "--benchmark-samples" && i + 1 < argc)
                benchmarkSamples = std::stoi(argv[++i]);
            else if (option == "--benchmark-repeats" && i + 1 < argc)
                benchmarkRepeats = std::stoi(argv[++i]);
            else if (option == "--scene" && i + 1 < argc)
                scenePath = argv[++i];
            else if (option == "--stress-count" && i + 1 < argc)
            {
                const int count = std::stoi(argv[++i]);
                if (count < 1 || count > 10000)
                    throw std::runtime_error("--stress-count must be 1..10000");
                stressCount = static_cast<uint32_t>(count);
            }
            else if (option == "--capture" && i + 1 < argc)
                capturePath = argv[++i];
            else if (option == "--shadow-mode" && i + 1 < argc)
                shadowMode = std::stoi(argv[++i]);
            else if (option == "--yaw" && i + 1 < argc)
                yaw = std::stof(argv[++i]);
            else if (option == "--pitch" && i + 1 < argc)
            {
                pitch = std::stof(argv[++i]);
                pitchSpecified = true;
            }
            else if (option == "--camera-sweep" && i + 1 < argc)
                cameraSweep = std::stof(argv[++i]);
            else if (option == "--camera-distance" && i + 1 < argc)
                cameraDistance = std::stof(argv[++i]);
            else if (option == "--target-offset-x" && i + 1 < argc)
                targetOffsetX = std::stof(argv[++i]);
            else if (option == "--mixed-scene")
            {
                mixedScene = true;
                occlusionScene = true;
            }
            else if (option == "--inspect-group" && i + 1 < argc)
            {
                inspectGroup = std::stoi(argv[++i]);
                hiZInspector = true;
            }
            else if (option == "--hiz-inspector")
                hiZInspector = true;
            else if (option == "--inspect-instance" && i + 1 < argc)
            {
                inspectInstance = std::stoi(argv[++i]);
                hiZInspector = true;
            }
            else if (option == "--hiz-mip" && i + 1 < argc)
            {
                inspectMip = std::stoi(argv[++i]);
                hiZInspector = true;
            }
            else if (option == "--occlusion-scene")
                occlusionScene = true;
            else if (option == "--culling-observer")
                cullingObserver = true;
            else if (option == "--no-occlusion")
                occlusion = false;
            else if (option == "--no-culling")
                culling = false;
            else if (option == "--verify-culling")
                verifyCulling = true;
            else if (option == "--paused")
                paused = true;
            else if (option == "--material-view" && i + 1 < argc)
                materialView = std::stoi(argv[++i]);
            else if (option == "--lighting-view" && i + 1 < argc)
                lightingView = std::stoi(argv[++i]);
            else if (option == "--environment-degrees" && i + 1 < argc)
                environmentDegrees = std::stof(argv[++i]);
            else if (option == "--frames" && i + 1 < argc)
            {
                frameLimit = std::stoi(argv[++i]);
                if (frameLimit <= 0)
                    throw std::runtime_error("--frames must be positive");
            }
            else
                throw std::runtime_error(
                    "Usage: VulkanRenderingLab [--ssao] [--ssao-blurred] [--ssao-view] [--no-ssao] "
                    "[--ao-radius value] [--ao-bias value] "
                    "[--ao-samples 1..64] [--prepass] [--prepass-view normal|depth|world-normal] "
                    "[--verify-prepass] [--depth-range value] [--width pixels] [--height pixels] [--scene "
                    "path.gltf|path.glb] [--frames count] [--stress-count "
                    "1..10000] [--benchmark-culling output.csv] [--benchmark-warmup 30] [--benchmark-samples "
                    "120] [--benchmark-repeats 3] [--benchmark-captures] [--mixed-scene] [--occlusion-scene] "
                    "[--inspect-group index] [--hiz-inspector] [--hiz-mip -1..11] [--inspect-instance "
                    "offset] [--culling-observer] [--no-occlusion] [--no-culling] [--verify-culling] "
                    "[--camera-distance value] [--target-offset-x value] [--material-view 0..5] "
                    "[--lighting-view 0..3] [--environment-degrees angle]");
        }
        if (width < 64 || height < 64 || width > 4096 || height > 4096 || resizeWidth < 64 ||
            resizeHeight < 64 || resizeWidth > 4096 || resizeHeight > 4096 || resizeAfter < 0)
            throw std::runtime_error("Invalid window/resize dimensions (64..4096)");
        if (resizeAfter && (!frameLimit || resizeAfter >= frameLimit))
            throw std::runtime_error("--resize-after requires a larger --frames limit");
        if (!benchmarkPath.empty())
        {
            if (prepassMode || resizeAfter || !scenePath.empty() || cullingObserver || hiZInspector ||
                verifyCulling || frameLimit || cameraSweep != 0 || !capturePath.empty())
                throw std::runtime_error("Culling benchmark cannot combine scene, observer, verification, "
                                         "frames, sweep or capture");
            if (benchmarkWarmup < 1 || benchmarkSamples < 2 || benchmarkRepeats < 1 ||
                benchmarkRepeats > 20 || benchmarkWarmup > 10000 || benchmarkSamples > 10000)
                throw std::runtime_error(
                    "Invalid benchmark warmup (1..10000), samples (2..10000), repeats (1..20)");
            occlusionScene = true;
            paused = true;
        }
        if (aoCycleTest && (!frameLimit || frameLimit < 8 || !benchmarkPath.empty()))
            throw std::runtime_error("--ao-cycle-test requires --frames >=8 and no benchmark");
        if (cameraSweep != 0.0f && !frameLimit)
            throw std::runtime_error("--camera-sweep requires a bounded --frames run");
        if (occlusionScene && !stressCount)
            stressCount = 1600;
        if (occlusionScene && !pitchSpecified)
            pitch = 25.0f;
#ifdef _WIN32
        RENDERDOC_API_1_0_0 *renderdoc = nullptr;
        if (renderdocCapture)
        {
            auto module = GetModuleHandleA("renderdoc.dll");
            auto get = module
                           ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
                           : nullptr;
            if (!get || !get(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void **>(&renderdoc)))
                throw std::runtime_error("Launch under RenderDoc to use --renderdoc-capture");
        }
#endif
        Window window(width, height, "Vulkan Rendering Lab");
        auto createContext = [&]() {
            auto result = std::make_unique<VulkanContext>(window, scenePath, stressCount, verifyCulling,
                                                          occlusionScene, mixedScene, verifyPrepass);
            result->SetOcclusion(occlusion);
            result->SetCulling(culling, verifyCulling);
            result->SetInspection(materialView, lightingView, environmentDegrees);
            result->ConfigureShadowDiagnostic(shadowMode, yaw, pitch, paused);
            result->ConfigureCullingCamera(cameraDistance, targetOffsetX);
            result->ConfigureAo(aoRadius, aoBias, aoSamples);
            result->ConfigurePrepass(prepassMode, depthRange);
            result->ConfigureHiZInspector(hiZInspector, inspectInstance, inspectMip, inspectGroup);
            if (cullingObserver)
                result->StartCullingObserver();
            return result;
        };
        auto context = createContext();
        if (!benchmarkPath.empty())
        {
            context->RunCullingBenchmark(benchmarkPath, benchmarkWarmup, benchmarkSamples, benchmarkRepeats,
                                         benchmarkCaptures);
            return EXIT_SUCCESS;
        }
        if (!capturePath.empty())
            context->CaptureNextFrame(capturePath);
        int frames = 0;

        double lastFrameTime = glfwGetTime();

        while (!window.ShouldClose())
        {
            const double currentFrameTime = glfwGetTime();

            const double deltaTime = currentFrameTime - lastFrameTime;

            lastFrameTime = currentFrameTime;

            window.PollEvents();

            int framebufferWidth = 0, framebufferHeight = 0;
            glfwGetFramebufferSize(window.GetHandle(), &framebufferWidth, &framebufferHeight);
            if (framebufferWidth == 0 || framebufferHeight == 0)
            {
                glfwWaitEventsTimeout(.05);
                continue;
            }
            const auto extent = context->Extent();
            if (uint32_t(framebufferWidth) != extent.width || uint32_t(framebufferHeight) != extent.height ||
                context->NeedsRebuild())
            {
                // Wait for a short quiet interval to avoid rebuilding on every mouse movement.
                double stable = glfwGetTime();
                int previousWidth = framebufferWidth, previousHeight = framebufferHeight;
                while (!window.ShouldClose() && glfwGetTime() - stable < .15)
                {
                    glfwWaitEventsTimeout(.025);
                    glfwGetFramebufferSize(window.GetHandle(), &framebufferWidth, &framebufferHeight);
                    if (framebufferWidth != previousWidth || framebufferHeight != previousHeight)
                    {
                        stable = glfwGetTime();
                        previousWidth = framebufferWidth;
                        previousHeight = framebufferHeight;
                    }
                }
                if (window.ShouldClose())
                    break;
                if (framebufferWidth == 0 || framebufferHeight == 0)
                    continue;
                auto restore = context->SaveRuntimeState();
                context->FinishDiagnostics();
                context.reset();
                context = createContext();
                restore(*context);
                if (resizeAfter && !capturePath.empty())
                    context->CaptureNextFrame(capturePath + ".resized.png");
                std::cout << "[Resize] rebuilt complete renderer at " << context->Extent().width << 'x'
                          << context->Extent().height << std::endl;
                lastFrameTime = glfwGetTime();
                continue;
            }
            if (aoCycleTest)
            {
                const int modes[] = {0, 7, 0, 5, 6, 7, 0, 7};
                const int mode = modes[frames % 8];
                context->ConfigurePrepass(mode, depthRange);
                if (!capturePath.empty())
                    context->CaptureNextFrame(capturePath + ".frame" + std::to_string(frames) + ".mode" +
                                              std::to_string(mode) + ".png");
            }
            if (cameraSweep != 0.0f)
                context->ConfigureShadowDiagnostic(shadowMode, yaw + float(frames) * cameraSweep, pitch,
                                                   paused);
#ifdef _WIN32
            if (renderdocCapture && frames == 0)
                renderdoc->TriggerCapture();
#endif
            context->UpdateUniformBuffer();
            context->DrawFrame();
            context->UpdateWindowTitle(deltaTime);
            ++frames;
            if (resizeAfter && frames == resizeAfter)
                glfwSetWindowSize(window.GetHandle(), resizeWidth, resizeHeight);
            if (frameLimit && frames >= frameLimit)
                break;
        }
        context->FinishDiagnostics();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Application error: " << e.what() << std::endl;

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}