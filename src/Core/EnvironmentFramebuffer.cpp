#include "EnvironmentFramebuffer.h"

#include <iostream>
#include <stdexcept>

EnvironmentFramebuffer::EnvironmentFramebuffer(
    const LogicalDevice& logicalDevice,
    const EnvironmentRenderPass& renderPass,
    const TextureImage& cubemapImage,
    uint32_t width,
    uint32_t height
)
    : m_LogicalDevice(logicalDevice),
    m_RenderPass(renderPass),
    m_CubemapImage(cubemapImage),
    m_Width(width),
    m_Height(height)
{
    if (m_Width == 0 ||
        m_Height == 0)
    {
        throw std::runtime_error(
            "Environment framebuffer dimensions must be greater than zero."
        );
    }

    CreateFramebuffers();

    std::cout
        << "[IBL] Created 6 environment framebuffers."
        << std::endl;
}

EnvironmentFramebuffer::~EnvironmentFramebuffer()
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
}

VkFramebuffer EnvironmentFramebuffer::GetHandle(
    uint32_t faceIndex
) const
{
    if (faceIndex >= FaceCount)
    {
        throw std::out_of_range(
            "Environment cubemap face index is out of range."
        );
    }

    return m_Framebuffers[faceIndex];
}

void EnvironmentFramebuffer::CreateFramebuffers()
{
    VkDevice device =
        m_LogicalDevice.GetHandle();

    for (uint32_t faceIndex = 0;
        faceIndex < FaceCount;
        ++faceIndex)
    {
        VkImageView faceImageView =
            m_CubemapImage.GetLayerImageView(
                faceIndex
            );

        if (faceImageView == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "Environment cubemap face image view is null."
            );
        }

        VkFramebufferCreateInfo framebufferInfo{};

        framebufferInfo.sType =
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

        framebufferInfo.renderPass =
            m_RenderPass.GetHandle();

        framebufferInfo.attachmentCount =
            1;

        framebufferInfo.pAttachments =
            &faceImageView;

        framebufferInfo.width =
            m_Width;

        framebufferInfo.height =
            m_Height;

        framebufferInfo.layers =
            1;

        if (vkCreateFramebuffer(
            device,
            &framebufferInfo,
            nullptr,
            &m_Framebuffers[faceIndex]
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to create environment cubemap framebuffer."
            );
        }
    }
}