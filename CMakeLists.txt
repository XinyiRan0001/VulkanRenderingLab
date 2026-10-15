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
    src/Core/GLTFLoader.cpp
    src/Core/InstancedScene.cpp
    src/Core/GpuCulling.cpp

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

    src/Core/ShadowRenderPass.h
    src/Core/ShadowRenderPass.cpp

    src/Core/ShadowFramebuffer.h
    src/Core/ShadowFramebuffer.cpp
    src/Core/ShadowPipeline.h
    src/Core/ShadowPipeline.cpp

    src/Core/DescriptorSetLayout.cpp
    src/Core/DescriptorSetLayout.h
    src/Core/SkyboxPipeline.cpp
    src/Core/SkyboxPipeline.h

    src/Core/EnvironmentRenderPass.cpp
    src/Core/EnvironmentFramebuffer.cpp
    src/Core/HDRToCubemapPipeline.cpp
    src/Core/HDRToCubemapDescriptorManager.cpp

    src/Core/PrefilterFramebuffer.h
    src/Core/PrefilterFramebuffer.cpp
    src/Core/PrefilterPipeline.h
    src/Core/PrefilterPipeline.cpp
    src/Core/PrefilterDescriptorManager.cpp
    src/Core/PrefilterPipeline.cpp

    src/Core/BRDFLUTRenderPass.cpp
    src/Core/BRDFLUTFramebuffer.cpp
    src/Core/BRDFLUTPipeline.cpp


    src/Core/IrradianceDescriptorManager.cpp
    src/Core/IrradianceFramebuffer.cpp
    src/Core/IrradiancePipeline.cpp
    src/Core/IrradianceRenderPass.cpp
)

target_link_libraries(VulkanRenderingLab
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
        tinyobjloader::tinyobjloader
)

target_compile_definitions(VulkanRenderingLab
    PRIVATE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_include_directories(VulkanRenderingLab
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${STB_INCLUDE_DIRS}
)
# Pin cgltf v1.15 in-tree; shaders are rebuilt whenever their source changes.
target_include_directories(VulkanRenderingLab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf)
if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc is required to compile renderer shaders")
endif()
file(GLOB RENDERER_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp")
set(SHADER_OUTPUTS)
foreach(SHADER IN LISTS RENDERER_SHADERS)
    get_filename_component(SHADER_NAME "${SHADER}" NAME)
    set(SPIRV "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(OUTPUT "${SPIRV}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${SHADER}" -o "${SPIRV}"
        DEPENDS "${SHADER}" VERBATIM)
    list(APPEND SHADER_OUTPUTS "${SPIRV}")
endforeach()
add_custom_target(RendererShaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(VulkanRenderingLab RendererShaders)

if(MSVC)
    target_compile_options(VulkanRenderingLab PRIVATE /utf-8)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(GLTFLoaderTests tests/GLTFLoaderTests.cpp src/Core/GLTFLoader.cpp src/Core/TextureData.cpp)
    target_include_directories(GLTFLoaderTests PRIVATE src/Core third_party/cgltf ${STB_INCLUDE_DIRS})
    target_link_libraries(GLTFLoaderTests PRIVATE Vulkan::Vulkan glfw glm::glm)
    add_test(NAME GLTFLoaderTests COMMAND GLTFLoaderTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/gltf-validation")
endif()

if(BUILD_TESTING)
    add_executable(InstancedSceneTests tests/InstancedSceneTests.cpp src/Core/InstancedScene.cpp)
    target_include_directories(InstancedSceneTests PRIVATE src)
    target_link_libraries(InstancedSceneTests PRIVATE Vulkan::Vulkan glfw glm::glm)
    add_test(NAME InstancedSceneTests COMMAND InstancedSceneTests)
endif()

if(BUILD_TESTING)
    add_executable(FrustumTests tests/FrustumTests.cpp)
    target_include_directories(FrustumTests PRIVATE src)
    target_link_libraries(FrustumTests PRIVATE glm::glm)
    add_test(NAME FrustumTests COMMAND FrustumTests)
endif()
