#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "LogicalDevice.h"
#include "EnvironmentRenderPass.h"
#include "TextureImage.h"

#include <cstdint>
#include <vector>

class PrefilterFramebuffer
{
public:
    static constexpr uint32_t FaceCount =
        6;

    PrefilterFramebuffer(
        const LogicalDevice& logicalDevice,
        const EnvironmentRenderPass& renderPass,
        const TextureImage& cubemapImage,
        uint32_t baseWidth,
        uint32_t baseHeight,
        uint32_t mipLevels
    );

    ~PrefilterFramebuffer();

    PrefilterFramebuffer(
        const PrefilterFramebuffer&
    ) = delete;

    PrefilterFramebuffer& operator=(
        const PrefilterFramebuffer&
    ) = delete;

    VkFramebuffer GetHandle(
        uint32_t faceIndex,
        uint32_t mipLevel
    ) const;

    uint32_t GetMipWidth(
        uint32_t mipLevel
    ) const;

    uint32_t GetMipHeight(
        uint32_t mipLevel
    ) const;

    uint32_t GetMipLevels() const
    {
        return m_MipLevels;
    }

    uint32_t GetFramebufferCount() const
    {
        return static_cast<uint32_t>(
            m_Framebuffers.size()
        );
    }

private:
    const LogicalDevice& m_LogicalDevice;
    const EnvironmentRenderPass& m_RenderPass;
    const TextureImage& m_CubemapImage;

    uint32_t m_BaseWidth = 0;
    uint32_t m_BaseHeight = 0;
    uint32_t m_MipLevels = 0;

    std::vector<VkFramebuffer>
        m_Framebuffers;

private:
    void CreateFramebuffers();

    uint32_t GetFramebufferIndex(
        uint32_t faceIndex,
        uint32_t mipLevel
    ) const;
};