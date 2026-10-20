#include "AnimationPanel.h"
#include "ShaderUtils.h"
#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <stdexcept>

namespace
{
void Check(VkResult r)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error("Animation panel Vulkan operation failed");
}
// Original tiny 5x7 pixel glyphs; no external font or UI dependency.
const std::map<char, std::array<int, 7>> glyphs = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 14}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},      {':', {0, 12, 12, 0, 12, 12, 0}},
    {'/', {1, 2, 2, 4, 8, 8, 16}},       {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},       {'<', {2, 4, 8, 16, 8, 4, 2}},
    {'>', {8, 4, 2, 1, 2, 4, 8}}};
constexpr size_t MaxVertices = 60000;
} // namespace
AnimationPanel::AnimationPanel(const PhysicalDevice &physical, const LogicalDevice &device, VkRenderPass pass,
                               VkExtent2D extent)
    : m_Device(device)
{
    const auto d = device.GetHandle();
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    try
    {
        const auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        m_Vertices = std::make_unique<Buffer>(physical, device, MaxVertices * sizeof(Vertex),
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host);
        m_Draw = std::make_unique<Buffer>(physical, device, sizeof(VkDrawIndirectCommand),
                                          VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, host);
        VkDrawIndirectCommand empty{0, 1, 0, 0};
        m_Draw->UploadData(&empty, sizeof(empty));
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        Check(vkCreatePipelineLayout(d, &layout, nullptr, &m_Layout));
        auto shader = [&](const char *path) {
            auto bytes = ReadFile(path);
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = bytes.size();
            info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
            VkShaderModule module;
            Check(vkCreateShaderModule(d, &info, nullptr, &module));
            return module;
        };
        vert = shader("build/shaders/animation_panel.vert.spv");
        frag = shader("build/shaders/animation_panel.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
             vert, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
             frag, "main", nullptr}};
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[2] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = 2;
        input.pVertexAttributeDescriptions = attributes;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        view.viewportCount = view.scissorCount = 1;
        view.pViewports = &viewport;
        view.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &view;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.layout = m_Layout;
        pipeline.renderPass = pass;
        Check(vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_Pipeline));
        vkDestroyShaderModule(d, vert, nullptr);
        vkDestroyShaderModule(d, frag, nullptr);
    }
    catch (...)
    {
        if (vert)
            vkDestroyShaderModule(d, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(d, frag, nullptr);
        Release();
        throw;
    }
}
AnimationPanel::~AnimationPanel()
{
    Release();
}
void AnimationPanel::Release()
{
    auto d = m_Device.GetHandle();
    if (m_Pipeline)
        vkDestroyPipeline(d, m_Pipeline, nullptr);
    if (m_Layout)
        vkDestroyPipelineLayout(d, m_Layout, nullptr);
}
float AnimationPanel::Scale(int width, int height)
{
    return std::max(.1f, std::min({1.f, (width - 24) / 324.f, (height - 24) / 332.f}));
}
void AnimationPanel::Rect(float x, float y, float w, float h, glm::vec4 color)
{
    const auto point = [&](float a, float b) {
        return glm::vec2((12 + a * m_Scale) / m_Width * 2 - 1, (12 + b * m_Scale) / m_Height * 2 - 1);
    };
    for (auto p : {point(x, y), point(x + w, y), point(x, y + h), point(x, y + h), point(x + w, y),
                   point(x + w, y + h)})
        m_Data.push_back({p, color});
}
void AnimationPanel::Text(float x, float y, const std::string &text, glm::vec4 color)
{
    for (unsigned char c : text)
    {
        auto it = glyphs.find(char(std::toupper(c)));
        if (it != glyphs.end())
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (it->second[row] & (1 << (4 - col)))
                        Rect(x + col * 2, y + row * 2, 2, 2, color);
        x += 12;
    }
}
void AnimationPanel::Update(const AnimationControls &c, int width, int height)
{
    m_Data.clear();
    m_Width = float(std::max(1, width));
    m_Height = float(std::max(1, height));
    m_Scale = Scale(width, height);
    if (c.visible)
    {
        const glm::vec4 white(.8f, .86f, .95f, 1), muted(.3f, .4f, .52f, 1), active(.03f, .18f, .26f, 1),
            inactive(.04f, .055f, .075f, 1);
        Rect(0, 0, 324, 332, {.012f, .02f, .033f, 1});
        Text(12, 12, "ANIMATION", white);
        const std::string name = c.clips.empty() ? "NONE" : c.clips[c.clip].name.substr(0, 18);
        Text(50, 46, name, white);
        for (const auto &b : AnimationControls::buttons)
        {
            bool selected = (b.action == AnimationControls::Play && c.playing) ||
                            (b.action == AnimationControls::Loop && c.loop) ||
                            (b.action == AnimationControls::Linear && !c.dual) ||
                            (b.action == AnimationControls::Dual && c.dual) ||
                            (b.action == AnimationControls::Surface && c.surface) ||
                            (b.action == AnimationControls::Authored && !c.surface);
            Rect(b.x, b.y, b.w, b.h, selected ? active : inactive);
            Text(b.x + 6, b.y + 6,
                 b.action == AnimationControls::Play ? (c.playing ? "PAUSE" : "PLAY") : b.label, white);
        }
        char time[64];
        std::snprintf(time, sizeof(time), "%.2F / %.2F S", c.time, c.Duration());
        Text(12, 116, time, white);
        Rect(12, 140, 300, 12, inactive);
        float f = c.Duration() > 0 ? float(c.time / c.Duration()) : 0;
        Rect(12, 140, 300 * f, 12, active);
        Rect(10 + 300 * f, 136, 4, 20, white);
        char speed[32];
        std::snprintf(speed, sizeof(speed), "SPEED %.2F X", c.speed);
        Text(72, 180, speed, white);
        Text(12, 208, "SKINNING", muted);
        Text(12, 258, "NORMALS", muted);
        Text(12, 312, "ENTER PLAY  F12 PANEL", muted);
    }
    if (m_Data.size() > MaxVertices)
        throw std::runtime_error("Animation panel vertex capacity exceeded");
    if (!m_Data.empty())
        m_Vertices->UploadData(m_Data.data(), m_Data.size() * sizeof(Vertex));
    VkDrawIndirectCommand draw{uint32_t(m_Data.size()), 1, 0, 0};
    m_Draw->UploadData(&draw, sizeof(draw));
}
void AnimationPanel::Record(VkCommandBuffer cmd) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    VkBuffer buffer = m_Vertices->GetHandle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
    vkCmdDrawIndirect(cmd, m_Draw->GetHandle(), 0, 1, sizeof(VkDrawIndirectCommand));
}
