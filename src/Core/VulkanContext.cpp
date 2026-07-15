#include "VulkanContext.h"

#include "Window.h"
#include "PhysicalDevice.h"
#include "LogicalDevice.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "CommandManager.h"
#include "Renderer.h"
#include "GraphicsPipeline.h"
#include "Buffer.h"
#include "Vertex.h"
#include "Image.h"
#include "UniformBufferObject.h"
#include "DescriptorManager.h"
#include "OBJLoader.h"
#include "MeshData.h"
#include "TextureData.h"
#include "TextureImage.h"
#include "ShadowRenderPass.h"
#include "ShadowFramebuffer.h"
#include "ShadowPipeline.h"

#include <array>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

VulkanContext::VulkanContext(Window& window)
    : m_Window(window)
{
    // =========================================================
    // Vulkan base objects
    // =========================================================

    CreateInstance();
    CreateSurface();

    m_PhysicalDevice = std::make_unique<PhysicalDevice>(
        m_Instance,
        m_Surface
    );

    m_LogicalDevice = std::make_unique<LogicalDevice>(
        *m_PhysicalDevice
    );

    m_Swapchain = std::make_unique<Swapchain>(
        m_Window,
        *m_PhysicalDevice,
        *m_LogicalDevice,
        m_Surface
    );

    m_RenderPass = std::make_unique<RenderPass>(
        *m_LogicalDevice,
        *m_Swapchain
    );

    // =========================================================
    // Main depth buffer
    // =========================================================

    m_DepthImage = std::make_unique<Image>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        m_Swapchain->GetExtent().width,
        m_Swapchain->GetExtent().height,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    // =========================================================
// Shadow map depth image
// =========================================================

    constexpr uint32_t SHADOW_MAP_WIDTH = 2048;
    constexpr uint32_t SHADOW_MAP_HEIGHT = 2048;

    std::cout
        << "[Shadow] Creating shadow depth image..."
        << std::endl;

    m_ShadowDepthImage = std::make_unique<Image>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        SHADOW_MAP_WIDTH,
        SHADOW_MAP_HEIGHT,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    std::cout
        << "[Shadow] Shadow depth image created successfully."
        << std::endl;

    std::cout
        << "[Shadow] Creating shadow render pass..."
        << std::endl;

    m_ShadowRenderPass =
        std::make_unique<ShadowRenderPass>(
            *m_LogicalDevice
        );

    std::cout
        << "[Shadow] Shadow render pass ready."
        << std::endl;

    std::cout
        << "[Shadow] Creating shadow framebuffer..."
        << std::endl;

    m_ShadowFramebuffer =
        std::make_unique<ShadowFramebuffer>(
            *m_LogicalDevice,
            *m_ShadowRenderPass,
            m_ShadowDepthImage->GetView(),
            SHADOW_MAP_WIDTH,
            SHADOW_MAP_HEIGHT
        );

    std::cout
        << "[Shadow] Shadow framebuffer ready."
        << std::endl;

    std::cout
    << "[Shadow] Creating shadow pipeline..."
    << std::endl;

    

    CreateShadowSampler();

    // =========================================================
    // Swapchain framebuffers
    // =========================================================

    m_Framebuffer = std::make_unique<Framebuffer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_RenderPass,
        m_DepthImage->GetView()
    );

    // =========================================================
    // Main graphics pipeline
    // =========================================================

    m_GraphicsPipeline = std::make_unique<GraphicsPipeline>(
        *m_LogicalDevice,
        *m_RenderPass,
        *m_Swapchain
    );

    m_ShadowPipeline =
            std::make_unique<ShadowPipeline>(
                *m_LogicalDevice,
                *m_ShadowRenderPass,
                m_GraphicsPipeline->GetDescriptorSetLayout()
            );

        std::cout
        << "[Shadow] Shadow pipeline ready."
        << std::endl;
    // =========================================================
    // Load Marry model
    // =========================================================

    MeshData maryMesh = OBJLoader::Load(
        "assets/models/mary/Marry.obj"
    );

    // =========================================================
    // Load Floor model
    // =========================================================

    MeshData floorMesh = OBJLoader::Load(
        "assets/models/floor/floor.obj"
    );

    // Temporarily bake a transform into the floor vertices.
    // These values can be adjusted after seeing the result.
    for (Vertex& vertex : floorMesh.vertices)
    {
        vertex.position[0] *= 4.0f;
        vertex.position[1] *= 4.0f;
        vertex.position[2] *= 4.0f;

        // Move the floor slightly behind/below the character.
        vertex.position[2] -= 1.5f;
    }

    // The current renderer only has one texture descriptor.
    // Do not let the floor accidentally sample Marry's texture.
    for (MaterialData& material : floorMesh.materials)
    {
        material.hasDiffuseTexture = false;
    }

    // =========================================================
    // Combine Marry and Floor into one MeshData
    // =========================================================

    MeshData combinedMesh;

    // ---------------------------------------------------------
    // Copy Marry vertices and indices
    // ---------------------------------------------------------

    combinedMesh.vertices.insert(
        combinedMesh.vertices.end(),
        maryMesh.vertices.begin(),
        maryMesh.vertices.end()
    );

    combinedMesh.indices.insert(
        combinedMesh.indices.end(),
        maryMesh.indices.begin(),
        maryMesh.indices.end()
    );

    combinedMesh.materials = maryMesh.materials;

    for (const SubMesh& sourceSubMesh : maryMesh.subMeshes)
    {
        combinedMesh.subMeshes.push_back(sourceSubMesh);
    }

    // ---------------------------------------------------------
    // Record offsets before appending Floor
    // ---------------------------------------------------------

    const uint32_t floorVertexOffset =
        static_cast<uint32_t>(
            combinedMesh.vertices.size()
            );

    const uint32_t floorIndexOffset =
        static_cast<uint32_t>(
            combinedMesh.indices.size()
            );

    const int floorMaterialOffset =
        static_cast<int>(
            combinedMesh.materials.size()
            );

    // ---------------------------------------------------------
    // Append Floor vertices
    // ---------------------------------------------------------

    combinedMesh.vertices.insert(
        combinedMesh.vertices.end(),
        floorMesh.vertices.begin(),
        floorMesh.vertices.end()
    );

    // ---------------------------------------------------------
    // Append Floor indices
    //
    // Floor indices originally begin at vertex 0.
    // They must be shifted because Marry's vertices come first.
    // ---------------------------------------------------------

    for (uint32_t index : floorMesh.indices)
    {
        combinedMesh.indices.push_back(
            index + floorVertexOffset
        );
    }

    // ---------------------------------------------------------
    // Append Floor materials
    // ---------------------------------------------------------

    combinedMesh.materials.insert(
        combinedMesh.materials.end(),
        floorMesh.materials.begin(),
        floorMesh.materials.end()
    );

    // ---------------------------------------------------------
    // Append Floor SubMeshes
    // ---------------------------------------------------------

    for (const SubMesh& sourceSubMesh : floorMesh.subMeshes)
    {
        SubMesh floorSubMesh = sourceSubMesh;

        // Floor indices begin after all Marry indices.
        floorSubMesh.firstIndex += floorIndexOffset;

        // Floor material IDs begin after all Marry materials.
        if (floorSubMesh.materialId >= 0)
        {
            floorSubMesh.materialId += floorMaterialOffset;
        }

        combinedMesh.subMeshes.push_back(
            floorSubMesh
        );
    }

    std::cout
        << "[Scene] Combined vertices: "
        << combinedMesh.vertices.size()
        << std::endl;

    std::cout
        << "[Scene] Combined indices: "
        << combinedMesh.indices.size()
        << std::endl;

    std::cout
        << "[Scene] Combined submeshes: "
        << combinedMesh.subMeshes.size()
        << std::endl;

    std::cout
        << "[Scene] Combined materials: "
        << combinedMesh.materials.size()
        << std::endl;

    // =========================================================
    // Load Marry texture
    // =========================================================

    TextureData textureData = TextureLoader::LoadRGBA(
        "assets/models/mary/MC003_Kozakura_Mari.png"
    );

    m_TextureImage = std::make_unique<TextureImage>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        textureData
    );

    // =========================================================
    // Combined vertex buffer
    // =========================================================

    const VkDeviceSize vertexBufferSize =
        sizeof(Vertex) *
        combinedMesh.vertices.size();

    std::cout
        << "[Context] Creating vertex buffer..."
        << std::endl;

    m_VertexBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_VertexBuffer->UploadData(
        combinedMesh.vertices.data(),
        vertexBufferSize
    );

    std::cout
        << "[Context] Vertex buffer uploaded."
        << std::endl;

    // =========================================================
    // Combined index buffer
    // =========================================================

    const VkDeviceSize indexBufferSize =
        sizeof(uint32_t) *
        combinedMesh.indices.size();

    std::cout
        << "[Context] Creating index buffer..."
        << std::endl;

    m_IndexBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_IndexBuffer->UploadData(
        combinedMesh.indices.data(),
        indexBufferSize
    );

    std::cout
        << "[Context] Index buffer uploaded."
        << std::endl;

    // =========================================================
    // Initial camera/MVP uniform data
    // =========================================================

    UniformBufferObject ubo{};

    ubo.model = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(0.35f)
    );

    ubo.view = glm::lookAt(
        glm::vec3(0.0f, 0.8f, 3.5f),
        glm::vec3(0.0f, 0.8f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    const float aspect =
        m_Swapchain->GetExtent().width /
        static_cast<float>(
            m_Swapchain->GetExtent().height
            );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        aspect,
        0.1f,
        100.0f
    );

    ubo.proj[1][1] *= -1.0f;

    // =========================================================
    // Uniform buffer
    // =========================================================

    std::cout
        << "[Context] Creating uniform buffer..."
        << std::endl;

    m_UniformBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        sizeof(UniformBufferObject),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_UniformBuffer->UploadData(
        &ubo,
        sizeof(ubo)
    );

    std::cout
        << "[Context] Uniform buffer uploaded."
        << std::endl;

    // =========================================================
    // Descriptor set
    // =========================================================

    std::cout
        << "[Context] Creating descriptor manager..."
        << std::endl;

    m_DescriptorManager =
        std::make_unique<DescriptorManager>(
            *m_LogicalDevice,
            *m_GraphicsPipeline,
            *m_UniformBuffer,
            *m_TextureImage,
            m_ShadowDepthImage->GetView(),
            m_ShadowSampler
        );

    std::cout
        << "[Context] Descriptor manager ready."
        << std::endl;

    // =========================================================
    // Command buffers
    // =========================================================

    std::cout
        << "[Context] Creating command manager..."
        << std::endl;

    m_CommandManager =
        std::make_unique<CommandManager>(
            *m_LogicalDevice,
            *m_PhysicalDevice,
            *m_Framebuffer,
            *m_RenderPass,
            *m_Swapchain,
            *m_GraphicsPipeline,
            *m_ShadowRenderPass,
            *m_ShadowFramebuffer,
            *m_ShadowPipeline,
            *m_VertexBuffer,
            *m_IndexBuffer,
            *m_DescriptorManager,
            combinedMesh.subMeshes,
            combinedMesh.materials
        );

    std::cout
        << "[Context] Command manager ready."
        << std::endl;

    // =========================================================
    // Renderer
    // =========================================================

    std::cout
        << "[Context] Creating renderer..."
        << std::endl;

    m_Renderer = std::make_unique<Renderer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_CommandManager
    );

    std::cout
        << "[Context] Renderer ready."
        << std::endl;
    SetupCameraControls();
    std::cout
        << "VulkanContext created successfully."
        << std::endl;
}

void VulkanContext::SetupCameraControls()
{
    GLFWwindow* glfwWindow = m_Window.GetHandle();

    glfwSetWindowUserPointer(
        glfwWindow,
        this
    );

    glfwSetMouseButtonCallback(
        glfwWindow,
        [](GLFWwindow* window, int button, int action, int mods)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr)
            {
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_LEFT)
            {
                if (action == GLFW_PRESS)
                {
                    context->m_IsDragging = true;

                    glfwGetCursorPos(
                        window,
                        &context->m_LastMouseX,
                        &context->m_LastMouseY
                    );
                }
                else if (action == GLFW_RELEASE)
                {
                    context->m_IsDragging = false;
                }
            }
        }
    );

    glfwSetCursorPosCallback(
        glfwWindow,
        [](GLFWwindow* window, double mouseX, double mouseY)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr ||
                !context->m_IsDragging)
            {
                return;
            }

            const double deltaX =
                mouseX - context->m_LastMouseX;

            const double deltaY =
                mouseY - context->m_LastMouseY;

            context->m_LastMouseX = mouseX;
            context->m_LastMouseY = mouseY;

            constexpr float rotationSpeed = 0.005f;

            context->m_CameraYaw +=
                static_cast<float>(deltaX) *
                rotationSpeed;

            context->m_CameraPitch -=
                static_cast<float>(deltaY) *
                rotationSpeed;

            context->m_CameraPitch =
                std::clamp(
                    context->m_CameraPitch,
                    -1.3f,
                    1.3f
                );
        }
    );

    glfwSetScrollCallback(
        glfwWindow,
        [](GLFWwindow* window, double xOffset, double yOffset)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr)
            {
                return;
            }

            context->m_CameraDistance -=
                static_cast<float>(yOffset) *
                0.25f;

            context->m_CameraDistance =
                std::clamp(
                    context->m_CameraDistance,
                    1.5f,
                    10.0f
                );
        }
    );
    glfwSetKeyCallback(
        glfwWindow,
        [](GLFWwindow* window,
            int key,
            int scancode,
            int action,
            int mods)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr)
            {
                return;
            }

            if (action != GLFW_PRESS &&
                action != GLFW_REPEAT)
            {
                return;
            }

            constexpr float moveStep = 0.15f;

            if (context->m_ManualPointLightControl)
            {
                if (key == GLFW_KEY_J)
                {
                    context->m_ManualPointLightPosition.x -= moveStep;
                }
                else if (key == GLFW_KEY_L)
                {
                    context->m_ManualPointLightPosition.x += moveStep;
                }
                else if (key == GLFW_KEY_I)
                {
                    context->m_ManualPointLightPosition.z -= moveStep;
                }
                else if (key == GLFW_KEY_K)
                {
                    context->m_ManualPointLightPosition.z += moveStep;
                }
                else if (key == GLFW_KEY_U)
                {
                    context->m_ManualPointLightPosition.y += moveStep;
                }
                else if (key == GLFW_KEY_O)
                {
                    context->m_ManualPointLightPosition.y -= moveStep;
                }
            }

            if (key == GLFW_KEY_1)
            {
                context->m_ShadowMode = 1;

                std::cout
                    << "[Shadow] Mode: Hard Shadow"
                    << std::endl;
            }
            else if (key == GLFW_KEY_2)
            {
                context->m_ShadowMode = 2;

                std::cout
                    << "[Shadow] Mode: PCF"
                    << std::endl;
            }
            else if (key == GLFW_KEY_3)
            {
                context->m_ShadowMode = 3;

                std::cout
                    << "[Shadow] Mode: PCSS"
                    << std::endl;
            }
            else if (key == GLFW_KEY_4)
            {
                context->m_ShadowMode = 4;

                std::cout
                    << "[Shadow] Mode: Shadow Map Debug"
                    << std::endl;
            }

            else if (key == GLFW_KEY_R)
            {
                context->m_CameraYaw = 0.0f;
                context->m_CameraPitch = 0.15f;
                context->m_CameraDistance = 3.5f;

                std::cout
                    << "[Camera] Reset"
                    << std::endl;
            }
            else if (key == GLFW_KEY_ESCAPE)
            {
                glfwSetWindowShouldClose(
                    window,
                    GLFW_TRUE
                );
            }
            else if (key == GLFW_KEY_P)
            {
                context->m_PointLightEnabled =
                    !context->m_PointLightEnabled;

                std::cout
                    << "[Point Light] "
                    << (
                        context->m_PointLightEnabled
                        ? "Enabled"
                        : "Disabled"
                        )
                    << std::endl;
            }
            else if (key == GLFW_KEY_SPACE)
            {
                context->m_PointLightAnimationEnabled =
                    !context->m_PointLightAnimationEnabled;

                std::cout
                    << "[Point Light Animation] "
                    << (
                        context->m_PointLightAnimationEnabled
                        ? "Running"
                        : "Paused"
                        )
                    << std::endl;
            }
            else if (key == GLFW_KEY_M)
            {
                context->m_ManualPointLightControl =
                    !context->m_ManualPointLightControl;

                std::cout
                    << "[Point Light] Control Mode: "
                    << (
                        context->m_ManualPointLightControl
                        ? "Manual"
                        : "Orbit"
                        )
                    << std::endl;
            }
            else if (
                key == GLFW_KEY_B &&
                action == GLFW_PRESS
                )
            {
                std::cout
                    << "[Input] B pressed."
                    << std::endl;

                if (context->m_Renderer != nullptr)
                {
                    context->m_Renderer->StartBenchmark(
                        context->m_ShadowMode
                    );
                }
            }
        }
    );
}

void VulkanContext::UpdateWindowTitle(double deltaTime)
{
    m_FrameTimeAccumulator += deltaTime;
    m_FrameCount++;

    const double currentTime = glfwGetTime();

    if (currentTime - m_LastTitleUpdateTime < 0.5)
    {
        return;
    }

    const double averageFrameTime =
        m_FrameTimeAccumulator /
        static_cast<double>(m_FrameCount);

    const double fps =
        averageFrameTime > 0.0
        ? 1.0 / averageFrameTime
        : 0.0;

    const double frameTimeMs =
        averageFrameTime * 1000.0;

    const char* shadowModeName = "Unknown";

    switch (m_ShadowMode)
    {
    case 1:
        shadowModeName = "Hard Shadow";
        break;

    case 2:
        shadowModeName = "PCF";
        break;

    case 3:
        shadowModeName = "PCSS";
        break;

    case 4:
        shadowModeName = "Shadow Map Debug";
        break;
    }

    const char* pointLightMode =
        m_ManualPointLightControl
        ? "Manual Light"
        : "Orbit Light";

    std::ostringstream title;

    title
        << "Vulkan Rendering Lab"
        << " | "
        << shadowModeName
        << " | "
        << pointLightMode
        << " | "
        << std::fixed
        << std::setprecision(1)
        << fps
        << " FPS"
        << " | "
        << std::setprecision(2)
        << frameTimeMs
        << " ms";

    glfwSetWindowTitle(
        m_Window.GetHandle(),
        title.str().c_str()
    );

    m_LastTitleUpdateTime = currentTime;
    m_FrameTimeAccumulator = 0.0;
    m_FrameCount = 0;
}

VulkanContext::~VulkanContext()
{
    m_Renderer.reset();
    m_CommandManager.reset();
    m_TextureImage.reset();
    m_IndexBuffer.reset();
    m_VertexBuffer.reset();
    m_GraphicsPipeline.reset();
    m_Framebuffer.reset();
    m_DepthImage.reset();
    m_RenderPass.reset();
    m_Swapchain.reset();
    m_ShadowPipeline.reset();
    m_ShadowFramebuffer.reset();
    m_ShadowRenderPass.reset();
    m_ShadowDepthImage.reset();

    if (m_ShadowSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(
            m_LogicalDevice->GetHandle(),
            m_ShadowSampler,
            nullptr
        );

        m_ShadowSampler = VK_NULL_HANDLE;
    }

    m_LogicalDevice.reset();
    m_PhysicalDevice.reset();

    if (m_Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::CreateSurface()
{
    VkResult result = glfwCreateWindowSurface(
        m_Instance,
        m_Window.GetHandle(),
        nullptr,
        &m_Surface
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface.");
    }

    std::cout << "Window surface created successfully." << std::endl;
}

void VulkanContext::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Rendering Lab";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Mini Vulkan Renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

    if (!extensions || extensionCount == 0)
    {
        throw std::runtime_error("Failed to get required GLFW Vulkan extensions.");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
}

void VulkanContext::CreateShadowSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType =
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerInfo.addressModeV =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerInfo.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    samplerInfo.borderColor =
        VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // We will manually compare depths in the shader first.
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    samplerInfo.mipmapMode =
        VK_SAMPLER_MIPMAP_MODE_LINEAR;

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(
        m_LogicalDevice->GetHandle(),
        &samplerInfo,
        nullptr,
        &m_ShadowSampler
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shadow map sampler."
        );
    }

    std::cout
        << "[Shadow] Shadow sampler created successfully."
        << std::endl;
}

void VulkanContext::UpdateUniformBuffer()
{
    UniformBufferObject ubo{};

    // =========================================================
    // Model
    // =========================================================

    ubo.model = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(0.35f)
    );

    // =========================================================
    // Orbit camera
    // =========================================================

    const glm::vec3 cameraTarget(
        0.0f,
        0.8f,
        0.0f
    );

    const float cosPitch =
        std::cos(m_CameraPitch);

    glm::vec3 cameraPosition{};

    cameraPosition.x =
        cameraTarget.x +
        m_CameraDistance *
        cosPitch *
        std::sin(m_CameraYaw);

    cameraPosition.y =
        cameraTarget.y +
        m_CameraDistance *
        std::sin(m_CameraPitch);

    cameraPosition.z =
        cameraTarget.z +
        m_CameraDistance *
        cosPitch *
        std::cos(m_CameraYaw);

    ubo.view = glm::lookAt(
        cameraPosition,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    // =========================================================
    // Main camera projection
    // =========================================================

    const float aspect =
        m_Swapchain->GetExtent().width /
        static_cast<float>(
            m_Swapchain->GetExtent().height
            );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        aspect,
        0.1f,
        100.0f
    );

    ubo.proj[1][1] *= -1.0f;

    // =========================================================
    // Shadow mode
    // =========================================================

    ubo.shadowSettings = glm::ivec4(
        m_ShadowMode,
        0,
        0,
        0
    );

    // =========================================================
    // Orbiting / manually controlled light
    // =========================================================

    const double currentTime =
        glfwGetTime();

    if (m_LastLightUpdateTime == 0.0)
    {
        m_LastLightUpdateTime =
            currentTime;
    }

    const float deltaTime =
        static_cast<float>(
            currentTime -
            m_LastLightUpdateTime
            );

    m_LastLightUpdateTime =
        currentTime;

    if (m_PointLightAnimationEnabled &&
        !m_ManualPointLightControl)
    {
        constexpr float lightSpeed = 0.8f;

        m_PointLightAngle +=
            deltaTime * lightSpeed;
    }

    constexpr float lightRadius = 2.8f;
    constexpr float lightHeight = 2.5f;

    glm::vec3 pointLightPosition{};

    if (m_ManualPointLightControl)
    {
        pointLightPosition =
            m_ManualPointLightPosition;
    }
    else
    {
        pointLightPosition =
            glm::vec3(
                std::cos(m_PointLightAngle) *
                lightRadius,

                lightHeight,

                std::sin(m_PointLightAngle) *
                lightRadius
            );

        m_ManualPointLightPosition =
            pointLightPosition;
    }

    ubo.pointLight.position =
        glm::vec4(
            pointLightPosition,
            1.0f
        );

    ubo.pointLight.color =
        glm::vec4(
            1.0f,
            0.35f,
            0.15f,
            m_PointLightEnabled
            ? 2.5f
            : 0.0f
        );

    // =========================================================
    // Shadow camera follows the same moving light
    // =========================================================

    const glm::vec3 shadowTarget(
        0.0f,
        0.8f,
        0.0f
    );

    const glm::vec3 fixedLightPosition(
        0.0f,
        2.2f,
        3.5f
    );

    glm::vec3 shadowLightPosition =
        m_PointLightEnabled
        ? pointLightPosition
        : fixedLightPosition;

    glm::mat4 lightView =
        glm::lookAt(
            shadowLightPosition,
            shadowTarget,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

    glm::mat4 lightProjection =
        glm::perspective(
            glm::radians(70.0f),
            1.0f,
            0.1f,
            12.0f
        );

    lightProjection[1][1] *= -1.0f;

    ubo.lightViewProj =
        lightProjection *
        lightView;

    // =========================================================
    // Upload
    // =========================================================

    m_UniformBuffer->UploadData(
        &ubo,
        sizeof(ubo)
    );
}

void VulkanContext::DrawFrame()
{
    m_Renderer->DrawFrame();
}