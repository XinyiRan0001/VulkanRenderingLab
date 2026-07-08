#include "Framebuffer.h"

#include <iostream>
#include <stdexcept>

Framebuffer::Framebuffer(const LogicalDevice& logicalDevice,
    const Swapchain& swapchain,
    const RenderPass& renderPass)
    : m_LogicalDevice(logicalDevice),
    m_Swapchain(swapchain),
    m_RenderPass(renderPass)
{
    CreateFramebuffers();

    std::cout << "Framebuffers created successfully." << std::endl;
}

Framebuffer::~Framebuffer()
{
    for (VkFramebuffer framebuffer : m_Framebuffers)
    {
        vkDestroyFramebuffer(
            m_LogicalDevice.GetHandle(),
            framebuffer,
            nullptr
        );
    }
}

void Framebuffer::CreateFramebuffers()
{
    const auto& imageViews = m_Swapchain.GetImageViews();
    m_Framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        VkImageView attachments[] =
        {
            imageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass.GetHandle();
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_Swapchain.GetExtent().width;
        framebufferInfo.height = m_Swapchain.GetExtent().height;
        framebufferInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(
            m_LogicalDevice.GetHandle(),
            &framebufferInfo,
            nullptr,
            &m_Framebuffers[i]
        );

        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer.");
        }
    }
}