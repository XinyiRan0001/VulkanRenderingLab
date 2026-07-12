#include "Core/Window.h"
#include "Core/VulkanContext.h"
#include <iostream>
#include <stdexcept>

#include <GLFW/glfw3.h>

int main()
{
    try
    {
        Window window(
            1280,
            720,
            "Vulkan Rendering Lab"
        );

        VulkanContext context(window);

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
        }
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