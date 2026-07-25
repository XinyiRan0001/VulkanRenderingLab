#include "BRDFLUTPipeline.h"

#include "BRDFLUTRenderPass.h"
#include "LogicalDevice.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

// =========================================================
// Constructor
// =========================================================

BRDFLUTPipeline::BRDFLUTPipeline(
    const LogicalDevice& logicalDevice,
    const BRDFLUTRenderPass& renderPass,
    uint32_t lutSize
)
    : m_LogicalDevice(logicalDevice)
{
    CreatePipeline(
        renderPass,
        lutSize
    );

    std::cout
        << "[IBL] BRDF LUT pipeline created."
        << std::endl;
}

// =========================================================
// Destructor
// =========================================================

BRDFLUTPipeline::~BRDFLUTPipeline()
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

// =========================================================
// Create graphics pipeline
// =========================================================

void BRDFLUTPipeline::CreatePipeline(
    const BRDFLUTRenderPass& renderPass,
    uint32_t lutSize
)
{
    const std::vector<char> vertexShaderCode =
        ReadFile(
            "build/shaders/brdf_lut.vert.spv"
        );

    const std::vector<char> fragmentShaderCode =
        ReadFile(
            "build/shaders/brdf_lut.frag.spv"
        );

    const VkShaderModule vertexShaderModule =
        CreateShaderModule(
            vertexShaderCode
        );

    const VkShaderModule fragmentShaderModule =
        CreateShaderModule(
            fragmentShaderCode
        );

    try
    {
        // =====================================================
        // Shader stages
        // =====================================================

        VkPipelineShaderStageCreateInfo
            vertexShaderStageInfo{};

        vertexShaderStageInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

        vertexShaderStageInfo.stage =
            VK_SHADER_STAGE_VERTEX_BIT;

        vertexShaderStageInfo.module =
            vertexShaderModule;

        vertexShaderStageInfo.pName =
            "main";

        VkPipelineShaderStageCreateInfo
            fragmentShaderStageInfo{};

        fragmentShaderStageInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

        fragmentShaderStageInfo.stage =
            VK_SHADER_STAGE_FRAGMENT_BIT;

        fragmentShaderStageInfo.module =
            fragmentShaderModule;

        fragmentShaderStageInfo.pName =
            "main";

        const VkPipelineShaderStageCreateInfo
            shaderStages[] =
        {
            vertexShaderStageInfo,
            fragmentShaderStageInfo
        };

        // =====================================================
        // Vertex input
        //
        // No vertex buffer is required.
        // The vertex shader generates a full-screen triangle
        // using gl_VertexIndex.
        // =====================================================

        VkPipelineVertexInputStateCreateInfo
            vertexInputInfo{};

        vertexInputInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        vertexInputInfo.vertexBindingDescriptionCount =
            0;

        vertexInputInfo.pVertexBindingDescriptions =
            nullptr;

        vertexInputInfo.vertexAttributeDescriptionCount =
            0;

        vertexInputInfo.pVertexAttributeDescriptions =
            nullptr;

        // =====================================================
        // Input assembly
        // =====================================================

        VkPipelineInputAssemblyStateCreateInfo
            inputAssembly{};

        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

        inputAssembly.topology =
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        inputAssembly.primitiveRestartEnable =
            VK_FALSE;

        // =====================================================
        // Viewport
        // =====================================================

        VkViewport viewport{};

        viewport.x =
            0.0f;

        viewport.y =
            0.0f;

        viewport.width =
            static_cast<float>(
                lutSize
            );

        viewport.height =
            static_cast<float>(
                lutSize
            );

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
            lutSize,
            lutSize
        };

        VkPipelineViewportStateCreateInfo
            viewportState{};

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

        // =====================================================
        // Rasterization
        // =====================================================

        VkPipelineRasterizationStateCreateInfo
            rasterizer{};

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

        rasterizer.cullMode =
            VK_CULL_MODE_NONE;

        rasterizer.frontFace =
            VK_FRONT_FACE_COUNTER_CLOCKWISE;

        rasterizer.depthBiasEnable =
            VK_FALSE;

        rasterizer.depthBiasConstantFactor =
            0.0f;

        rasterizer.depthBiasClamp =
            0.0f;

        rasterizer.depthBiasSlopeFactor =
            0.0f;

        // =====================================================
        // Multisampling
        // =====================================================

        VkPipelineMultisampleStateCreateInfo
            multisampling{};

        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

        multisampling.sampleShadingEnable =
            VK_FALSE;

        multisampling.rasterizationSamples =
            VK_SAMPLE_COUNT_1_BIT;

        multisampling.minSampleShading =
            1.0f;

        multisampling.pSampleMask =
            nullptr;

        multisampling.alphaToCoverageEnable =
            VK_FALSE;

        multisampling.alphaToOneEnable =
            VK_FALSE;

        // =====================================================
        // Depth and stencil
        //
        // BRDF LUT generation is a 2D full-screen pass, so depth
        // testing and depth writing are disabled.
        // =====================================================

        VkPipelineDepthStencilStateCreateInfo
            depthStencil{};

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

        depthStencil.minDepthBounds =
            0.0f;

        depthStencil.maxDepthBounds =
            1.0f;

        // =====================================================
        // Color blending
        // =====================================================

        VkPipelineColorBlendAttachmentState
            colorBlendAttachment{};

        colorBlendAttachment.blendEnable =
            VK_FALSE;

        colorBlendAttachment.srcColorBlendFactor =
            VK_BLEND_FACTOR_ONE;

        colorBlendAttachment.dstColorBlendFactor =
            VK_BLEND_FACTOR_ZERO;

        colorBlendAttachment.colorBlendOp =
            VK_BLEND_OP_ADD;

        colorBlendAttachment.srcAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE;

        colorBlendAttachment.dstAlphaBlendFactor =
            VK_BLEND_FACTOR_ZERO;

        colorBlendAttachment.alphaBlendOp =
            VK_BLEND_OP_ADD;

        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo
            colorBlending{};

        colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

        colorBlending.logicOpEnable =
            VK_FALSE;

        colorBlending.logicOp =
            VK_LOGIC_OP_COPY;

        colorBlending.attachmentCount =
            1;

        colorBlending.pAttachments =
            &colorBlendAttachment;

        colorBlending.blendConstants[0] =
            0.0f;

        colorBlending.blendConstants[1] =
            0.0f;

        colorBlending.blendConstants[2] =
            0.0f;

        colorBlending.blendConstants[3] =
            0.0f;

        // =====================================================
        // Pipeline layout
        //
        // BRDF LUT generation does not use descriptor sets or
        // push constants.
        // =====================================================

        VkPipelineLayoutCreateInfo
            pipelineLayoutInfo{};

        pipelineLayoutInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        pipelineLayoutInfo.setLayoutCount =
            0;

        pipelineLayoutInfo.pSetLayouts =
            nullptr;

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
            throw std::runtime_error(
                "Failed to create BRDF LUT pipeline layout."
            );
        }

        // =====================================================
        // Graphics pipeline
        // =====================================================

        VkGraphicsPipelineCreateInfo
            pipelineInfo{};

        pipelineInfo.sType =
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

        pipelineInfo.stageCount =
            2;

        pipelineInfo.pStages =
            shaderStages;

        pipelineInfo.pVertexInputState =
            &vertexInputInfo;

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
            renderPass.GetHandle();

        pipelineInfo.subpass =
            0;

        pipelineInfo.basePipelineHandle =
            VK_NULL_HANDLE;

        pipelineInfo.basePipelineIndex =
            -1;

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
                "Failed to create BRDF LUT graphics pipeline."
            );
        }
    }
    catch (...)
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

// =========================================================
// Read SPIR-V shader file
// =========================================================

std::vector<char> BRDFLUTPipeline::ReadFile(
    const std::string& filename
)
{
    std::ifstream file(
        filename,
        std::ios::ate |
        std::ios::binary
    );

    if (!file.is_open())
    {
        throw std::runtime_error(
            "Failed to open BRDF LUT shader file: " +
            filename
        );
    }

    const std::streamsize fileSize =
        file.tellg();

    if (fileSize <= 0)
    {
        throw std::runtime_error(
            "BRDF LUT shader file is empty: " +
            filename
        );
    }

    std::vector<char> buffer(
        static_cast<size_t>(
            fileSize
        )
    );

    file.seekg(
        0
    );

    file.read(
        buffer.data(),
        fileSize
    );

    if (!file)
    {
        throw std::runtime_error(
            "Failed to read BRDF LUT shader file: " +
            filename
        );
    }

    file.close();

    return buffer;
}

// =========================================================
// Create shader module
// =========================================================

VkShaderModule BRDFLUTPipeline::CreateShaderModule(
    const std::vector<char>& code
) const
{
    if (code.empty())
    {
        throw std::runtime_error(
            "Cannot create BRDF LUT shader module from empty code."
        );
    }

    if ((code.size() % sizeof(uint32_t)) != 0)
    {
        throw std::runtime_error(
            "BRDF LUT SPIR-V shader size is not aligned to 4 bytes."
        );
    }

    VkShaderModuleCreateInfo
        createInfo{};

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
            "Failed to create BRDF LUT shader module."
        );
    }

    return shaderModule;
}