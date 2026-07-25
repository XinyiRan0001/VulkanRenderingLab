#include "BRDFLUTFramebuffer.h"

#include "BRDFLUTRenderPass.h"
#include "LogicalDevice.h"
#include "TextureImage.h"

#include <iostream>
#include <stdexcept>

BRDFLUTFramebuffer::BRDFLUTFramebuffer(
    const LogicalDevice& logicalDevice,
    const BRDFLUTRenderPass& renderPass,
    const TextureImage& brdfLUT,
    uint32_t width,
    uint32_t height
)
    : m_LogicalDevice(logicalDevice)
{
    const VkImageView attachment =
        brdfLUT.GetImageView();

    VkFramebufferCreateInfo
        framebufferInfo{};

    framebufferInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

    framebufferInfo.renderPass =
        renderPass.GetHandle();

    framebufferInfo.attachmentCount =
        1;

    framebufferInfo.pAttachments =
        &attachment;

    framebufferInfo.width =
        width;

    framebufferInfo.height =
        height;

    framebufferInfo.layers =
        1;

    if (vkCreateFramebuffer(
        m_LogicalDevice.GetHandle(),
        &framebufferInfo,
        nullptr,
        &m_Framebuffer
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create BRDF LUT framebuffer."
        );
    }

    std::cout
        << "[IBL] BRDF LUT framebuffer created."
        << std::endl;
}

BRDFLUTFramebuffer::~BRDFLUTFramebuffer()
{
    if (m_Framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(
            m_LogicalDevice.GetHandle(),
            m_Framebuffer,
            nullptr
        );

        m_Framebuffer =
            VK_NULL_HANDLE;
    }
}