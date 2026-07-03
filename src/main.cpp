#include "VulkanApp.h"

#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        VulkanApp app;
        app.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}