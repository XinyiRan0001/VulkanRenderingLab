#include "ShadowRenderPass.h"

#include <iostream>
#include <stdexcept>
#include <array>

ShadowRenderPass::ShadowRenderPass(
    const LogicalDevice& logicalDevice
)
    : m_LogicalDevice(logicalDevice)
{
    CreateRenderPass();

    std::cout
        << "ShadowRenderPass created successfully."
        << std::endl;
}

ShadowRenderPass::~ShadowRenderPass()
{
    if (m_RenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(
            m_LogicalDevice.GetHandle(),
            m_RenderPass,
            nullptr
        );

        m_RenderPass = VK_NULL_HANDLE;
    }
}

void ShadowRenderPass::CreateRenderPass()
{
    VkAttachmentDescription depthAttachment{};

    depthAttachment.format =
        VK_FORMAT_D32_SFLOAT;

    depthAttachment.samples =
        VK_SAMPLE_COUNT_1_BIT;

    depthAttachment.loadOp =
        VK_ATTACHMENT_LOAD_OP_CLEAR;

    depthAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;

    depthAttachment.stencilLoadOp =
        VK_ATTACHMENT_LOAD_OP_DONT_CARE;

    depthAttachment.stencilStoreOp =
        VK_ATTACHMENT_STORE_OP_DONT_CARE;

    depthAttachment.initialLayout =
        VK_IMAGE_LAYOUT_UNDEFINED;

    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};

    depthAttachmentReference.attachment = 0;

    depthAttachmentReference.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};

    subpass.pipelineBindPoint =
        VK_PIPELINE_BIND_POINT_GRAPHICS;

    subpass.colorAttachmentCount = 0;
    subpass.pColorAttachments = nullptr;

    subpass.pDepthStencilAttachment =
        &depthAttachmentReference;

    std::array<VkSubpassDependency, 2> dependencies{};

    // Previous fragment-shader read -> shadow depth write.
    dependencies[0].srcSubpass =
        VK_SUBPASS_EXTERNAL;

    dependencies[0].dstSubpass = 0;

    dependencies[0].srcStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    dependencies[0].srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    dependencies[0].dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[0].dependencyFlags =
        VK_DEPENDENCY_BY_REGION_BIT;

    // Shadow depth write -> main-pass fragment-shader read.
    dependencies[1].srcSubpass = 0;

    dependencies[1].dstSubpass =
        VK_SUBPASS_EXTERNAL;

    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    dependencies[1].srcAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    dependencies[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;

    dependencies[1].dependencyFlags =
        VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};

    renderPassInfo.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments =
        &depthAttachment;

    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses =
        &subpass;

    renderPassInfo.dependencyCount =
        static_cast<uint32_t>(
            dependencies.size()
            );

    renderPassInfo.pDependencies =
        dependencies.data();

    if (vkCreateRenderPass(
        m_LogicalDevice.GetHandle(),
        &renderPassInfo,
        nullptr,
        &m_RenderPass
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shadow render pass."
        );
    }
}