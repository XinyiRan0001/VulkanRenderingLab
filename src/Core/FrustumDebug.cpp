#include "FrustumDebug.h"
#include "ShaderUtils.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace
{
void Check(VkResult r)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error("Cannot create frustum debug pipeline");
}
VkShaderModule Load(VkDevice device, const char *path)
{
    auto bytes = ReadFile(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes.size();
    info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
    VkShaderModule module;
    Check(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}
} // namespace
FrustumDebug::FrustumDebug(const PhysicalDevice &physical, const LogicalDevice &device, VkRenderPass pass,
                           VkExtent2D extent)
    : m_Device(device)
{
    m_Vertices =
        std::make_unique<Buffer>(physical, device, 48 * sizeof(glm::vec4), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const auto d = device.GetHandle();
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    Check(vkCreatePipelineLayout(d, &layout, nullptr, &m_Layout));
    const auto vert = Load(d, "build/shaders/frustum_debug.vert.spv");
    const auto frag = Load(d, "build/shaders/frustum_debug.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &s : stages)
    {
        s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    VkVertexInputBindingDescription binding{0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 1;
    input.pVertexAttributeDescriptions = &attribute;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    view.viewportCount = 1;
    view.pViewports = &viewport;
    view.scissorCount = 1;
    view.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // Overlay deliberately remains visible through the wall; it never writes scene depth.
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &view;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.layout = m_Layout;
    info.renderPass = pass;
    const auto result = vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &info, nullptr, &m_Pipeline);
    vkDestroyShaderModule(d, vert, nullptr);
    vkDestroyShaderModule(d, frag, nullptr);
    Check(result);
}
FrustumDebug::~FrustumDebug()
{
    vkDestroyPipeline(m_Device.GetHandle(), m_Pipeline, nullptr);
    vkDestroyPipelineLayout(m_Device.GetHandle(), m_Layout, nullptr);
}
void FrustumDebug::Update(const UniformBufferObject &observer, const UniformBufferObject &culling,
                          bool enabled, glm::vec4 sphere, bool inspect)
{
    std::array<glm::vec4, 48> lines;
    // Clip-space points outside the viewport make the fixed draw invisible when disabled.
    lines.fill(glm::vec4(2, 2, 0, 1));
    if (enabled)
    {
        const auto inverse = glm::inverse(culling.proj * culling.view);
        const auto clip = observer.proj * observer.view;
        std::array<glm::vec4, 8> corners;
        for (int i = 0; i < 8; ++i)
        {
            auto p = inverse * glm::vec4((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : 0.f, 1);
            p /= p.w;
            // Cap only the visualization at 20 world units; culling still uses the full far plane.
            const auto offset = glm::vec3(p) - glm::vec3(culling.cameraPosition);
            const float length = glm::length(offset);
            if (length > 20.f)
                p = glm::vec4(glm::vec3(culling.cameraPosition) + offset * (20.f / length), 1);
            corners[i] = clip * p;
        }
        int vertex = 0;
        for (int i = 0; i < 8; ++i)
            for (int bit = 1; bit <= 4; bit *= 2)
                if (!(i & bit))
                {
                    lines[vertex++] = corners[i];
                    lines[vertex++] = corners[i | bit];
                }
    }
    if (inspect)
    {
        const auto clip = observer.proj * observer.view * observer.model;
        std::array<glm::vec4, 8> corners;
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 sign((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : -1.f);
            corners[i] = clip * glm::vec4(glm::vec3(sphere) + sign * sphere.w, 1);
        }
        int vertex = 24;
        for (int i = 0; i < 8; ++i)
            for (int bit = 1; bit <= 4; bit *= 2)
                if (!(i & bit))
                {
                    lines[vertex++] = corners[i];
                    lines[vertex++] = corners[i | bit];
                }
    }
    m_Vertices->UploadData(lines.data(), sizeof(lines));
}
void FrustumDebug::Record(VkCommandBuffer cmd) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    VkBuffer buffer = m_Vertices->GetHandle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
    vkCmdDraw(cmd, 48, 1, 0, 0);
}
