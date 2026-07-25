#include "PrefilterFramebuffer.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

PrefilterFramebuffer::PrefilterFramebuffer(
    const LogicalDevice& logicalDevice,
    const EnvironmentRenderPass& renderPass,
    const TextureImage& cubemapImage,
    uint32_t baseWidth,
    uint32_t baseHeight,
    uint32_t mipLevels
)
    : m_LogicalDevice(logicalDevice),
    m_RenderPass(renderPass),
    m_CubemapImage(cubemapImage),
    m_BaseWidth(baseWidth),
    m_BaseHeight(baseHeight),
    m_MipLevels(mipLevels)
{
    if (m_BaseWidth == 0 ||
        m_BaseHeight == 0)
    {
        throw std::runtime_error(
            "Prefilter framebuffer dimensions must be greater than zero."
        );
    }

    if (m_MipLevels == 0)
    {
        throw std::runtime_error(
            "Prefilter framebuffer requires at least one mip level."
        );
    }

    CreateFramebuffers();

    std::cout
        << "[IBL] Created "
        << m_Framebuffers.size()
        << " prefilter framebuffers."
        << std::endl;
}

PrefilterFramebuffer::~PrefilterFramebuffer()
{
    VkDevice device =
        m_LogicalDevice.GetHandle();

    for (VkFramebuffer& framebuffer :
        m_Framebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(
                device,
                framebuffer,
                nullptr
            );

            framebuffer =
                VK_NULL_HANDLE;
        }
    }

    m_Framebuffers.clear();
}

VkFramebuffer PrefilterFramebuffer::GetHandle(
    uint32_t faceIndex,
    uint32_t mipLevel
) const
{
    if (faceIndex >= 6)
    {
        throw std::out_of_range(
            "Prefilter cubemap face index is out of range."
        );
    }

    if (mipLevel >= m_MipLevels)
    {
        throw std::out_of_range(
            "Prefilter cubemap mip level is out of range."
        );
    }

    const uint32_t framebufferIndex =
        mipLevel * 6 +
        faceIndex;

    return m_Framebuffers.at(
        framebufferIndex
    );
}

uint32_t PrefilterFramebuffer::GetMipWidth(
    uint32_t mipLevel
) const
{
    if (mipLevel >= m_MipLevels)
    {
        throw std::out_of_range(
            "Prefilter mip level is out of range."
        );
    }

    return std::max(
        1u,
        m_BaseWidth >> mipLevel
    );
}

uint32_t PrefilterFramebuffer::GetMipHeight(
    uint32_t mipLevel
) const
{
    if (mipLevel >= m_MipLevels)
    {
        throw std::out_of_range(
            "Prefilter mip level is out of range."
        );
    }

    return std::max(
        1u,
        m_BaseHeight >> mipLevel
    );
}

uint32_t PrefilterFramebuffer::GetFramebufferIndex(
    uint32_t faceIndex,
    uint32_t mipLevel
) const
{
    if (faceIndex >= FaceCount)
    {
        throw std::out_of_range(
            "Prefilter cubemap face index is out of range."
        );
    }

    if (mipLevel >= m_MipLevels)
    {
        throw std::out_of_range(
            "Prefilter cubemap mip level is out of range."
        );
    }

    return
        mipLevel * FaceCount +
        faceIndex;
}

void PrefilterFramebuffer::CreateFramebuffers()
{
    VkDevice device =
        m_LogicalDevice.GetHandle();

    const uint32_t framebufferCount =
        FaceCount * m_MipLevels;

    m_Framebuffers.resize(
        framebufferCount,
        VK_NULL_HANDLE
    );

    for (uint32_t mipLevel = 0;
        mipLevel < m_MipLevels;
        ++mipLevel)
    {
        const uint32_t mipWidth =
            GetMipWidth(mipLevel);

        const uint32_t mipHeight =
            GetMipHeight(mipLevel);

        for (uint32_t faceIndex = 0;
            faceIndex < FaceCount;
            ++faceIndex)
        {
            VkImageView faceMipImageView =
                m_CubemapImage.GetLayerImageView(
                    faceIndex,
                    mipLevel
                );

            if (faceMipImageView ==
                VK_NULL_HANDLE)
            {
                throw std::runtime_error(
                    "Prefilter cubemap face/mip image view is null."
                );
            }

            VkFramebufferCreateInfo
                framebufferInfo{};

            framebufferInfo.sType =
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

            framebufferInfo.renderPass =
                m_RenderPass.GetHandle();

            framebufferInfo.attachmentCount =
                1;

            framebufferInfo.pAttachments =
                &faceMipImageView;

            framebufferInfo.width =
                mipWidth;

            framebufferInfo.height =
                mipHeight;

            framebufferInfo.layers =
                1;

            const uint32_t framebufferIndex =
                GetFramebufferIndex(
                    faceIndex,
                    mipLevel
                );

            if (vkCreateFramebuffer(
                device,
                &framebufferInfo,
                nullptr,
                &m_Framebuffers[
                    framebufferIndex
                ]
            ) != VK_SUCCESS)
            {
                throw std::runtime_error(
                    "Failed to create prefilter cubemap framebuffer."
                );
            }
        }
    }
}