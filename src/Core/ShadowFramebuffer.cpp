#include "ShadowFramebuffer.h"

#include <iostream>
#include <stdexcept>

ShadowFramebuffer::ShadowFramebuffer(
    const LogicalDevice& logicalDevice,
    const ShadowRenderPass& shadowRenderPass,
    VkImageView shadowDepthImageView,
    uint32_t width,
    uint32_t height
)
    : m_LogicalDevice(logicalDevice)
{
    CreateFramebuffer(
        shadowRenderPass,
        shadowDepthImageView,
        width,
        height
    );

    std::cout
        << "ShadowFramebuffer created successfully."
        << std::endl;
}

ShadowFramebuffer::~ShadowFramebuffer()
{
    if (m_Framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(
            m_LogicalDevice.GetHandle(),
            m_Framebuffer,
            nullptr
        );

        m_Framebuffer = VK_NULL_HANDLE;
    }
}

void ShadowFramebuffer::CreateFramebuffer(
    const ShadowRenderPass& shadowRenderPass,
    VkImageView shadowDepthImageView,
    uint32_t width,
    uint32_t height
)
{
    VkFramebufferCreateInfo framebufferInfo{};

    framebufferInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

    framebufferInfo.renderPass =
        shadowRenderPass.GetHandle();

    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments =
        &shadowDepthImageView;

    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(
        m_LogicalDevice.GetHandle(),
        &framebufferInfo,
        nullptr,
        &m_Framebuffer
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shadow framebuffer."
        );
    }
}