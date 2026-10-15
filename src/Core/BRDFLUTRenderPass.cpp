#include "BRDFLUTRenderPass.h"

#include "LogicalDevice.h"

#include <iostream>
#include <stdexcept>

BRDFLUTRenderPass::BRDFLUTRenderPass(
    const LogicalDevice& logicalDevice,
    VkFormat colorFormat
)
    : m_LogicalDevice(logicalDevice)
{
    CreateRenderPass(
        colorFormat
    );

    std::cout
        << "[IBL] BRDF LUT render pass created."
        << std::endl;
}

BRDFLUTRenderPass::~BRDFLUTRenderPass()
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

void BRDFLUTRenderPass::CreateRenderPass(
    VkFormat colorFormat
)
{
    VkAttachmentDescription
        colorAttachment{};

    colorAttachment.format =
        colorFormat;

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

    VkAttachmentReference
        colorAttachmentReference{};

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

    VkSubpassDependency dependencies[2]{};

    // ---------------------------------------------------------
    // External operations -> BRDF LUT color attachment
    // ---------------------------------------------------------

    dependencies[0].srcSubpass =
        VK_SUBPASS_EXTERNAL;

    dependencies[0].dstSubpass =
        0;

    dependencies[0].srcStageMask =
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    dependencies[0].srcAccessMask =
        0;

    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // ---------------------------------------------------------
    // BRDF LUT color attachment -> fragment shader sampling
    // ---------------------------------------------------------

    dependencies[1].srcSubpass =
        0;

    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo
        renderPassInfo{};

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
            "Failed to create BRDF LUT render pass."
        );
    }
}