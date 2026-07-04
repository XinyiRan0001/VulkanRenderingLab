#include "Core/Window.h"
#include "Core/VulkanContext.h"

#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        Window window(1280, 720, "Vulkan Rendering Lab");
        VulkanContext context(window);

        while (!window.ShouldClose())
        {
            window.PollEvents();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}