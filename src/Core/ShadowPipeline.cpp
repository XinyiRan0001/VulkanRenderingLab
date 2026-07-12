#include "ShadowPipeline.h"

#include "ShaderUtils.h"
#include "Vertex.h"

#include <iostream>
#include <stdexcept>

ShadowPipeline::ShadowPipeline(
    const LogicalDevice& logicalDevice,
    const ShadowRenderPass& shadowRenderPass
)
    : m_LogicalDevice(logicalDevice),
    m_ShadowRenderPass(shadowRenderPass)
{
    CreatePipeline();

    std::cout
        << "ShadowPipeline created successfully."
        << std::endl;
}

ShadowPipeline::~ShadowPipeline()
{
    VkDevice device = m_LogicalDevice.GetHandle();

    if (m_Pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(
            device,
            m_Pipeline,
            nullptr
        );

        m_Pipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(
            device,
            m_PipelineLayout,
            nullptr
        );

        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

VkShaderModule ShadowPipeline::CreateShaderModule(
    const std::vector<char>& code
)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType =
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    createInfo.codeSize = code.size();

    createInfo.pCode =
        reinterpret_cast<const uint32_t*>(
            code.data()
            );

    VkShaderModule shaderModule =
        VK_NULL_HANDLE;

    if (vkCreateShaderModule(
        m_LogicalDevice.GetHandle(),
        &createInfo,
        nullptr,
        &shaderModule
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shadow shader module."
        );
    }

    return shaderModule;
}

void ShadowPipeline::CreatePipeline()
{
    const std::vector<char> vertexShaderCode =
        ReadFile(
            "build/shaders/shadow.vert.spv"
        );

    const std::vector<char> fragmentShaderCode =
        ReadFile(
            "build/shaders/shadow.frag.spv"
        );

    VkShaderModule vertexShaderModule =
        CreateShaderModule(vertexShaderCode);

    VkShaderModule fragmentShaderModule =
        CreateShaderModule(fragmentShaderCode);

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    vertexStage.stage =
        VK_SHADER_STAGE_VERTEX_BIT;

    vertexStage.module =
        vertexShaderModule;

    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    fragmentStage.stage =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    fragmentStage.module =
        fragmentShaderModule;

    fragmentStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] =
    {
        vertexStage,
        fragmentStage
    };

    const VkVertexInputBindingDescription bindingDescription =
        Vertex::GetBindingDescription();

    const auto attributeDescriptions =
        Vertex::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions =
        &bindingDescription;

    // Shadow shader only reads position at location 0.
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions =
        &attributeDescriptions[0];

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

    inputAssembly.topology =
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 2048.0f;
    viewport.height = 2048.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { 2048, 2048 };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;

    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;

    rasterizer.polygonMode =
        VK_POLYGON_MODE_FILL;

    rasterizer.lineWidth = 1.0f;

    rasterizer.cullMode =
        VK_CULL_MODE_NONE;

    rasterizer.frontFace =
        VK_FRONT_FACE_CLOCKWISE;

    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

    multisampling.rasterizationSamples =
        VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp =
        VK_COMPARE_OP_LESS;

    depthStencil.depthBoundsTestEnable =
        VK_FALSE;

    depthStencil.stencilTestEnable =
        VK_FALSE;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT;

    pushConstantRange.offset = 0;
    pushConstantRange.size =
        sizeof(float) * 32;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;

    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges =
        &pushConstantRange;

    if (vkCreatePipelineLayout(
        m_LogicalDevice.GetHandle(),
        &layoutInfo,
        nullptr,
        &m_PipelineLayout
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shadow pipeline layout."
        );
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState =
        &vertexInput;

    pipelineInfo.pInputAssemblyState =
        &inputAssembly;

    pipelineInfo.pViewportState =
        &viewportState;

    pipelineInfo.pRasterizationState =
        &rasterizer;

    pipelineInfo.pMultisampleState =
        &multisampling;

    pipelineInfo.pDepthStencilState =
        &depthStencil;

    pipelineInfo.pColorBlendState =
        nullptr;

    pipelineInfo.layout =
        m_PipelineLayout;

    pipelineInfo.renderPass =
        m_ShadowRenderPass.GetHandle();

    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(
        m_LogicalDevice.GetHandle(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &m_Pipeline
    ) != VK_SUCCESS)
    {
        vkDestroyShaderModule(
            m_LogicalDevice.GetHandle(),
            fragmentShaderModule,
            nullptr
        );

        vkDestroyShaderModule(
            m_LogicalDevice.GetHandle(),
            vertexShaderModule,
            nullptr
        );

        throw std::runtime_error(
            "Failed to create shadow graphics pipeline."
        );
    }

    vkDestroyShaderModule(
        m_LogicalDevice.GetHandle(),
        fragmentShaderModule,
        nullptr
    );

    vkDestroyShaderModule(
        m_LogicalDevice.GetHandle(),
        vertexShaderModule,
        nullptr
    );
}