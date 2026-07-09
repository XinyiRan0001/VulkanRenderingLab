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

#include <array>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

VulkanContext::VulkanContext(Window& window)
    : m_Window(window)
{
    CreateInstance();
    CreateSurface();

    m_PhysicalDevice = std::make_unique<PhysicalDevice>(m_Instance, m_Surface);
    m_LogicalDevice = std::make_unique<LogicalDevice>(*m_PhysicalDevice);
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

    m_Framebuffer = std::make_unique<Framebuffer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_RenderPass,
        m_DepthImage->GetView()
    );

    m_GraphicsPipeline = std::make_unique<GraphicsPipeline>(
        *m_LogicalDevice,
        *m_RenderPass,
        *m_Swapchain
    );

    std::array<Vertex, 8> vertices =
    {
        Vertex{{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        Vertex{{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
        Vertex{{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
        Vertex{{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},

        Vertex{{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 1.0f}},
        Vertex{{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 1.0f}},
        Vertex{{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}},
        Vertex{{-0.5f,  0.5f,  0.5f}, {0.2f, 0.5f, 1.0f}}
    };

    VkDeviceSize bufferSize = sizeof(vertices);

    m_VertexBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_VertexBuffer->UploadData(vertices.data(), bufferSize);

    std::array<uint32_t, 36> indices =
    {
        // Front
        4, 5, 6,
        6, 7, 4,

        // Back
        1, 0, 3,
        3, 2, 1,

        // Left
        0, 4, 7,
        7, 3, 0,

        // Right
        5, 1, 2,
        2, 6, 5,

        // Top
        3, 7, 6,
        6, 2, 3,

        // Bottom
        0, 1, 5,
        5, 4, 0
    };

    VkDeviceSize indexBufferSize = sizeof(indices);

    m_IndexBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_IndexBuffer->UploadData(indices.data(), indexBufferSize);

    UniformBufferObject ubo{};

    ubo.model = glm::mat4(1.0f);

    ubo.view = glm::lookAt(
        glm::vec3(1.5f, 1.5f, 2.5f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        m_Swapchain->GetExtent().width /
        static_cast<float>(m_Swapchain->GetExtent().height),
        0.1f,
        10.0f
    );

    ubo.proj[1][1] *= -1.0f;

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

    m_DescriptorManager = std::make_unique<DescriptorManager>(
        *m_LogicalDevice,
        *m_GraphicsPipeline,
        *m_UniformBuffer
    );

    m_CommandManager = std::make_unique<CommandManager>(
        *m_LogicalDevice,
        *m_PhysicalDevice,
        *m_Framebuffer,
        *m_RenderPass,
        *m_Swapchain,
        *m_GraphicsPipeline,
        *m_VertexBuffer,
        *m_IndexBuffer,
        *m_DescriptorManager
    );

    m_Renderer = std::make_unique<Renderer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_CommandManager
    );

    std::cout << "VulkanContext created successfully." << std::endl;
}

VulkanContext::~VulkanContext()
{
    m_Renderer.reset();
    m_CommandManager.reset();
    m_IndexBuffer.reset();
    m_VertexBuffer.reset();
    m_GraphicsPipeline.reset();
    m_Framebuffer.reset();
    m_DepthImage.reset();
    m_RenderPass.reset();
    m_Swapchain.reset();
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

void VulkanContext::UpdateUniformBuffer()
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();

    float time = std::chrono::duration<float, std::chrono::seconds::period>(
        currentTime - startTime
    ).count();

    UniformBufferObject ubo{};

    ubo.model = glm::rotate(
        glm::mat4(1.0f),
        time * glm::radians(45.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    ubo.view = glm::lookAt(
        glm::vec3(1.5f, 1.5f, 2.5f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        m_Swapchain->GetExtent().width /
        static_cast<float>(m_Swapchain->GetExtent().height),
        0.1f,
        10.0f
    );

    ubo.proj[1][1] *= -1.0f;

    m_UniformBuffer->UploadData(&ubo, sizeof(ubo));
}

void VulkanContext::DrawFrame()
{
    m_Renderer->DrawFrame();
}