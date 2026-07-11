cmake_minimum_required(VERSION 3.25)

project(VulkanRenderingLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

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

    src/Core/ShaderUtils.h
    src/Core/ShaderUtils.cpp
    src/Core/GraphicsPipeline.h
    src/Core/GraphicsPipeline.cpp

    src/Core/Vertex.h
    src/Core/Buffer.h
    src/Core/Buffer.cpp

    src/Core/Image.h
    src/Core/Image.cpp

    src/Core/UniformBufferObject.h

    src/Core/DescriptorManager.h
    src/Core/DescriptorManager.cpp

    src/Core/MeshData.h
    src/Core/OBJLoader.h
    src/Core/OBJLoader.cpp

    src/Core/TextureData.h
    src/Core/TextureData.cpp

    src/Core/TextureImage.h
    src/Core/TextureImage.cpp
)

target_link_libraries(VulkanRenderingLab
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
        tinyobjloader::tinyobjloader
)

target_include_directories(VulkanRenderingLab
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${STB_INCLUDE_DIRS}
)