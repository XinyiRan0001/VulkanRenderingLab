#include "GpuCulling.h"
#include "ShaderUtils.h"
#include <algorithm>
#include <cstring>
#include <iostream>

static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20, "Indirect GPU command stride must be 20 bytes");
namespace
{
void CheckVk(VkResult result, const char *message)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(message);
}
} // namespace

GpuCulling::GpuCulling(const PhysicalDevice &physical, const LogicalDevice &device, const Buffer &input,
                       const MeshData &mesh, bool verifyReadback, HiZ *hiZ, VkDescriptorImageInfo fallback)
    : m_Device(device), m_Instances(verifyReadback ? mesh.instances : std::vector<InstanceData>{})
{
    for (const auto &sub : mesh.subMeshes)
        if (sub.firstInstance > 0)
        {
            if (!m_Count)
            {
                m_First = sub.firstInstance;
                m_Count = sub.instanceCount;
                m_LocalSphere = sub.localSphere;
            }
            if (sub.firstInstance != m_First || sub.instanceCount != m_Count ||
                sub.localSphere != m_LocalSphere)
                throw std::runtime_error("GPU culling currently supports one shared instance group");
            // firstInstance=0: compact list slots are remapped to actual IDs in the vertex shader.
            m_Templates.push_back({sub.indexCount, 0, sub.firstIndex, 0, 0});
        }
    if (!m_Count || size_t(m_First) + m_Count > mesh.instances.size() || m_Templates.size() * 20 > 65536)
        throw std::runtime_error("Invalid GPU culling group");
    m_HiZ = hiZ;
    m_Fallback = fallback;
    m_VisibleCount = m_Count;
    m_CaptureVisible = verifyReadback;
    try
    {
        const auto gpu = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        const auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        m_Visible = std::make_unique<Buffer>(
            physical, device, m_Count * 4,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, gpu);
        m_Indirect = std::make_unique<Buffer>(
            physical, device, m_Templates.size() * 20,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            gpu);
        m_Uniform = std::make_unique<Buffer>(physical, device, sizeof(CullUniform),
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host);
        m_Stats =
            std::make_unique<Buffer>(physical, device, 16,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     gpu);
        m_Readback = std::make_unique<Buffer>(
            physical, device, m_Indirect->GetSize() + 16 + (m_CaptureVisible ? m_Visible->GetSize() : 0),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, host);
        const std::array<VkDescriptorSetLayoutBinding, 6> bindings{
            {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
             {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
             {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
             {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
             {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
             {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 6;
        layout.pBindings = bindings.data();
        CheckVk(vkCreateDescriptorSetLayout(device.GetHandle(), &layout, nullptr, &m_SetLayout),
                "Cannot create cull descriptor layout");
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
                                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 3;
        pool.pPoolSizes = sizes;
        CheckVk(vkCreateDescriptorPool(device.GetHandle(), &pool, nullptr, &m_Pool),
                "Cannot create cull descriptor pool");
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_Pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_SetLayout;
        CheckVk(vkAllocateDescriptorSets(device.GetHandle(), &alloc, &m_Set),
                "Cannot allocate cull descriptor set");
        const VkDescriptorBufferInfo infos[] = {{input.GetHandle(), 0, input.GetSize()},
                                                {m_Visible->GetHandle(), 0, m_Visible->GetSize()},
                                                {m_Indirect->GetHandle(), 0, m_Indirect->GetSize()},
                                                {m_Uniform->GetHandle(), 0, sizeof(CullUniform)},
                                                {m_Stats->GetHandle(), 0, 16}};
        const auto pyramid = m_HiZ ? m_HiZ->SampledPyramid() : fallback;
        if (!pyramid.imageView || !pyramid.sampler)
            throw std::runtime_error("Culling requires a valid fallback sampled texture");
        std::array<VkWriteDescriptorSet, 6> writes{};
        for (uint32_t i = 0; i < 6; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bindings[i].descriptorType;
            if (i == 4)
                writes[i].pImageInfo = &pyramid;
            else
                writes[i].pBufferInfo = &infos[i == 5 ? 4 : i];
        }
        vkUpdateDescriptorSets(device.GetHandle(), 6, writes.data(), 0, nullptr);
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &m_SetLayout;
        CheckVk(vkCreatePipelineLayout(device.GetHandle(), &pipelineLayout, nullptr, &m_Layout),
                "Cannot create cull pipeline layout");
        const auto code = ReadFile("build/shaders/frustum_cull.comp.spv");
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
        VkShaderModule module;
        CheckVk(vkCreateShaderModule(device.GetHandle(), &moduleInfo, nullptr, &module),
                "Cannot create cull shader");
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.layout = m_Layout;
        pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline.stage.module = module;
        pipeline.stage.pName = "main";
        const auto result =
            vkCreateComputePipelines(device.GetHandle(), VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_Pipeline);
        vkDestroyShaderModule(device.GetHandle(), module, nullptr);
        CheckVk(result, "Cannot create compute culling pipeline");
    }
    catch (...)
    {
        Release();
        throw;
    }
}
GpuCulling::~GpuCulling()
{
    Release();
}
void GpuCulling::Release()
{
    const auto device = m_Device.GetHandle();
    if (m_Pipeline)
        vkDestroyPipeline(device, m_Pipeline, nullptr);
    if (m_Layout)
        vkDestroyPipelineLayout(device, m_Layout, nullptr);
    if (m_Pool)
        vkDestroyDescriptorPool(device, m_Pool, nullptr);
    if (m_SetLayout)
        vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
}
void GpuCulling::Update(const UniformBufferObject &ubo, bool enabled, bool verify, bool occlusion)
{
    ReadCompleted(); // results belong to the previous frame, before its camera is replaced
    CullUniform uniform{
        ExtractFrustum(ubo.proj * ubo.view * ubo.model), m_LocalSphere,
        glm::uvec4(m_First, m_Count, static_cast<uint32_t>(m_Templates.size()), enabled ? 1u : 0u),
        ubo.proj * ubo.view * ubo.model, m_HiZ ? m_HiZ->Settings(occlusion) : glm::uvec4(1, 1, 1, 0)};
    if (verify && !m_CaptureVisible)
        throw std::runtime_error("Visible-ID verification must be enabled at startup");
    m_Verify = verify;
    m_Expected.clear();
    if (verify)
        for (uint32_t i = m_First; i < m_First + m_Count; ++i)
            if (!enabled ||
                SphereVisible(uniform.planes, TransformSphere(m_Instances[i].model, m_LocalSphere)))
                m_Expected.push_back(i);
    m_LastUniform = uniform;
    m_Uniform->UploadData(&uniform, sizeof(uniform));
}
void GpuCulling::Record(VkCommandBuffer cmd) const
{
    VkMemoryBarrier reuse{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reuse.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                          VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                          VK_ACCESS_SHADER_WRITE_BIT;
    reuse.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &reuse,
                         0, nullptr, 0, nullptr);
    vkCmdUpdateBuffer(cmd, m_Indirect->GetHandle(), 0, m_Indirect->GetSize(), m_Templates.data());
    vkCmdFillBuffer(cmd, m_Stats->GetHandle(), 0, 16, 0);
    VkMemoryBarrier reset{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    reset.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    reset.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &reset, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Layout, 0, 1, &m_Set, 0, nullptr);
    vkCmdDispatch(cmd, (m_Count + 63) / 64, 1, 1);
    VkMemoryBarrier ready{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ready.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &ready, 0, nullptr, 0, nullptr);
    VkBufferCopy copy{0, 0, m_Indirect->GetSize()};
    vkCmdCopyBuffer(cmd, m_Indirect->GetHandle(), m_Readback->GetHandle(), 1, &copy);
    VkBufferCopy statistics{0, m_Indirect->GetSize(), 16};
    vkCmdCopyBuffer(cmd, m_Stats->GetHandle(), m_Readback->GetHandle(), 1, &statistics);
    // Normal telemetry copies only the small command array. Full ID readback is
    // reserved for the explicitly enabled verification path.
    if (m_CaptureVisible)
    {
        VkBufferCopy ids{0, m_Indirect->GetSize() + 16, m_Visible->GetSize()};
        vkCmdCopyBuffer(cmd, m_Visible->GetHandle(), m_Readback->GetHandle(), 1, &ids);
    }
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                         nullptr, 0, nullptr);
}
void GpuCulling::ReadCompleted()
{
    if (!m_Pending)
        return;
    m_Pending = false;
    std::vector<unsigned char> bytes(static_cast<size_t>(m_Readback->GetSize()));
    m_Readback->DownloadData(bytes.data(), bytes.size());
    std::vector<VkDrawIndexedIndirectCommand> commands(m_Templates.size());
    std::memcpy(commands.data(), bytes.data(), m_Indirect->GetSize());
    m_VisibleCount = commands[0].instanceCount;
    if (m_VisibleCount > m_Count)
        throw std::runtime_error("GPU visibility count exceeds capacity");
    for (size_t i = 0; i < commands.size(); ++i)
    {
        auto expected = m_Templates[i];
        expected.instanceCount = m_VisibleCount;
        if (std::memcmp(&expected, &commands[i], sizeof(expected)) != 0)
            throw std::runtime_error("GPU indirect command corruption/count mismatch");
    }
    uint32_t stats[4]{};
    std::memcpy(stats, bytes.data() + m_Indirect->GetSize(), 16);
    m_FrustumCount = stats[0];
    m_OccludedCount = stats[1];
    if (m_FrustumCount > m_Count || m_OccludedCount > m_FrustumCount ||
        m_VisibleCount + m_OccludedCount != m_FrustumCount)
        throw std::runtime_error("GPU frustum/Hi-Z statistics mismatch");
    if (m_Verify)
    {
        if (m_Expected.size() != m_FrustumCount)
            throw std::runtime_error("GPU frustum count disagrees with CPU reference");
        if (m_HiZ)
        {
            m_HiZ->ValidatePyramid();
            if (m_LastUniform.hiZ.w != 0)
            {
                std::vector<uint32_t> retained;
                for (auto id : m_Expected)
                    if (!m_HiZ->OccludedCPU(TransformSphere(m_Instances[id].model, m_LocalSphere),
                                            m_LastUniform.clipFromScene))
                        retained.push_back(id);
                m_Expected = std::move(retained);
            }
        }
        std::vector<uint32_t> ids(m_VisibleCount);
        std::memcpy(ids.data(), bytes.data() + m_Indirect->GetSize() + 16, ids.size() * 4);
        std::sort(ids.begin(), ids.end());
        m_VerifiedIds = ids;
        if (ids != m_Expected)
            throw std::runtime_error("GPU visible IDs disagree with CPU frustum/Hi-Z reference");
        std::cout << "[Culling Verify] exact visible-ID match: " << m_VisibleCount << " / " << m_Count
                  << std::endl;
    }
}

void GpuCulling::SetHiZSampling(bool enabled) {
    const auto image = enabled && m_HiZ ? m_HiZ->SampledPyramid() : m_Fallback;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=m_Set; write.dstBinding=4; write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo=&image;
    vkUpdateDescriptorSets(m_Device.GetHandle(),1,&write,0,nullptr);
}

bool GpuCulling::VerifiedVisible(uint32_t id) const {
    if (!m_Verify) throw std::runtime_error("Exact instance visibility requires --verify-culling");
    return std::binary_search(m_VerifiedIds.begin(),m_VerifiedIds.end(),id);
}
