#include "Core/Window.h"
#include "Core/VulkanContext.h"
#include <iostream>
#include <stdexcept>

#include <GLFW/glfw3.h>

int main(int argc, char** argv)
{
    try
    {
        std::string scenePath;
        std::string benchmarkPath;
        bool benchmarkCaptures=false;
        int benchmarkWarmup=30, benchmarkSamples=120, benchmarkRepeats=3;
        int frameLimit = 0;
        uint32_t stressCount = 0;
        std::string capturePath;
        int shadowMode = 3;
        float yaw = 0.0f, pitch = 8.594367f;
        bool paused = false;
        bool cullingObserver = false;
        bool hiZInspector=false;
        int inspectInstance=-1, inspectMip=0;
        bool culling = true, verifyCulling = false;
        bool occlusionScene=false,occlusion=true,pitchSpecified=false;
        float cameraDistance=0.0f, targetOffsetX=0.0f, cameraSweep=0.0f;
        int materialView = 0, lightingView = 0;
        float environmentDegrees = 0.0f;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--benchmark-culling" && i+1<argc) benchmarkPath=argv[++i];
            else if (option == "--benchmark-captures") benchmarkCaptures=true;
            else if (option == "--benchmark-warmup" && i+1<argc) benchmarkWarmup=std::stoi(argv[++i]);
            else if (option == "--benchmark-samples" && i+1<argc) benchmarkSamples=std::stoi(argv[++i]);
            else if (option == "--benchmark-repeats" && i+1<argc) benchmarkRepeats=std::stoi(argv[++i]);
            else if (option == "--scene" && i + 1 < argc) scenePath = argv[++i];
            else if (option == "--stress-count" && i + 1 < argc) {
                const int count = std::stoi(argv[++i]);
                if (count < 1 || count > 10000) throw std::runtime_error("--stress-count must be 1..10000");
                stressCount = static_cast<uint32_t>(count);
            }
            else if (option == "--capture" && i + 1 < argc) capturePath = argv[++i];
            else if (option == "--shadow-mode" && i + 1 < argc) shadowMode = std::stoi(argv[++i]);
            else if (option == "--yaw" && i + 1 < argc) yaw = std::stof(argv[++i]);
            else if (option == "--pitch" && i + 1 < argc) { pitch = std::stof(argv[++i]);pitchSpecified=true; }
            else if (option == "--camera-sweep" && i+1<argc) cameraSweep=std::stof(argv[++i]);
            else if (option == "--camera-distance" && i+1<argc) cameraDistance=std::stof(argv[++i]);
            else if (option == "--target-offset-x" && i+1<argc) targetOffsetX=std::stof(argv[++i]);
            else if (option == "--hiz-inspector") hiZInspector=true;
            else if (option == "--inspect-instance" && i+1<argc) { inspectInstance=std::stoi(argv[++i]);hiZInspector=true; }
            else if (option == "--hiz-mip" && i+1<argc) { inspectMip=std::stoi(argv[++i]);hiZInspector=true; }
            else if (option == "--occlusion-scene") occlusionScene=true;
            else if (option == "--culling-observer") cullingObserver = true;
            else if (option == "--no-occlusion") occlusion=false;
            else if (option == "--no-culling") culling = false;
            else if (option == "--verify-culling") verifyCulling = true;
            else if (option == "--paused") paused = true;
            else if (option == "--material-view" && i + 1 < argc) materialView = std::stoi(argv[++i]);
            else if (option == "--lighting-view" && i + 1 < argc) lightingView = std::stoi(argv[++i]);
            else if (option == "--environment-degrees" && i + 1 < argc) environmentDegrees = std::stof(argv[++i]);
            else if (option == "--frames" && i + 1 < argc) {
                frameLimit = std::stoi(argv[++i]);
                if (frameLimit <= 0) throw std::runtime_error("--frames must be positive");
            } else throw std::runtime_error("Usage: VulkanRenderingLab [--scene path.gltf|path.glb] [--frames count] [--stress-count 1..10000] [--benchmark-culling output.csv] [--benchmark-warmup 30] [--benchmark-samples 120] [--benchmark-repeats 3] [--benchmark-captures] [--occlusion-scene] [--hiz-inspector] [--hiz-mip -1..11] [--inspect-instance offset] [--culling-observer] [--no-occlusion] [--no-culling] [--verify-culling] [--camera-distance value] [--target-offset-x value] [--material-view 0..5] [--lighting-view 0..3] [--environment-degrees angle]");
        }
        if (!benchmarkPath.empty()) {
            if (!scenePath.empty() || cullingObserver || hiZInspector || verifyCulling || frameLimit || cameraSweep!=0 || !capturePath.empty())
                throw std::runtime_error("Culling benchmark cannot combine scene, observer, verification, frames, sweep or capture");
            if (benchmarkWarmup<1 || benchmarkSamples<2 || benchmarkRepeats<1 || benchmarkRepeats>20 || benchmarkWarmup>10000 || benchmarkSamples>10000)
                throw std::runtime_error("Invalid benchmark warmup (1..10000), samples (2..10000), repeats (1..20)");
            occlusionScene=true; paused=true;
        }
        if (cameraSweep!=0.0f && !frameLimit) throw std::runtime_error("--camera-sweep requires a bounded --frames run");
        if (occlusionScene && !stressCount) stressCount=1600;
        if (occlusionScene && !pitchSpecified) pitch=25.0f;
        Window window(1280, 720, "Vulkan Rendering Lab");
        VulkanContext context(window, scenePath, stressCount, verifyCulling, occlusionScene);
        context.SetOcclusion(occlusion);
        context.SetCulling(culling,verifyCulling);
        context.SetInspection(materialView, lightingView, environmentDegrees);
        context.ConfigureShadowDiagnostic(shadowMode, yaw, pitch, paused);
        context.ConfigureCullingCamera(cameraDistance,targetOffsetX);
        if (!benchmarkPath.empty()) { context.RunCullingBenchmark(benchmarkPath,benchmarkWarmup,benchmarkSamples,benchmarkRepeats,benchmarkCaptures); return EXIT_SUCCESS; }
        if (inspectInstance< -1) throw std::runtime_error("--inspect-instance must be a zero-based sphere offset");
        context.ConfigureHiZInspector(hiZInspector,inspectInstance,inspectMip);
        if (cullingObserver) context.StartCullingObserver();
        if (!capturePath.empty()) context.CaptureNextFrame(capturePath);
        int frames = 0;

        double lastFrameTime =
            glfwGetTime();

        while (!window.ShouldClose())
        {
            const double currentFrameTime =
                glfwGetTime();

            const double deltaTime =
                currentFrameTime -
                lastFrameTime;

            lastFrameTime =
                currentFrameTime;

            window.PollEvents();

            if(cameraSweep!=0.0f) context.ConfigureShadowDiagnostic(shadowMode,yaw+float(frames)*cameraSweep,pitch,paused);
            context.UpdateUniformBuffer();
            context.DrawFrame();
            context.UpdateWindowTitle(deltaTime);
            ++frames;
            if (frameLimit && frames >= frameLimit) break;
        }
        context.FinishDiagnostics();
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "Application error: "
            << e.what()
            << std::endl;

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}