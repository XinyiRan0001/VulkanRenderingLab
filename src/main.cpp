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
        int frameLimit = 0;
        uint32_t stressCount = 0;
        std::string capturePath;
        int shadowMode = 3;
        float yaw = 0.0f, pitch = 8.594367f;
        bool paused = false;
        bool culling = true, verifyCulling = false;
        float cameraDistance=0.0f, targetOffsetX=0.0f;
        int materialView = 0, lightingView = 0;
        float environmentDegrees = 0.0f;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--scene" && i + 1 < argc) scenePath = argv[++i];
            else if (option == "--stress-count" && i + 1 < argc) {
                const int count = std::stoi(argv[++i]);
                if (count < 1 || count > 10000) throw std::runtime_error("--stress-count must be 1..10000");
                stressCount = static_cast<uint32_t>(count);
            }
            else if (option == "--capture" && i + 1 < argc) capturePath = argv[++i];
            else if (option == "--shadow-mode" && i + 1 < argc) shadowMode = std::stoi(argv[++i]);
            else if (option == "--yaw" && i + 1 < argc) yaw = std::stof(argv[++i]);
            else if (option == "--pitch" && i + 1 < argc) pitch = std::stof(argv[++i]);
            else if (option == "--camera-distance" && i+1<argc) cameraDistance=std::stof(argv[++i]);
            else if (option == "--target-offset-x" && i+1<argc) targetOffsetX=std::stof(argv[++i]);
            else if (option == "--no-culling") culling = false;
            else if (option == "--verify-culling") verifyCulling = true;
            else if (option == "--paused") paused = true;
            else if (option == "--material-view" && i + 1 < argc) materialView = std::stoi(argv[++i]);
            else if (option == "--lighting-view" && i + 1 < argc) lightingView = std::stoi(argv[++i]);
            else if (option == "--environment-degrees" && i + 1 < argc) environmentDegrees = std::stof(argv[++i]);
            else if (option == "--frames" && i + 1 < argc) {
                frameLimit = std::stoi(argv[++i]);
                if (frameLimit <= 0) throw std::runtime_error("--frames must be positive");
            } else throw std::runtime_error("Usage: VulkanRenderingLab [--scene path.gltf|path.glb] [--frames count] [--stress-count 1..10000] [--no-culling] [--verify-culling] [--camera-distance value] [--target-offset-x value] [--material-view 0..5] [--lighting-view 0..3] [--environment-degrees angle]");
        }
        Window window(1280, 720, "Vulkan Rendering Lab");
        VulkanContext context(window, scenePath, stressCount, verifyCulling);
        context.SetCulling(culling,verifyCulling);
        context.SetInspection(materialView, lightingView, environmentDegrees);
        context.ConfigureShadowDiagnostic(shadowMode, yaw, pitch, paused);
        context.ConfigureCullingCamera(cameraDistance,targetOffsetX);
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

            context.UpdateUniformBuffer();
            context.DrawFrame();
            context.UpdateWindowTitle(deltaTime);
            if (frameLimit && ++frames >= frameLimit) break;
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