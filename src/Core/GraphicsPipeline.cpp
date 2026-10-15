#include "GraphicsPipeline.h"
#include "ShaderUtils.h"
#include "MeshData.h"

#include <stdexcept>
#include <iostream>
#include <vector>
#include <array>
#include "Vertex.h"

GraphicsPipeline::GraphicsPipeline(
    const LogicalDevice& logicalDevice,
    const RenderPass& renderPass,
    const Swapchain& swapchain,
    VkDescriptorSetLayout descriptorSetLayout
)
    : m_LogicalDevice(logicalDevice),
    m_RenderPass(renderPass),
    m_Swapchain(swapchain),
    m_DescriptorSetLayout(descriptorSetLayout)
{
    if (m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        throw std::runtime_error(
            "GraphicsPipeline received a null descriptor set layout."
        );
    }

    CreateGraphicsPipeline();

    std::cout
        << "GraphicsPipeline created successfully."
        << std::endl;
}

GraphicsPipeline::~GraphicsPipeline()
{
    VkDevice device =
        m_LogicalDevice.GetHandle();

    if (m_GraphicsPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(
            device,
            m_GraphicsPipeline,
            nullptr
        );

        m_GraphicsPipeline =
            VK_NULL_HANDLE;
    }

    if (m_PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(
            device,
            m_PipelineLayout,
            nullptr
        );

        m_PipelineLayout =
            VK_NULL_HANDLE;
    }
}

VkShaderModule GraphicsPipeline::CreateShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;

    if (vkCreateShaderModule(
        m_LogicalDevice.GetHandle(),
        &createInfo,
        nullptr,
        &shaderModule) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shader module.");
    }

    return shaderModule;
}

void GraphicsPipeline::CreateGraphicsPipeline()
{
    std::cout << "[Pipeline] Loading vertex shader..." << std::endl;
    auto vertShaderCode = ReadFile("build/shaders/triangle.vert.spv");

    std::cout << "[Pipeline] Loading fragment shader..." << std::endl;
    auto fragShaderCode = ReadFile("build/shaders/triangle.frag.spv");

    std::cout << "[Pipeline] Creating vertex shader module..." << std::endl;
    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);

    std::cout << "[Pipeline] Creating fragment shader module..." << std::endl;
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    std::cout << "[Pipeline] Shader modules created." << std::endl;

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    auto bindingDescription = Vertex::GetBindingDescription();
    auto attributeDescriptions = Vertex::GetAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
    viewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_Swapchain.GetExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;

    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size =
        sizeof(MaterialPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;

    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges =
        &pushConstantRange;

    std::cout << "[Pipeline] Creating pipeline layout..." << std::endl;
    if (vkCreatePipelineLayout(
        m_LogicalDevice.GetHandle(),
        &pipelineLayoutInfo,
        nullptr,
        &m_PipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout.");
    }
    std::cout << "[Pipeline] Pipeline layout created." << std::endl;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = m_RenderPass.GetHandle();
    pipelineInfo.subpass = 0;

    std::cout << "[Pipeline] Creating graphics pipeline..." << std::endl;
    if (vkCreateGraphicsPipelines(
        m_LogicalDevice.GetHandle(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &m_GraphicsPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline.");
    }

    vkDestroyShaderModule(m_LogicalDevice.GetHandle(), fragShaderModule, nullptr);
    vkDestroyShaderModule(m_LogicalDevice.GetHandle(), vertShaderModule, nullptr);
}