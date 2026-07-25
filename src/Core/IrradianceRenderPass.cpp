#include "IrradianceRenderPass.h"

#include <iostream>
#include <stdexcept>

IrradianceRenderPass::IrradianceRenderPass(
    const LogicalDevice& logicalDevice,
    VkFormat colorFormat
)
    : m_LogicalDevice(logicalDevice),
    m_ColorFormat(colorFormat)
{
    if (m_ColorFormat == VK_FORMAT_UNDEFINED)
    {
        throw std::runtime_error(
            "Irradiance render pass requires a valid color format."
        );
    }

    CreateRenderPass();

    std::cout
        << "[IBL] Irradiance render pass created."
        << std::endl;
}

IrradianceRenderPass::~IrradianceRenderPass()
{
    if (m_RenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(
            m_LogicalDevice.GetHandle(),
            m_RenderPass,
            nullptr
        );

        m_RenderPass =
            VK_NULL_HANDLE;
    }
}

void IrradianceRenderPass::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};

    colorAttachment.format =
        m_ColorFormat;

    colorAttachment.samples =
        VK_SAMPLE_COUNT_1_BIT;

    colorAttachment.loadOp =
        VK_ATTACHMENT_LOAD_OP_CLEAR;

    colorAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.stencilLoadOp =
        VK_ATTACHMENT_LOAD_OP_DONT_CARE;

    colorAttachment.stencilStoreOp =
        VK_ATTACHMENT_STORE_OP_DONT_CARE;

    colorAttachment.initialLayout =
        VK_IMAGE_LAYOUT_UNDEFINED;

    colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentReference{};

    colorAttachmentReference.attachment =
        0;

    colorAttachmentReference.layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};

    subpass.pipelineBindPoint =
        VK_PIPELINE_BIND_POINT_GRAPHICS;

    subpass.colorAttachmentCount =
        1;

    subpass.pColorAttachments =
        &colorAttachmentReference;

    subpass.pDepthStencilAttachment =
        nullptr;

    VkSubpassDependency externalToSubpass{};

    externalToSubpass.srcSubpass =
        VK_SUBPASS_EXTERNAL;

    externalToSubpass.dstSubpass =
        0;

    externalToSubpass.srcStageMask =
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    externalToSubpass.srcAccessMask =
        0;

    externalToSubpass.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    externalToSubpass.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency subpassToExternal{};

    subpassToExternal.srcSubpass =
        0;

    subpassToExternal.dstSubpass =
        VK_SUBPASS_EXTERNAL;

    subpassToExternal.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    subpassToExternal.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    subpassToExternal.dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    subpassToExternal.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    VkSubpassDependency dependencies[] =
    {
        externalToSubpass,
        subpassToExternal
    };

    VkRenderPassCreateInfo renderPassInfo{};

    renderPassInfo.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    renderPassInfo.attachmentCount =
        1;

    renderPassInfo.pAttachments =
        &colorAttachment;

    renderPassInfo.subpassCount =
        1;

    renderPassInfo.pSubpasses =
        &subpass;

    renderPassInfo.dependencyCount =
        2;

    renderPassInfo.pDependencies =
        dependencies;

    if (vkCreateRenderPass(
        m_LogicalDevice.GetHandle(),
        &renderPassInfo,
        nullptr,
        &m_RenderPass
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create Irradiance render pass."
        );
    }
}