#include "HiZInspector.h"
#include "ShaderUtils.h"
#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace
{
void Check(VkResult r)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error("Cannot create Hi-Z inspector");
}
VkShaderModule Load(VkDevice d, const char *path)
{
    auto bytes = ReadFile(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes.size();
    info.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
    VkShaderModule module;
    Check(vkCreateShaderModule(d, &info, nullptr, &module));
    return module;
}
} // namespace
HiZInspector::HiZInspector(const PhysicalDevice &physical, const LogicalDevice &device, VkRenderPass pass,
                           VkExtent2D extent, HiZ &hiZ, GpuCulling &culling, const MeshData &mesh)
    : m_Device(device), m_HiZ(hiZ), m_Culling(culling)
{
    for (const auto &sub : mesh.subMeshes)
        if (sub.firstInstance > 0)
        {
            m_First = sub.firstInstance;
            for (uint32_t i = 0; i < sub.instanceCount; ++i)
                m_Spheres.push_back(TransformSphere(mesh.instances[m_First + i].model, sub.localSphere));
            break;
        }
    if (m_Spheres.empty())
        throw std::runtime_error("Inspector needs an instanced mesh");
    m_Selection =
        int(m_Spheres.size() / 2 + size_t(std::sqrt(double(m_Spheres.size()))) / 2) % int(m_Spheres.size());
    m_Current.view = glm::ivec4(0, 0, extent.width, extent.height);
    const auto d = device.GetHandle();
    try
    {
        const auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        m_Uniform = std::make_unique<Buffer>(physical, device, sizeof(Settings),
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host);
        m_Output = std::make_unique<Buffer>(
            physical, device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_Readback = std::make_unique<Buffer>(physical, device, 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host);
        VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
             VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = 4;
        setLayout.pBindings = bindings;
        Check(vkCreateDescriptorSetLayout(d, &setLayout, nullptr, &m_SetLayout));
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 3;
        pool.pPoolSizes = sizes;
        Check(vkCreateDescriptorPool(d, &pool, nullptr, &m_Pool));
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_Pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_SetLayout;
        Check(vkAllocateDescriptorSets(d, &alloc, &m_Set));
        VkDescriptorImageInfo images[] = {hiZ.SampledPyramid(), hiZ.SampledDepth()};
        VkDescriptorBufferInfo buffers[] = {{m_Uniform->GetHandle(), 0, sizeof(Settings)},
                                            {m_Output->GetHandle(), 0, 16}};
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (uint32_t i = 0; i < 4; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bindings[i].descriptorType;
            if (i < 2)
                writes[i].pImageInfo = &images[i];
            else
                writes[i].pBufferInfo = &buffers[i - 2];
        }
        vkUpdateDescriptorSets(d, 4, writes.data(), 0, nullptr);
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_SetLayout;
        Check(vkCreatePipelineLayout(d, &layout, nullptr, &m_Layout));
        const auto compute = Load(d, "build/shaders/hiz_inspect.comp.spv");
        VkComputePipelineCreateInfo query{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        query.layout = m_Layout;
        query.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                       nullptr,
                       0,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       compute,
                       "main",
                       nullptr};
        const auto status = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &query, nullptr, &m_Query);
        vkDestroyShaderModule(d, compute, nullptr);
        Check(status);
        const auto vert = Load(d, "build/shaders/hiz_inspect.vert.spv");
        const auto frag = Load(d, "build/shaders/hiz_inspect.frag.spv");
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
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        view.viewportCount = 1;
        view.pViewports = &viewport;
        view.scissorCount = 1;
        view.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        // Overlay deliberately remains visible through the wall; it never writes scene depth.
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
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
        const auto result = vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &info, nullptr, &m_Panel);
        vkDestroyShaderModule(d, vert, nullptr);
        vkDestroyShaderModule(d, frag, nullptr);
        Check(result);
    }
    catch (...)
    {
        Release();
        throw;
    }
}
HiZInspector::~HiZInspector()
{
    Release();
}
void HiZInspector::Release()
{
    const auto d = m_Device.GetHandle();
    if (m_Panel)
        vkDestroyPipeline(d, m_Panel, nullptr);
    if (m_Query)
        vkDestroyPipeline(d, m_Query, nullptr);
    if (m_Layout)
        vkDestroyPipelineLayout(d, m_Layout, nullptr);
    if (m_Pool)
        vkDestroyDescriptorPool(d, m_Pool, nullptr);
    if (m_SetLayout)
        vkDestroyDescriptorSetLayout(d, m_SetLayout, nullptr);
}
void HiZInspector::SetMip(int mip)
{
    if (mip < -1 || mip >= int(m_HiZ.Levels()))
        throw std::runtime_error("Inspector mip must be -1 (raw depth) or a valid pyramid level");
    m_Mip = mip;
}
void HiZInspector::StepMip(int delta)
{
    const int count = int(m_HiZ.Levels()) + 1;
    m_Mip = (m_Mip + 1 + delta + count) % count - 1;
}
void HiZInspector::Select(int offset)
{
    if (offset < 0 || offset >= int(m_Spheres.size()))
        throw std::runtime_error("Inspector instance index out of range");
    m_Selection = offset;
}
void HiZInspector::StepSelection(int delta)
{
    m_Selection = (m_Selection + delta + int(m_Spheres.size())) % int(m_Spheres.size());
}
void HiZInspector::Update(const UniformBufferObject &culling, bool frustum, bool occlusion, bool verify)
{
    ReadCompleted();
    if (!m_Enabled)
        return;
    m_Verify = verify;
    const auto clip = culling.proj * culling.view * culling.model;
    m_Footprint =
        ProjectHiZSphere(SelectedSphere(), clip, m_Current.view.z, m_Current.view.w, m_HiZ.Levels());
    m_Current.region = glm::ivec4(m_Footprint.minimum, m_Footprint.maximum);
    const bool rejected = frustum && !SphereVisible(ExtractFrustum(clip), SelectedSphere());
    m_Current.query =
        glm::ivec4(m_Footprint.mip, m_Footprint.valid ? 1 : 0, rejected ? 1 : 0, occlusion ? 1 : 0);
    m_Current.depths = glm::vec4(m_Footprint.valid ? m_Footprint.nearest : 1.f, culling.proj[2][2],
                                 culling.proj[3][2], 1e-4f);
    m_Current.pixels = glm::vec4(glm::vec2(m_Footprint.pixelMinimum), glm::vec2(m_Footprint.pixelMaximum));
    m_Current.selection =
        glm::ivec4(m_Selection, m_First + uint32_t(m_Selection), int(m_Footprint.bypass), 0);
    m_Current.view.x = m_Enabled ? 1 : 0;
    m_Current.view.y = m_Mip;
    m_Uniform->UploadData(&m_Current, sizeof(m_Current));
}
void HiZInspector::MarkSubmitted()
{
    if (!m_Enabled)
        return;
    m_Pending = true;
    m_Submitted = m_Current;
    m_SubmittedFootprint = m_Footprint;
    m_SubmittedId = m_First + uint32_t(m_Selection);
}
void HiZInspector::ReadCompleted()
{
    if (!m_Pending)
        return;
    m_Pending = false;
    m_Readback->DownloadData(&m_Result, sizeof(m_Result));
    if (!std::isfinite(m_Result.x) || !std::isfinite(m_Result.y) || m_Result.x < 0 || m_Result.x > 1 ||
        m_Result.y < 0 || m_Result.y > 1 || (m_Result.z != 0.f && m_Result.z != 1.f) ||
        !std::isfinite(m_Result.w) || m_Result.w < 0 || m_Result.w > 3 ||
        std::floor(m_Result.w) != m_Result.w)
        throw std::runtime_error("Invalid Hi-Z inspector result");
    m_ResultSettings = m_Submitted;
    m_ResultId = m_SubmittedId;
    m_HaveResult = true;
    if (m_Verify)
    {
        const bool valid = m_Submitted.query.y != 0;
        const float farthest = valid ? m_HiZ.FarthestCPU(m_SubmittedFootprint) : 1.f;
        const bool covered = valid && m_Submitted.depths.x > farthest + 1e-4f;
        if (std::abs(m_Result.x - m_Submitted.depths.x) > 1e-6f || std::abs(m_Result.y - farthest) > 1e-6f ||
            (m_Result.z != 0) != covered)
            throw std::runtime_error("Inspector GPU query differs from CPU pyramid reference");
        const bool drawn = m_Result.w != 0.f && m_Result.w != 3.f;
        if (m_Culling.VerifiedVisible(m_ResultId) != drawn)
            throw std::runtime_error("Inspector decision differs from actual GPU visible IDs");
        std::cout << "[HiZ Inspector Verify] " << Status() << std::endl;
    }
}
std::string HiZInspector::Status() const
{
    std::ostringstream text;
    text << (m_Mip < 0 ? "Raw depth" : "Preview mip " + std::to_string(m_Mip));
    if (!m_HaveResult)
        return text.str() + " | awaiting query";
    static const char *states[] = {"FRUSTUM REJECT", "BYPASS", "VISIBLE", "OCCLUDED"};
    text << " | Sphere " << m_ResultSettings.selection.x << " (ID " << m_ResultId << ")" << " "
         << states[int(m_Result.w)];
    if (m_ResultSettings.query.y)
    {
        text << " | query mip " << m_ResultSettings.query.x << std::fixed << std::setprecision(6)
             << " | near " << m_Result.x << " max " << m_Result.y;
    }
    else
    {
        text << " | "
             << (m_ResultSettings.selection.z == 1 ? "near/eye-plane bypass" : "offscreen query bypass")
             << " | depths N/A";
    }
    if (!m_ResultSettings.query.w)
        text << " | HiZ OFF (test " << (m_Result.z != 0 ? "covered" : "clear") << ")";
    return text.str();
}
void HiZInspector::RecordQuery(VkCommandBuffer cmd) const
{
    VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reuse.srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    reuse.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &reuse, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Query);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Layout, 0, 1, &m_Set, 0, nullptr);
    vkCmdDispatch(cmd, 1, 1, 1);
    VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &ready,
                         0, nullptr, 0, nullptr);
    VkBufferCopy copy{0, 0, 16};
    vkCmdCopyBuffer(cmd, m_Output->GetHandle(), m_Readback->GetHandle(), 1, &copy);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                         nullptr, 0, nullptr);
}
void HiZInspector::RecordPanel(VkCommandBuffer cmd) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Panel);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout, 0, 1, &m_Set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
