#include "GpuSkinning.h"
#include "MeshWinding.h"
#include "MotionMath.h"
#include "ShaderUtils.h"
#include "SkinningMath.h"
#include "SurfaceNormals.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <numeric>
#include <stdexcept>
namespace
{
void Check(VkResult r)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error("GPU skinning Vulkan operation failed");
}
glm::vec3 Unit(glm::vec3 n)
{
    return glm::dot(n, n) > 1e-12f ? glm::normalize(n) : glm::vec3(0, 1, 0);
}
} // namespace
MeshData GpuSkinning::BuildMesh(const AnimationRig &rig, MeshData mesh, bool includeFloor,
                                glm::mat4 placement)
{
    const auto bind = rig.BindPose();
    for (const auto &p : rig.primitives)
    {
        SubMesh sub{uint32_t(mesh.indices.size()), uint32_t(p.indices.size()),
                    p.material < 0 ? int(mesh.materials.size() - 1) : p.material};
        const auto &material = mesh.materials.at(sub.materialId);
        if (material.textures[1] >= 0)
            throw std::runtime_error("Animated normal maps require tangent import, not implemented yet");
        sub.transparent = material.blend;
        sub.doubleSided = material.doubleSided;
        uint32_t base = uint32_t(mesh.vertices.size());
        for (const auto &v : p.vertices)
        {
            glm::mat4 skin = rig.nodes[p.node].skin >= 0 ? glm::mat4(0) : bind.world[p.node];
            if (rig.nodes[p.node].skin >= 0)
                for (int k = 0; k < 4; ++k)
                    skin += bind.world[p.node] * bind.palettes[p.node][v.joints[k]] * v.weights[k];
            skin = placement * skin;
            const auto pos = glm::vec3(skin * glm::vec4(v.position, 1));
            const auto normal = Unit(glm::transpose(glm::inverse(glm::mat3(skin))) * v.normal);
            Vertex vertex{};
            for (int k = 0; k < 3; ++k)
            {
                vertex.position[k] = pos[k];
                vertex.normal[k] = normal[k];
                vertex.color[k] = 1;
            }
            vertex.texCoord[0] = v.uv.x;
            vertex.texCoord[1] = v.uv.y;
            mesh.vertices.push_back(vertex);
        }
        // Some exported double-sided sheets have all authored normals opposite
        // their winding (the supplied character's frontHair). Preserve normals
        // and UVs, but align winding so gl_FrontFacing flips only the true back.
        const bool reversed = sub.doubleSided && HasUniformlyReversedNormals(p.vertices, p.indices);
        if (reversed)
            std::cout << "[Skinning Import] Reversed triangle winding to match authored normals: "
                      << rig.nodes[p.node].name << " (" << p.indices.size() / 3 << " triangles)\n";
        for (size_t i = 0; i < p.indices.size(); i += 3)
        {
            mesh.indices.push_back(base + p.indices[i]);
            mesh.indices.push_back(base + p.indices[i + (reversed ? 2 : 1)]);
            mesh.indices.push_back(base + p.indices[i + (reversed ? 1 : 2)]);
        }
        mesh.subMeshes.push_back(sub);
    }
    if (!includeFloor)
        return mesh;
    const uint32_t first = uint32_t(mesh.vertices.size()), index = uint32_t(mesh.indices.size());
    for (glm::vec3 p : {glm::vec3(-4, 0, -4), glm::vec3(-4, 0, 4), glm::vec3(4, 0, 4), glm::vec3(4, 0, -4)})
    {
        Vertex v{};
        for (int k = 0; k < 3; ++k)
        {
            v.position[k] = p[k];
            v.color[k] = 1;
        }
        v.normal[1] = 1;
        mesh.vertices.push_back(v);
    }
    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
        mesh.indices.push_back(first + i);
    MaterialData floor;
    floor.diffuseColor[0] = .35f;
    floor.diffuseColor[1] = .38f;
    floor.diffuseColor[2] = .42f;
    floor.roughness = .85f;
    mesh.materials.push_back(floor);
    mesh.subMeshes.push_back({index, 6, int(mesh.materials.size() - 1)});
    return mesh;
}
GpuSkinning::GpuSkinning(const PhysicalDevice &physical, const LogicalDevice &device, const AnimationRig &rig,
                         const MeshData &mesh, Buffer &output, bool verify, glm::mat4 placement)
    : m_Device(device), m_Rig(rig), m_Output(output), m_Verify(verify), m_Placement(placement)
{
    auto limits = physical.GetProperties().limits;
    if (output.GetSize() > limits.maxStorageBufferRange)
        throw std::runtime_error("Animated vertex storage exceeds device range");
    m_Matrices.push_back({-1, -1});
    uint32_t morphCount = 0;
    for (const auto &p : rig.primitives)
    {
        m_MorphOffsets.push_back(morphCount);
        morphCount += uint32_t(p.morphs.size());
        const uint32_t base = uint32_t(m_Matrices.size());
        if (rig.nodes[p.node].skin >= 0)
            for (size_t j = 0; j < rig.skins[rig.nodes[p.node].skin].joints.size(); ++j)
                m_Matrices.push_back({p.node, int(j)});
        else
            m_Matrices.push_back({p.node, -1});
        m_PrimitiveFirst.push_back(uint32_t(m_Input.size()));
        for (size_t vi = 0; vi < p.vertices.size(); ++vi)
        {
            const auto &v = p.vertices[vi];
            Input input;
            input.position = {v.position, 1};
            input.normal = {v.normal, 0};
            input.joints = v.joints;
            input.weights = v.weights;
            if (rig.nodes[p.node].skin < 0)
            {
                input.joints = glm::uvec4(0);
                input.weights = {1, 0, 0, 0};
            }
            input.meta = {base, uint32_t(m_Deltas.size()), uint32_t(p.morphs.size()), m_MorphOffsets.back()};
            for (const auto &m : p.morphs)
                m_Deltas.push_back({glm::vec4(m.positions.empty() ? glm::vec3(0) : m.positions[vi], 0),
                                    glm::vec4(m.normals.empty() ? glm::vec3(0) : m.normals[vi], 0)});
            m_Input.push_back(input);
        }
    }
    for (auto &v : m_Input)
        v.meta.w += uint32_t(m_Matrices.size() * 12);
    for (size_t i = m_Input.size(); i < mesh.vertices.size(); ++i)
    {
        Input v;
        v.position = glm::vec4(glm::make_vec3(mesh.vertices[i].position), 1);
        v.normal = glm::vec4(glm::make_vec3(mesh.vertices[i].normal), 0);
        v.weights = {1, 0, 0, 0};
        m_Input.push_back(v);
    }
    m_SurfaceFaces.resize(m_Input.size());
    for (size_t pi = 0; pi < rig.primitives.size(); ++pi)
    {
        const auto &p = rig.primitives[pi];
        const bool reverse =
            mesh.subMeshes[pi].doubleSided && HasUniformlyReversedNormals(p.vertices, p.indices);
        auto adjacency = BuildSurfaceAdjacency(p, m_PrimitiveFirst[pi], reverse);
        for (size_t vi = 0; vi < adjacency.size(); ++vi)
        {
            const uint32_t index = m_PrimitiveFirst[pi] + uint32_t(vi);
            m_SurfaceFaces[index] = std::move(adjacency[vi]);
            m_Input[index].surface = {uint32_t(m_Deltas.size()), uint32_t(m_SurfaceFaces[index].size()), 0,
                                      0};
            // Pack adjacency into the existing morph SSBO, retaining four bindings.
            for (auto face : m_SurfaceFaces[index])
                m_Deltas.push_back({glm::uintBitsToFloat(glm::uvec4(face, 0)), glm::vec4(0)});
        }
    }
    if (m_Deltas.empty())
        m_Deltas.resize(1);
    for (const auto &sub : mesh.subMeshes)
        m_Transparent.push_back(sub.transparent);
    m_Current = rig.BindPose();
    m_Previous = m_Current;
    m_PoseData = PoseData(m_Current, m_Previous);
    const auto make = [&](VkDeviceSize bytes) {
        if (bytes > limits.maxStorageBufferRange)
            throw std::runtime_error("Skinning storage exceeds device range");
        return std::make_unique<Buffer>(physical, device, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    };
    auto d = device.GetHandle();
    try
    {
        m_InputBuffer = make(m_Input.size() * sizeof(Input));
        m_InputBuffer->UploadData(m_Input.data(), m_InputBuffer->GetSize());
        m_DeltaBuffer = make(m_Deltas.size() * sizeof(Delta));
        m_DeltaBuffer->UploadData(m_Deltas.data(), m_DeltaBuffer->GetSize());
        m_PoseBuffer = make(m_PoseData.size() * sizeof(glm::vec4));
        m_PoseBuffer->UploadData(m_PoseData.data(), m_PoseBuffer->GetSize());
        VkDescriptorSetLayoutBinding bindings[4]{};
        for (uint32_t i = 0; i < 4; ++i)
            bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        sl.bindingCount = 4;
        sl.pBindings = bindings;
        Check(vkCreateDescriptorSetLayout(d, &sl, nullptr, &m_SetLayout));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        Check(vkCreateDescriptorPool(d, &pool, nullptr, &m_Pool));
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_Pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_SetLayout;
        Check(vkAllocateDescriptorSets(d, &alloc, &m_Set));
        const Buffer *buffers[] = {m_InputBuffer.get(), m_PoseBuffer.get(), m_DeltaBuffer.get(), &output};
        VkDescriptorBufferInfo infos[4]{};
        VkWriteDescriptorSet writes[4]{};
        for (uint32_t i = 0; i < 4; ++i)
        {
            infos[i] = {buffers[i]->GetHandle(), 0, buffers[i]->GetSize()};
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(d, 4, writes, 0, nullptr);
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_SetLayout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        Check(vkCreatePipelineLayout(d, &layout, nullptr, &m_Layout));
        auto code = ReadFile("build/shaders/skinning.comp.spv");
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = code.size();
        sm.pCode = reinterpret_cast<const uint32_t *>(code.data());
        VkShaderModule module;
        Check(vkCreateShaderModule(d, &sm, nullptr, &module));
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module = module;
        cp.stage.pName = "main";
        cp.layout = m_Layout;
        auto status = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &cp, nullptr, &m_Pipeline);
        vkDestroyShaderModule(d, module, nullptr);
        Check(status);
    }
    catch (...)
    {
        Release();
        throw;
    }
}
GpuSkinning::~GpuSkinning()
{
    Release();
}
void GpuSkinning::Release()
{
    auto d = m_Device.GetHandle();
    if (m_Pipeline)
        vkDestroyPipeline(d, m_Pipeline, nullptr);
    if (m_Layout)
        vkDestroyPipelineLayout(d, m_Layout, nullptr);
    if (m_Pool)
        vkDestroyDescriptorPool(d, m_Pool, nullptr);
    if (m_SetLayout)
        vkDestroyDescriptorSetLayout(d, m_SetLayout, nullptr);
}
std::vector<glm::vec4> GpuSkinning::PoseData(const RigPose &current, const RigPose &previous) const
{
    size_t weights = 0;
    for (const auto &p : m_Rig.primitives)
        weights += p.morphs.size();
    std::vector<glm::vec4> data(m_Matrices.size() * 12 + weights);
    for (size_t i = 0; i < m_Matrices.size(); ++i)
    {
        auto [node, joint] = m_Matrices[i];
        const auto matrix = [&](const RigPose &pose) {
            return node < 0    ? glm::mat4(1)
                   : joint < 0 ? m_Placement * pose.world[node]
                               : m_Placement * pose.world[node] * pose.palettes[node][joint];
        };
        const auto a = matrix(current), b = matrix(previous);
        for (int c = 0; c < 4; ++c)
        {
            data[i * 12 + c] = a[c];
            data[i * 12 + 4 + c] = b[c];
        }
        auto qa = SkinningMath::FromRigid(a), qb = SkinningMath::FromRigid(b);
        data[i * 12 + 8] = SkinningMath::Pack(qa.real);
        data[i * 12 + 9] = SkinningMath::Pack(qa.dual);
        data[i * 12 + 10] = SkinningMath::Pack(qb.real);
        data[i * 12 + 11] = SkinningMath::Pack(qb.dual);
    }
    for (size_t i = 0; i < m_Rig.primitives.size(); ++i)
    {
        const auto &p = m_Rig.primitives[i];
        for (size_t j = 0; j < p.morphs.size(); ++j)
            data[m_Matrices.size() * 12 + m_MorphOffsets[i] + j] = {current.morphWeights[p.node][j],
                                                                    previous.morphWeights[p.node][j], 0, 0};
    }
    return data;
}
std::pair<glm::vec3, glm::vec3> GpuSkinning::Evaluate(size_t index, const std::vector<glm::vec4> &data,
                                                      bool previous) const
{
    const auto &v = m_Input[index];
    glm::vec3 p(v.position), n(v.normal);
    for (uint32_t k = 0; k < v.meta.z; ++k)
    {
        float w = data[v.meta.w + k][previous ? 1 : 0];
        p += glm::vec3(m_Deltas[v.meta.y + k].position) * w;
        n += glm::vec3(m_Deltas[v.meta.y + k].normal) * w;
    }
    std::array<SkinningMath::DualQuaternion, 4> bones;
    for (int k = 0; k < 4; ++k)
    {
        uint32_t base = (v.meta.x + v.joints[k]) * 12 + 8 + (previous ? 2 : 0);
        bones[k] = {SkinningMath::Unpack(data[base]), SkinningMath::Unpack(data[base + 1])};
    }
    SkinningMath::DualQuaternion blend;
    if (m_DualQuaternion && SkinningMath::Blend(bones, v.weights, blend))
        return {SkinningMath::Transform(blend, p), Unit(blend.real * n)};
    glm::mat4 skin(0);
    for (int k = 0; k < 4; ++k)
    {
        uint32_t base = (v.meta.x + v.joints[k]) * 12 + (previous ? 4 : 0);
        skin += glm::mat4(data[base], data[base + 1], data[base + 2], data[base + 3]) * v.weights[k];
    }
    auto transform = glm::mat3(skin);
    n = std::abs(glm::determinant(transform)) > 1e-8f ? glm::transpose(glm::inverse(transform)) * n
                                                      : transform * n;
    return {glm::vec3(skin * glm::vec4(p, 1)), Unit(n)};
}
void GpuSkinning::Update(size_t clip, float seconds, bool loop)
{
    ReadCompleted();
    m_Current = m_Rig.Sample(clip, seconds, loop);
    m_PoseData = PoseData(m_Current, m_Valid ? m_Previous : m_Current);
    m_PoseBuffer->UploadData(m_PoseData.data(), m_PoseBuffer->GetSize());
}
void GpuSkinning::SetDualQuaternion(bool enabled)
{
    ReadCompleted();
    m_Valid = false;
    m_DualQuaternion = enabled;
    std::cout << "[Skinning] " << (enabled ? "dual quaternion (nonrigid matrix fallback)" : "linear blend")
              << '\n';
}
void GpuSkinning::SetSurfaceNormals(bool enabled)
{
    ReadCompleted();
    m_Valid = false;
    m_SurfaceNormals = enabled;
    std::cout << "[Skinning Normals] " << (enabled ? "deformed surface" : "transformed authored") << '\n';
}
void GpuSkinning::MarkSubmitted()
{
    m_Previous = m_Current;
    m_Valid = true;
    if (m_Verify)
    {
        m_LastData = m_PoseData;
        m_Pending = true;
    }
}
void GpuSkinning::Record(VkCommandBuffer cmd) const
{
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    b.buffer = m_Output.GetHandle();
    b.offset = 0;
    b.size = VK_WHOLE_SIZE;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.srcAccessMask =
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &b, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Layout, 0, 1, &m_Set, 0, nullptr);
    uint32_t settings[] = {uint32_t(m_Input.size()), PreviousOffset(), m_DualQuaternion ? 1u : 0u, 0};
    vkCmdPushConstants(cmd, m_Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, settings);
    vkCmdDispatch(cmd, (settings[0] + 63) / 64, 1, 1);
    if (m_SurfaceNormals)
    {
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &b, 0, nullptr);
        settings[3] = 1;
        vkCmdPushConstants(cmd, m_Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, settings);
        vkCmdDispatch(cmd, (settings[0] + 63) / 64, 1, 1);
    }
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask =
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &b, 0, nullptr);
}
void GpuSkinning::ReadCompleted()
{
    if (!m_Pending)
        return;
    m_Pending = false;
    m_LastGpuData.resize(size_t(m_Output.GetSize() / 4));
    auto &data = m_LastGpuData;
    m_Output.DownloadData(data.data(), m_Output.GetSize());
    float maxPosition = 0, maxNormal = 0, maxPrevious = 0;
    std::vector<glm::vec3> referencePositions;
    if (m_SurfaceNormals)
        for (size_t i = 0; i < m_Input.size(); ++i)
            referencePositions.push_back(Evaluate(i, m_LastData, false).first);
    for (size_t i = 0; i < m_Input.size(); ++i)
    {
        auto [p, n] = Evaluate(i, m_LastData, false);
        if (m_SurfaceNormals)
            n = RebuildSurfaceNormal(referencePositions, m_SurfaceFaces[i], n);
        auto previous = Evaluate(i, m_LastData, true).first;
        for (int k = 0; k < 3; ++k)
        {
            maxPosition = std::max(maxPosition, std::abs(data[i * 15 + k] - p[k]));
            maxNormal = std::max(maxNormal, std::abs(data[i * 15 + 6 + k] - n[k]));
            maxPrevious = std::max(maxPrevious, std::abs(data[PreviousOffset() + i * 4 + k] - previous[k]));
            if (!std::isfinite(data[i * 15 + k]) || !std::isfinite(data[i * 15 + 6 + k]) ||
                !std::isfinite(data[PreviousOffset() + i * 4 + k]))
                throw std::runtime_error("Nonfinite GPU skinning result");
        }
    }
    if (maxPosition > .0003f || maxPrevious > .0003f || maxNormal > .002f)
        throw std::runtime_error("GPU skinning differs from CPU vertex reference");
    std::cout << "[Skinning Verify] vertices=" << m_Input.size() << " position=" << maxPosition
              << " normal=" << maxNormal << " previous=" << maxPrevious << std::endl;
}
std::vector<uint32_t> GpuSkinning::DrawOrder(const glm::mat4 &view) const
{
    std::vector<uint32_t> order;
    std::vector<std::pair<float, uint32_t>> transparent;
    for (uint32_t i = 0; i < m_Transparent.size(); ++i)
    {
        if (!m_Transparent[i])
            order.push_back(i);
        else
        {
            glm::vec3 center(0);
            const auto count = m_Rig.primitives[i].vertices.size();
            for (size_t v = 0; v < count; ++v)
                center += Evaluate(m_PrimitiveFirst[i] + v, m_PoseData, false).first;
            center /= float(count);
            transparent.push_back({(view * glm::vec4(center, 1)).z, i});
        }
    }
    std::sort(transparent.begin(), transparent.end());
    for (const auto &p : transparent)
        order.push_back(p.second);
    return order;
}

float GpuSkinning::VerifyMotion(const glm::mat4 &currentVP, const glm::mat4 &previousVP, bool history,
                                glm::vec2 extent, const std::vector<MotionSample> &samples) const
{
    if (m_LastGpuData.empty())
        throw std::runtime_error("Missing completed skinning data for motion verification");
    struct Projected
    {
        glm::vec4 current, previous;
        glm::vec2 uv;
    };
    std::vector<Projected> vertices(m_Input.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        auto &v = vertices[i];
        v.current = currentVP *
                    glm::vec4(m_LastGpuData[i * 15], m_LastGpuData[i * 15 + 1], m_LastGpuData[i * 15 + 2], 1);
        const size_t b = PreviousOffset() + i * 4;
        v.previous = previousVP * glm::vec4(m_LastGpuData[b], m_LastGpuData[b + 1], m_LastGpuData[b + 2], 1);
        v.uv = glm::vec2(v.current) / v.current.w * .5f + .5f;
    }
    std::vector<float> closest(samples.size(), 2.f);
    std::vector<glm::vec4> oldClip(samples.size());
    std::vector<bool> interior(samples.size());
    for (size_t primitive = 0; primitive < m_Rig.primitives.size(); ++primitive)
    {
        if (m_Transparent[primitive])
            continue;
        const auto &indices = m_Rig.primitives[primitive].indices;
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            const auto &a = vertices[m_PrimitiveFirst[primitive] + indices[triangle]],
                       &b = vertices[m_PrimitiveFirst[primitive] + indices[triangle + 1]],
                       &c = vertices[m_PrimitiveFirst[primitive] + indices[triangle + 2]];
            if (a.current.w <= 0 || b.current.w <= 0 || c.current.w <= 0 || a.current.z < 0 ||
                b.current.z < 0 || c.current.z < 0 || a.current.z > a.current.w ||
                b.current.z > b.current.w || c.current.z > c.current.w)
                continue;
            const auto v0 = b.uv - a.uv, v1 = c.uv - a.uv;
            const float area = v0.x * v1.y - v0.y * v1.x;
            if (std::abs(area) < 1e-12f)
                continue;
            auto low = glm::min(a.uv, glm::min(b.uv, c.uv)), high = glm::max(a.uv, glm::max(b.uv, c.uv));
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const auto p = samples[i].uv;
                if (p.x < low.x || p.y < low.y || p.x > high.x || p.y > high.y)
                    continue;
                const auto delta = p - a.uv;
                float y = (delta.x * v1.y - delta.y * v1.x) / area,
                      z = (v0.x * delta.y - v0.y * delta.x) / area, x = 1 - y - z;
                if (std::min(x, std::min(y, z)) < 0)
                    continue;
                const float depth = x * a.current.z / a.current.w + y * b.current.z / b.current.w +
                                    z * c.current.z / c.current.w;
                if (depth >= closest[i] - 1e-7f)
                    continue;
                closest[i] = depth;
                interior[i] = std::min(x, std::min(y, z)) > .01f;
                const float denominator = x / a.current.w + y / b.current.w + z / c.current.w;
                oldClip[i] = (a.previous * (x / a.current.w) + b.previous * (y / b.current.w) +
                              c.previous * (z / c.current.w)) /
                             denominator;
            }
        }
    }
    size_t checks = 0;
    float maximum = 0;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (!interior[i] || std::abs(closest[i] - samples[i].depth) > 1e-5f)
            continue;
        const auto expected = ProjectPreviousMotion(samples[i].uv, oldClip[i], history);
        const auto oldUv = samples[i].uv + expected.delta;
        const float border = std::min(std::min(std::abs(oldUv.x), std::abs(1 - oldUv.x)),
                                      std::min(std::abs(oldUv.y), std::abs(1 - oldUv.y)));
        if (expected.candidate != (samples[i].motion.w > 0) && border > 1e-4f &&
            std::abs(expected.previousDepth) > 1e-5f && std::abs(1 - expected.previousDepth) > 1e-5f)
            throw std::runtime_error("Deformed motion candidate flag differs from triangle projection");
        const auto error = glm::abs((glm::vec2(samples[i].motion) - expected.delta) * extent);
        if (expected.candidate && samples[i].motion.w > 0)
        {
            maximum = std::max(maximum, std::max(error.x, error.y));
            ++checks;
            if (std::abs(samples[i].motion.z - expected.previousDepth) > .001f)
                throw std::runtime_error("Deformed previous depth differs from triangle interpolation");
        }
        if (!history && glm::vec2(samples[i].motion) != glm::vec2(0))
            throw std::runtime_error("Deformed motion failed to reset");
    }
    if (maximum > .05f)
        throw std::runtime_error("Deformed motion differs from CPU triangle interpolation");
    std::cout << "[Deformed Motion Verify] pixels=" << checks << " max_error_px=" << maximum << std::endl;
    return maximum;
}
