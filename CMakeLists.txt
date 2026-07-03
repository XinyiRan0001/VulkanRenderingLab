cmake_minimum_required(VERSION 3.25)

project(VulkanRenderingLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(VulkanRenderingLab
    src/main.cpp
    src/VulkanApp.h
    src/VulkanApp.cpp
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