#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "IrradianceRenderPass.h"
#include "TextureImage.h"

#include <array>
#include <cstdint>

class IrradianceFramebuffer
{
public:
    static constexpr uint32_t FaceCount = 6;

    IrradianceFramebuffer(
        const LogicalDevice& logicalDevice,
        const IrradianceRenderPass& renderPass,
        const TextureImage& cubemapImage,
        uint32_t width,
        uint32_t height
    );

    ~IrradianceFramebuffer();

    IrradianceFramebuffer(
        const IrradianceFramebuffer&
    ) = delete;

    IrradianceFramebuffer& operator=(
        const IrradianceFramebuffer&
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
    const IrradianceRenderPass& m_RenderPass;
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