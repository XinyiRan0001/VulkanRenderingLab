#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "EnvironmentRenderPass.h"
#include "TextureImage.h"

#include <array>
#include <cstdint>

class EnvironmentFramebuffer
{
public:
    static constexpr uint32_t FaceCount = 6;

    EnvironmentFramebuffer(
        const LogicalDevice& logicalDevice,
        const EnvironmentRenderPass& renderPass,
        const TextureImage& cubemapImage,
        uint32_t width,
        uint32_t height
    );

    ~EnvironmentFramebuffer();

    EnvironmentFramebuffer(
        const EnvironmentFramebuffer&
    ) = delete;

    EnvironmentFramebuffer& operator=(
        const EnvironmentFramebuffer&
    ) = delete;

    VkFramebuffer GetHandle(
        uint32_t faceIndex
    ) const;

    const std::array<
        VkFramebuffer,
        FaceCount
    >& GetHandles() const
    {
        return m_Framebuffers;
    }

    uint32_t GetWidth() const
    {
        return m_Width;
    }

    uint32_t GetHeight() const
    {
        return m_Height;
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const EnvironmentRenderPass& m_RenderPass;
    const TextureImage& m_CubemapImage;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;

    std::array<
        VkFramebuffer,
        FaceCount
    > m_Framebuffers{};

private:
    void CreateFramebuffers();
};