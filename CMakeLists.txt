cmake_minimum_required(VERSION 3.25)

project(VulkanRenderingLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(VulkanRenderingLab
    src/main.cpp

    src/Core/Window.h
    src/Core/Window.cpp

    src/Core/VulkanContext.h
    src/Core/VulkanContext.cpp

    src/Core/QueueFamily.h

    src/Core/PhysicalDevice.h
    src/Core/PhysicalDevice.cpp

    src/Core/LogicalDevice.h
    src/Core/LogicalDevice.cpp

    src/Core/SwapchainSupport.h
    src/Core/Swapchain.h
    src/Core/Swapchain.cpp

    src/Core/RenderPass.h
    src/Core/RenderPass.cpp

    src/Core/Framebuffer.h
    src/Core/Framebuffer.cpp

    src/Core/CommandManager.h
    src/Core/CommandManager.cpp

    src/Core/Renderer.h
    src/Core/Renderer.cpp
)

target_link_libraries(VulkanRenderingLab
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

target_include_directories(VulkanRenderingLab
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)