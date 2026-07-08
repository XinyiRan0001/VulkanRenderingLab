#include "Framebuffer.h"

#include <array>
#include <iostream>
#include <stdexcept>

Framebuffer::Framebuffer(const LogicalDevice& logicalDevice,
    const Swapchain& swapchain,
    const RenderPass& renderPass,
    VkImageView depthImageView)
    : m_LogicalDevice(logicalDevice),
    m_Swapchain(swapchain),
    m_RenderPass(renderPass),
    m_DepthImageView(depthImageView)
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
        std::array<VkImageView, 2> attachments =
        {
            imageViews[i],
            m_DepthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass.GetHandle();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
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