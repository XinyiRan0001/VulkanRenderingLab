#include "IrradiancePipeline.h"

#include "ShaderUtils.h"

#include <iostream>
#include <stdexcept>

IrradiancePipeline::IrradiancePipeline(
    const LogicalDevice& logicalDevice,
    const IrradianceRenderPass& renderPass,
    uint32_t cubemapSize
)
    : m_LogicalDevice(logicalDevice),
      m_RenderPass(renderPass),
      m_CubemapSize(cubemapSize)
{
    if (m_CubemapSize == 0)
    {
        throw std::runtime_error(
            "Irradiance pipeline requires a non-zero cubemap size."
        );
    }

    CreateDescriptorSetLayout();
    CreatePipeline();

    std::cout
        << "[IBL] Irradiance pipeline created."
        << std::endl;
}

IrradiancePipeline::~IrradiancePipeline()
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

void IrradiancePipeline::CreateDescriptorSetLayout()
{
    // Binding 0 will contain the completed environment cubemap.
    VkDescriptorSetLayoutBinding environmentBinding{};

    environmentBinding.binding =
        0;

    environmentBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    environmentBinding.descriptorCount =
        1;

    environmentBinding.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;

    environmentBinding.pImmutableSamplers =
        nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};

    layoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

    layoutInfo.bindingCount =
        1;

    layoutInfo.pBindings =
        &environmentBinding;

    if (vkCreateDescriptorSetLayout(
        m_LogicalDevice.GetHandle(),
        &layoutInfo,
        nullptr,
        &m_DescriptorSetLayout
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create Irradiance descriptor set layout."
        );
    }
}

VkShaderModule IrradiancePipeline::CreateShaderModule(
    const std::vector<char>& code
) const
{
    if (code.empty())
    {
        throw std::runtime_error(
            "Cannot create prefilter shader module from empty code."
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
            "Failed to create Irradiance shader module."
        );
    }

    return shaderModule;
}

void IrradiancePipeline::CreatePipeline()
{
    const std::vector<char> vertexShaderCode =
        ReadFile(
            "build/shaders/irradiance.vert.spv"
        );

    const std::vector<char> fragmentShaderCode =
        ReadFile(
            "build/shaders/irradiance.frag.spv"
        );

    VkShaderModule vertexShaderModule =
        CreateShaderModule(
            vertexShaderCode
        );

    VkShaderModule fragmentShaderModule =
        VK_NULL_HANDLE;

    try
    {
        fragmentShaderModule =
            CreateShaderModule(
                fragmentShaderCode
            );

        // =====================================================
        // Shader stages
        // =====================================================

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

        // =====================================================
        // No vertex buffer
        //
        // The cube is generated with gl_VertexIndex.
        // =====================================================

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

        // =====================================================
        // Dynamic viewport and scissor
        //
        // Every mip level has a different resolution.
        // =====================================================

        VkPipelineViewportStateCreateInfo viewportState{};

        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

        viewportState.viewportCount =
            1;

        viewportState.pViewports =
            nullptr;

        viewportState.scissorCount =
            1;

        viewportState.pScissors =
            nullptr;

        const VkDynamicState dynamicStates[] =
        {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicStateInfo{};

        dynamicStateInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

        dynamicStateInfo.dynamicStateCount =
            2;

        dynamicStateInfo.pDynamicStates =
            dynamicStates;

        // =====================================================
        // Rasterization
        // =====================================================

        VkPipelineRasterizationStateCreateInfo rasterizer{};

        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

        rasterizer.depthClampEnable =
            VK_FALSE;

        rasterizer.rasterizerDiscardEnable =
            VK_FALSE;

        rasterizer.polygonMode =
            VK_POLYGON_MODE_FILL;

        // Capture is viewed from inside the cube; do not cull its faces.
        rasterizer.cullMode = VK_CULL_MODE_NONE;

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

        // =====================================================
        // No depth buffer
        // =====================================================

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

        // =====================================================
        // Color output
        // =====================================================

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

        // =====================================================
        // Push constants
        // =====================================================

        VkPushConstantRange pushConstantRange{};

        pushConstantRange.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT;

        pushConstantRange.offset =
            0;

        pushConstantRange.size =
            sizeof(IrradiancePushConstants);

        // =====================================================
        // Pipeline layout
        // =====================================================

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
                "Failed to create Irradiance pipeline layout."
            );
        }

        // =====================================================
        // Graphics pipeline
        // =====================================================

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
            &dynamicStateInfo;

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
                "Failed to create Irradiance graphics pipeline."
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