#include "HDRToCubemapPipeline.h"

#include "ShaderUtils.h"

#include <iostream>
#include <stdexcept>

HDRToCubemapPipeline::HDRToCubemapPipeline(
    const LogicalDevice& logicalDevice,
    const EnvironmentRenderPass& renderPass,
    uint32_t cubemapSize
)
    : m_LogicalDevice(logicalDevice),
    m_RenderPass(renderPass),
    m_CubemapSize(cubemapSize)
{
    if (m_CubemapSize == 0)
    {
        throw std::runtime_error(
            "HDR-to-cubemap pipeline requires a non-zero cubemap size."
        );
    }

    CreateDescriptorSetLayout();
    CreatePipeline();

    std::cout
        << "[IBL] HDR-to-cubemap pipeline created."
        << std::endl;
}

HDRToCubemapPipeline::~HDRToCubemapPipeline()
{
    VkDevice device =
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

    if (m_DescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(
            device,
            m_DescriptorSetLayout,
            nullptr
        );

        m_DescriptorSetLayout =
            VK_NULL_HANDLE;
    }
}

void HDRToCubemapPipeline::CreateDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding hdrBinding{};

    hdrBinding.binding =
        0;

    hdrBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    hdrBinding.descriptorCount =
        1;

    hdrBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    hdrBinding.pImmutableSamplers =
        nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};

    layoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

    layoutInfo.bindingCount =
        1;

    layoutInfo.pBindings =
        &hdrBinding;

    if (vkCreateDescriptorSetLayout(
        m_LogicalDevice.GetHandle(),
        &layoutInfo,
        nullptr,
        &m_DescriptorSetLayout
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create HDR-to-cubemap descriptor set layout."
        );
    }
}

VkShaderModule HDRToCubemapPipeline::CreateShaderModule(
    const std::vector<char>& code
) const
{
    if (code.empty())
    {
        throw std::runtime_error(
            "Cannot create shader module from empty shader code."
        );
    }

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
            "Failed to create HDR-to-cubemap shader module."
        );
    }

    return shaderModule;
}

void HDRToCubemapPipeline::CreatePipeline()
{
    const std::vector<char> vertexShaderCode =
        ReadFile(
            "build/shaders/hdr_to_cubemap.vert.spv"
        );

    const std::vector<char> fragmentShaderCode =
        ReadFile(
            "build/shaders/hdr_to_cubemap.frag.spv"
        );

    VkShaderModule vertexShaderModule =
        CreateShaderModule(vertexShaderCode);

    VkShaderModule fragmentShaderModule =
        VK_NULL_HANDLE;

    try
    {
        fragmentShaderModule =
            CreateShaderModule(fragmentShaderCode);

        VkPipelineShaderStageCreateInfo vertexStage{};

        vertexStage.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

        vertexStage.stage =
            VK_SHADER_STAGE_VERTEX_BIT;

        vertexStage.module =
            vertexShaderModule;

        vertexStage.pName =
            "main";

        VkPipelineShaderStageCreateInfo fragmentStage{};

        fragmentStage.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

        fragmentStage.stage =
            VK_SHADER_STAGE_FRAGMENT_BIT;

        fragmentStage.module =
            fragmentShaderModule;

        fragmentStage.pName =
            "main";

        const VkPipelineShaderStageCreateInfo shaderStages[] =
        {
            vertexStage,
            fragmentStage
        };

        // Vertex positions are generated from gl_VertexIndex.
        VkPipelineVertexInputStateCreateInfo vertexInput{};

        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        vertexInput.vertexBindingDescriptionCount =
            0;

        vertexInput.pVertexBindingDescriptions =
            nullptr;

        vertexInput.vertexAttributeDescriptionCount =
            0;

        vertexInput.pVertexAttributeDescriptions =
            nullptr;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};

        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

        inputAssembly.topology =
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        inputAssembly.primitiveRestartEnable =
            VK_FALSE;

        VkViewport viewport{};

        viewport.x =
            0.0f;

        viewport.y =
            0.0f;

        viewport.width =
            static_cast<float>(m_CubemapSize);

        viewport.height =
            static_cast<float>(m_CubemapSize);

        viewport.minDepth =
            0.0f;

        viewport.maxDepth =
            1.0f;

        VkRect2D scissor{};

        scissor.offset =
        {
            0,
            0
        };

        scissor.extent =
        {
            m_CubemapSize,
            m_CubemapSize
        };

        VkPipelineViewportStateCreateInfo viewportState{};

        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

        viewportState.viewportCount =
            1;

        viewportState.pViewports =
            &viewport;

        viewportState.scissorCount =
            1;

        viewportState.pScissors =
            &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};

        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

        rasterizer.depthClampEnable =
            VK_FALSE;

        rasterizer.rasterizerDiscardEnable =
            VK_FALSE;

        rasterizer.polygonMode =
            VK_POLYGON_MODE_FILL;

        // The camera is inside the cube, so render its inner faces.
        rasterizer.cullMode =
            VK_CULL_MODE_NONE;

        rasterizer.frontFace =
            VK_FRONT_FACE_COUNTER_CLOCKWISE;

        rasterizer.depthBiasEnable =
            VK_FALSE;

        rasterizer.lineWidth =
            1.0f;

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

        depthStencil.depthTestEnable =
            VK_FALSE;

        depthStencil.depthWriteEnable =
            VK_FALSE;

        depthStencil.depthCompareOp =
            VK_COMPARE_OP_ALWAYS;

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

        VkPushConstantRange pushConstantRange{};

        pushConstantRange.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT;

        pushConstantRange.offset =
            0;

        // mat4 viewProjection
        pushConstantRange.size =
            sizeof(float) * 16;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};

        pipelineLayoutInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        pipelineLayoutInfo.setLayoutCount =
            1;

        pipelineLayoutInfo.pSetLayouts =
            &m_DescriptorSetLayout;

        pipelineLayoutInfo.pushConstantRangeCount =
            1;

        pipelineLayoutInfo.pPushConstantRanges =
            &pushConstantRange;

        if (vkCreatePipelineLayout(
            m_LogicalDevice.GetHandle(),
            &pipelineLayoutInfo,
            nullptr,
            &m_PipelineLayout
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to create HDR-to-cubemap pipeline layout."
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
            throw std::runtime_error(
                "Failed to create HDR-to-cubemap graphics pipeline."
            );
        }
    }
    catch (...)
    {
        if (fragmentShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(
                m_LogicalDevice.GetHandle(),
                fragmentShaderModule,
                nullptr
            );
        }

        vkDestroyShaderModule(
            m_LogicalDevice.GetHandle(),
            vertexShaderModule,
            nullptr
        );

        throw;
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