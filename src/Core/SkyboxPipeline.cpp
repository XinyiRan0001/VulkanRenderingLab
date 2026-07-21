#include "SkyboxPipeline.h"
#include "ShaderUtils.h"

#include <iostream>
#include <stdexcept>

SkyboxPipeline::SkyboxPipeline(
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
            "SkyboxPipeline received a null descriptor set layout."
        );
    }

    CreatePipeline();

    std::cout
        << "[Pipeline] Skybox pipeline created successfully."
        << std::endl;
}

SkyboxPipeline::~SkyboxPipeline()
{
    const VkDevice device =
        m_LogicalDevice.GetHandle();

    if (m_Pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(
            device,
            m_Pipeline,
            nullptr
        );

        m_Pipeline =
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

VkShaderModule SkyboxPipeline::CreateShaderModule(
    const std::vector<char>& code
)
{
    VkShaderModuleCreateInfo createInfo{};

    createInfo.sType =
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

    createInfo.codeSize =
        code.size();

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
            "Failed to create skybox shader module."
        );
    }

    return shaderModule;
}

void SkyboxPipeline::CreatePipeline()
{
    std::cout
        << "[SkyboxPipeline] Loading vertex shader..."
        << std::endl;

    const std::vector<char> vertShaderCode =
        ReadFile("build/shaders/skybox.vert.spv");

    std::cout
        << "[SkyboxPipeline] Loading fragment shader..."
        << std::endl;

    const std::vector<char> fragShaderCode =
        ReadFile("build/shaders/skybox.frag.spv");

    const VkShaderModule vertShaderModule =
        CreateShaderModule(vertShaderCode);

    const VkShaderModule fragShaderModule =
        CreateShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStage{};

    vertStage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    vertStage.stage =
        VK_SHADER_STAGE_VERTEX_BIT;

    vertStage.module =
        vertShaderModule;

    vertStage.pName =
        "main";

    VkPipelineShaderStageCreateInfo fragStage{};

    fragStage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

    fragStage.stage =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    fragStage.module =
        fragShaderModule;

    fragStage.pName =
        "main";

    const VkPipelineShaderStageCreateInfo shaderStages[] =
    {
        vertStage,
        fragStage
    };

    // The skybox vertex shader uses gl_VertexIndex.
    // No vertex buffer or vertex attributes are required.
    VkPipelineVertexInputStateCreateInfo vertexInput{};

    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    vertexInput.vertexBindingDescriptionCount = 0;
    vertexInput.pVertexBindingDescriptions = nullptr;

    vertexInput.vertexAttributeDescriptionCount = 0;
    vertexInput.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};

    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

    inputAssembly.topology =
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    inputAssembly.primitiveRestartEnable =
        VK_FALSE;

    VkViewport viewport{};

    viewport.x = 0.0f;
    viewport.y = 0.0f;

    viewport.width =
        static_cast<float>(
            m_Swapchain.GetExtent().width
        );

    viewport.height =
        static_cast<float>(
            m_Swapchain.GetExtent().height
        );

    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};

    scissor.offset =
    {
        0,
        0
    };

    scissor.extent =
        m_Swapchain.GetExtent();

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

    rasterizer.depthClampEnable =
        VK_FALSE;

    rasterizer.rasterizerDiscardEnable =
        VK_FALSE;

    rasterizer.polygonMode =
        VK_POLYGON_MODE_FILL;

    rasterizer.lineWidth =
        1.0f;

    // We are conceptually viewing the cube from its inside.
    rasterizer.cullMode =
        VK_CULL_MODE_FRONT_BIT;

    rasterizer.frontFace =
        VK_FRONT_FACE_COUNTER_CLOCKWISE;

    rasterizer.depthBiasEnable =
        VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};

    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

    multisampling.rasterizationSamples =
        VK_SAMPLE_COUNT_1_BIT;

    multisampling.sampleShadingEnable =
        VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};

    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    // skybox.vert uses gl_Position.xyww, producing far-plane depth.
    depthStencil.depthTestEnable =
        VK_TRUE;

    // Never allow the skybox to overwrite scene depth.
    depthStencil.depthWriteEnable =
        VK_FALSE;

    depthStencil.depthCompareOp =
        VK_COMPARE_OP_LESS_OR_EQUAL;

    depthStencil.depthBoundsTestEnable =
        VK_FALSE;

    depthStencil.stencilTestEnable =
        VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};

    colorBlendAttachment.blendEnable =
        VK_FALSE;

    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};

    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    colorBlending.logicOpEnable =
        VK_FALSE;

    colorBlending.attachmentCount =
        1;

    colorBlending.pAttachments =
        &colorBlendAttachment;

    // The skybox uses the same global descriptor set:
    // binding 0 = UBO
    // binding 3 = HDR environment texture
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};

    pipelineLayoutInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    pipelineLayoutInfo.setLayoutCount =
        1;

    pipelineLayoutInfo.pSetLayouts =
        &m_DescriptorSetLayout;

    // Skybox shaders do not use material push constants.
    pipelineLayoutInfo.pushConstantRangeCount =
        0;

    pipelineLayoutInfo.pPushConstantRanges =
        nullptr;

    if (vkCreatePipelineLayout(
        m_LogicalDevice.GetHandle(),
        &pipelineLayoutInfo,
        nullptr,
        &m_PipelineLayout
    ) != VK_SUCCESS)
    {
        vkDestroyShaderModule(
            m_LogicalDevice.GetHandle(),
            fragShaderModule,
            nullptr
        );

        vkDestroyShaderModule(
            m_LogicalDevice.GetHandle(),
            vertShaderModule,
            nullptr
        );

        throw std::runtime_error(
            "Failed to create skybox pipeline layout."
        );
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};

    pipelineInfo.sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    pipelineInfo.stageCount =
        2;

    pipelineInfo.pStages =
        shaderStages;

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
        &colorBlending;

    pipelineInfo.pDynamicState =
        nullptr;

    pipelineInfo.layout =
        m_PipelineLayout;

    pipelineInfo.renderPass =
        m_RenderPass.GetHandle();

    pipelineInfo.subpass =
        0;

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
            fragShaderModule,
            nullptr
        );

        vkDestroyShaderModule(
            m_LogicalDevice.GetHandle(),
            vertShaderModule,
            nullptr
        );

        throw std::runtime_error(
            "Failed to create skybox graphics pipeline."
        );
    }

    vkDestroyShaderModule(
        m_LogicalDevice.GetHandle(),
        fragShaderModule,
        nullptr
    );

    vkDestroyShaderModule(
        m_LogicalDevice.GetHandle(),
        vertShaderModule,
        nullptr
    );
}