#include "RayQueryReflections.h"
#include "RayQueryMath.h"
#include "ShaderUtils.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace
{
void Check(VkResult r)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error("Ray Query Vulkan operation failed: " + std::to_string(r));
}
} // namespace
RayQueryReflections::RayQueryReflections(const PhysicalDevice &physical, const LogicalDevice &device)
    : m_Physical(physical), m_Device(device)
{
    if (!device.RayQueryEnabled())
        throw std::runtime_error("Ray Query feature was not enabled");
    auto d = device.GetHandle();
    m_Create = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(d, "vkCreateAccelerationStructureKHR"));
    m_Destroy = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(d, "vkDestroyAccelerationStructureKHR"));
    m_Sizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        vkGetDeviceProcAddr(d, "vkGetAccelerationStructureBuildSizesKHR"));
    m_Build = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        vkGetDeviceProcAddr(d, "vkCmdBuildAccelerationStructuresKHR"));
    m_Address = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(d, "vkGetAccelerationStructureDeviceAddressKHR"));
    if (!m_Create || !m_Destroy || !m_Sizes || !m_Build || !m_Address)
        throw std::runtime_error("Missing Ray Query device entry points");
    try
    {
        VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
             VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 7;
        layout.pBindings = bindings;
        Check(vkCreateDescriptorSetLayout(d, &layout, nullptr, &m_Layout));
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 4;
        pool.pPoolSizes = sizes;
        Check(vkCreateDescriptorPool(d, &pool, nullptr, &m_Pool));
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = m_Pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &m_Layout;
        Check(vkAllocateDescriptorSets(d, &allocation, &m_Set));
        m_Settings = std::make_unique<Buffer>(physical, device, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Update();
    }
    catch (...)
    {
        Release();
        throw;
    }
}
RayQueryReflections::~RayQueryReflections()
{
    Release();
}
void RayQueryReflections::Release()
{
    auto d = m_Device.GetHandle();
    if (m_Gather)
        vkDestroyPipeline(d, m_Gather, nullptr);
    if (m_GatherLayout)
        vkDestroyPipelineLayout(d, m_GatherLayout, nullptr);
    if (m_Tlas)
        m_Destroy(d, m_Tlas, nullptr);
    if (m_Blas)
        m_Destroy(d, m_Blas, nullptr);
    if (m_Pool)
        vkDestroyDescriptorPool(d, m_Pool, nullptr);
    if (m_Layout)
        vkDestroyDescriptorSetLayout(d, m_Layout, nullptr);
}
void RayQueryReflections::Update()
{
    glm::vec4 settings(m_Enabled ? (m_Stylized ? 2.f : 1.f) : 0.f, float(m_Debug), .002f, 100.f);
    m_Settings->UploadData(&settings, sizeof(settings));
}
void RayQueryReflections::Submit(const std::function<void(VkCommandBuffer)> &record)
{
    auto d = m_Device.GetHandle();
    VkCommandPool pool = VK_NULL_HANDLE;
    try
    {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.queueFamilyIndex = m_Physical.FindQueueFamilies().graphicsFamily.value();
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        Check(vkCreateCommandPool(d, &info, nullptr, &pool));
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        VkCommandBuffer cmd;
        Check(vkAllocateCommandBuffers(d, &allocation, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(cmd, &begin));
        record(cmd);
        Check(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        Check(vkQueueSubmit(m_Device.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE));
        Check(vkQueueWaitIdle(m_Device.GetGraphicsQueue()));
        vkDestroyCommandPool(d, pool, nullptr);
    }
    catch (...)
    {
        if (pool)
            vkDestroyCommandPool(d, pool, nullptr);
        throw;
    }
}
void RayQueryReflections::Build(const MeshData &mesh, bool verify, const Buffer *deformed)
{
    if (m_Blas)
        throw std::runtime_error("Static ray scene already built");
    m_Dynamic = deformed != nullptr;
    m_Verify = verify;
    for (const auto &sub : mesh.subMeshes)
    {
        const auto &material = mesh.materials.at(sub.materialId);
        m_HasAlpha|=material.blend;
        if (material.hasDiffuseTexture && material.textures[0] != 0)
            throw std::runtime_error("Ray Query currently supports one shared base-color atlas");
        for (int slot = 1; slot < 5; ++slot)
            if (material.textures[slot] >= 0)
                throw std::runtime_error(
                    "Ray Query hit shading does not yet support additional material maps");
        for (uint32_t instance = 0; instance < sub.instanceCount; ++instance)
        {
            const auto transform = mesh.instances.at(sub.firstInstance + instance).model;
            if(m_Dynamic && transform!=glm::mat4(1))throw std::runtime_error("Dynamic Ray Query expects scene-space vertices and identity instances");
            const auto normalTransform = glm::transpose(glm::inverse(glm::mat3(transform)));
            for (uint32_t index = sub.firstIndex; index < sub.firstIndex + sub.indexCount; index += 3)
            {
                Triangle tri{};
                glm::vec4 *p[] = {&tri.a, &tri.b, &tri.c};
                glm::vec4 *n[] = {&tri.na, &tri.nb, &tri.nc};
                for (int k = 0; k < 3; ++k)
                {
                    const auto &v = mesh.vertices.at(mesh.indices.at(index + k));
                    *p[k] = transform * glm::vec4(glm::make_vec3(v.position), 1);
                    *n[k] = glm::vec4(glm::normalize(normalTransform * glm::make_vec3(v.normal)), 0);
                }
                const auto cross = glm::cross(glm::vec3(tri.b - tri.a), glm::vec3(tri.c - tri.a));
                if (!m_Dynamic && glm::dot(cross, cross) < 1e-15f)
                    continue;
                tri.color = {material.diffuseColor[0], material.diffuseColor[1], material.diffuseColor[2],
                             material.alpha};
                tri.properties = {material.metallic, material.roughness, material.rayMirror ? float(1 + material.reflectionStyle) : 0.f,
                                  float((material.hasDiffuseTexture ? 1 : 0) | (material.blend ? 2 : 0))};
                const auto &va = mesh.vertices[mesh.indices[index]];
                const auto &vb = mesh.vertices[mesh.indices[index + 1]];
                const auto &vc = mesh.vertices[mesh.indices[index + 2]];
                tri.uv01 = {va.texCoord[0], va.texCoord[1], vb.texCoord[0], vb.texCoord[1]};
                tri.uv2 = {vc.texCoord[0], vc.texCoord[1], 0, 0};
                m_Map.push_back({mesh.indices[index], mesh.indices[index + 1], mesh.indices[index + 2], 0});
                m_Data.push_back(tri);
            }
        }
    }
    if (m_Data.empty())
        throw std::runtime_error("Empty ray geometry");
    auto d = m_Device.GetHandle();
    const auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(m_Physical.GetHandle(), VK_FORMAT_R32G32B32_SFLOAT, &format);
    if (!(format.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR))
        throw std::runtime_error("Ray Query vertex format unsupported");
    m_Triangles = std::make_unique<Buffer>(
        m_Physical, m_Device, m_Data.size() * sizeof(Triangle),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        host);
    m_Triangles->UploadData(m_Data.data(), m_Triangles->GetSize());
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < m_Data.size(); ++i)
        for (uint32_t k = 0; k < 3; ++k)
            indices.push_back(i * 10 + k);
    m_Indices =
        std::make_unique<Buffer>(m_Physical, m_Device, indices.size() * 4,
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                 host);
    m_Indices->UploadData(indices.data(), m_Indices->GetSize());
    m_Metadata = std::make_unique<Buffer>(m_Physical, m_Device, m_Map.size() * 16,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host);
    m_Metadata->UploadData(m_Map.data(), m_Metadata->GetSize());
    m_AtlasPixels = mesh.images.empty() ? TextureData{1, 1, 4, {255, 255, 255, 255}} : mesh.images[0];
    m_Atlas = std::make_unique<TextureImage>(m_Physical, m_Device, m_AtlasPixels, true, !mesh.images.empty());
    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &properties;
    vkGetPhysicalDeviceProperties2(m_Physical.GetHandle(), &properties2);
    if (m_Data.size() > properties.maxPrimitiveCount ||
        m_Triangles->GetSize() > m_Physical.GetProperties().limits.maxStorageBufferRange)
        throw std::runtime_error("Ray scene exceeds device limits");
    const auto build = [&](VkAccelerationStructureTypeKHR type, VkAccelerationStructureGeometryKHR &geom,
                           uint32_t count, std::unique_ptr<Buffer> &backing,
                           VkAccelerationStructureKHR &output) {
        VkAccelerationStructureBuildGeometryInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        info.type = type;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                     (m_Dynamic ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0);
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = 1;
        info.pGeometries = &geom;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        m_Sizes(d, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &count, &sizes);
        backing = std::make_unique<Buffer>(m_Physical, m_Device, sizes.accelerationStructureSize,
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create.buffer = backing->GetHandle();
        create.size = sizes.accelerationStructureSize;
        create.type = type;
        Check(m_Create(d, &create, nullptr, &output));
        VkDeviceSize alignment = properties.minAccelerationStructureScratchOffsetAlignment;
        auto &scratch =
            type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR ? m_BlasScratch : m_TlasScratch;
        scratch = std::make_unique<Buffer>(
            m_Physical, m_Device, std::max(sizes.buildScratchSize, sizes.updateScratchSize) + alignment - 1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        info.dstAccelerationStructure = output;
        info.scratchData.deviceAddress = (scratch->Address() + alignment - 1) / alignment * alignment;
        (type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR ? m_BlasScratchAddress
                                                                 : m_TlasScratchAddress) =
            info.scratchData.deviceAddress;
        VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0};
        const auto *ranges = &range;
        Submit([&](VkCommandBuffer cmd) {
            m_Build(cmd, 1, &info, &ranges);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        });
    };
    auto triangles = Geometry();
    build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangles, uint32_t(m_Data.size()), m_BlasBuffer,
          m_Blas);
    VkAccelerationStructureDeviceAddressInfoKHR address{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    address.accelerationStructure = m_Blas;
    VkAccelerationStructureInstanceKHR instance{};
    for (int k = 0; k < 3; ++k)
        instance.transform.matrix[k][k] = 1;
    instance.mask = 255;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = m_Address(d, &address);
    m_Instances =
        std::make_unique<Buffer>(m_Physical, m_Device, sizeof(instance),
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                 host);
    m_Instances->UploadData(&instance, sizeof(instance));
    auto top = Instances();
    build(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, top, 1, m_TlasBuffer, m_Tlas);
    m_Checks = std::make_unique<Buffer>(m_Physical, m_Device, 128 * sizeof(CheckRay),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host);
    VkWriteDescriptorSetAccelerationStructureKHR as{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    as.accelerationStructureCount = 1;
    as.pAccelerationStructures = &m_Tlas;
    VkDescriptorBufferInfo buffers[] = {{m_Triangles->GetHandle(), 0, m_Triangles->GetSize()},
                                        {m_Settings->GetHandle(), 0, 16},
                                        {m_Checks->GetHandle(), 0, m_Checks->GetSize()}};
    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_Set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        if (i)
        {
            writes[i].descriptorType =
                i == 2 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &buffers[i - 1];
        }
        else
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            writes[i].pNext = &as;
        }
    }
    vkUpdateDescriptorSets(d, 4, writes, 0, nullptr);
    VkDescriptorBufferInfo source{deformed ? deformed->GetHandle() : m_Triangles->GetHandle(), 0,
                                  deformed ? deformed->GetSize() : m_Triangles->GetSize()},
        metadata{m_Metadata->GetHandle(), 0, m_Metadata->GetSize()};
    VkDescriptorImageInfo atlas{m_Atlas->GetSampler(), m_Atlas->GetImageView(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet extra[3]{};
    for (int i = 0; i < 3; ++i)
    {
        extra[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extra[i].dstSet = m_Set;
        extra[i].dstBinding = i == 2 ? 7 : 4 + i;
        extra[i].descriptorCount = 1;
        extra[i].descriptorType =
            i == 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if (i == 2)
            extra[i].pImageInfo = &atlas;
        else
            extra[i].pBufferInfo = i == 0 ? &source : &metadata;
    }
    vkUpdateDescriptorSets(d, 3, extra, 0, nullptr);
    if (m_Dynamic)
    {
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_Layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        Check(vkCreatePipelineLayout(d, &layout, nullptr, &m_GatherLayout));
        auto bytes = ReadFile("build/shaders/ray_query_gather.comp.spv");
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = bytes.size();
        shader.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        VkShaderModule module;
        Check(vkCreateShaderModule(d, &shader, nullptr, &module));
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.layout = m_GatherLayout;
        pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                          nullptr,
                          0,
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          module,
                          "main",
                          nullptr};
        auto result = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_Gather);
        vkDestroyShaderModule(d, module, nullptr);
        Check(result);
    }
    std::cout << "[Ray Query Build] " << (m_Dynamic ? "updatable" : "static")
              << " triangles=" << m_Data.size() << " BLAS=1 TLAS=1 scratch_alignment="
              << properties.minAccelerationStructureScratchOffsetAlignment << '\n';
    if (verify)
        Verify();
}
VkAccelerationStructureGeometryKHR RayQueryReflections::Geometry() const
{
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = m_HasAlpha ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto &t = geometry.geometry.triangles;
    t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    t.vertexData.deviceAddress = m_Triangles->Address();
    t.vertexStride = 16;
    t.maxVertex = uint32_t((m_Data.size() - 1) * 10 + 2);
    t.indexType = VK_INDEX_TYPE_UINT32;
    t.indexData.deviceAddress = m_Indices->Address();
    return geometry;
}
VkAccelerationStructureGeometryKHR RayQueryReflections::Instances() const
{
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = m_Instances->Address();
    return geometry;
}
void RayQueryReflections::RecordUpdates(VkCommandBuffer cmd) const
{
    if (!m_Dynamic)
        return;
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Gather);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_GatherLayout, 0, 1, &m_Set, 0, nullptr);
    uint32_t count = uint32_t(m_Data.size());
    vkCmdPushConstants(cmd, m_GatherLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &count);
    vkCmdDispatch(cmd, (count + 63) / 64, 1, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    barrier.srcAccessMask =
        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask =
        VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    auto build = [&](VkAccelerationStructureKHR acceleration, VkAccelerationStructureTypeKHR type,
                     VkAccelerationStructureGeometryKHR &geometry, uint32_t primitives,
                     VkDeviceAddress scratch) {
        VkAccelerationStructureBuildGeometryInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        info.type = type;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                     VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        info.srcAccelerationStructure = info.dstAccelerationStructure = acceleration;
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        info.scratchData.deviceAddress = scratch;
        VkAccelerationStructureBuildRangeInfoKHR range{primitives, 0, 0, 0};
        const auto *ranges = &range;
        m_Build(cmd, 1, &info, &ranges);
    };
    auto triangles = Geometry();
    build(m_Blas, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangles, count, m_BlasScratchAddress);
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    auto instances = Instances();
    build(m_Tlas, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, instances, 1, m_TlasScratchAddress);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}
void RayQueryReflections::ReadCompleted(const std::vector<float> *vertices)
{
    if (!m_Pending)
        return;
    m_Pending = false;
    m_Triangles->DownloadData(m_Data.data(), m_Triangles->GetSize());
    if (!vertices || vertices->empty())
        throw std::runtime_error("Dynamic ray verification needs completed skinned vertices");
    float error = 0;
    for (size_t i = 0; i < m_Data.size(); ++i)
    {
        const auto &t = m_Data[i];
        const glm::vec4 positions[] = {t.a, t.b, t.c}, normals[] = {t.na, t.nb, t.nc};
        for (int k = 0; k < 3; ++k)
        {
            size_t base = size_t(m_Map[i][k]) * 15;
            for (int axis = 0; axis < 3; ++axis)
            {
                float a = std::abs(positions[k][axis] - vertices->at(base + axis)),
                      b = std::abs(normals[k][axis] - vertices->at(base + 6 + axis));
                if (!std::isfinite(a) || !std::isfinite(b) || a > .0003f || b > .002f)
                    throw std::runtime_error("Ray geometry differs from raster skinned vertices");
                error = std::max(error, a);
            }
        }
    }
    std::cout << "[Ray Skinning Verify] triangles=" << m_Data.size() << " raster_position_error=" << error
              << " normals=OK refit=BLAS+TLAS\n";
    Verify();
}
bool RayQueryReflections::Accept(const Triangle &t, glm::vec2 bary) const
{
    if ((int(t.properties.w) & 2) == 0)
        return true;
    float alpha = t.color.a;
    if (int(t.properties.w) & 1)
    {
        glm::vec2 uv = glm::vec2(t.uv01) * (1 - bary.x - bary.y) + glm::vec2(t.uv01.z, t.uv01.w) * bary.x +
                       glm::vec2(t.uv2) * bary.y;
        glm::vec2 pixel = uv * glm::vec2(m_AtlasPixels.width, m_AtlasPixels.height) - .5f;
        int x = int(std::floor(pixel.x)), y = int(std::floor(pixel.y));
        float fx = pixel.x - x, fy = pixel.y - y;
        auto sample = [&](int px, int py) {
            px = (px % m_AtlasPixels.width + m_AtlasPixels.width) % m_AtlasPixels.width;
            py = (py % m_AtlasPixels.height + m_AtlasPixels.height) % m_AtlasPixels.height;
            return m_AtlasPixels.pixels[(size_t(py) * m_AtlasPixels.width + px) * 4 + 3] / 255.f;
        };
        alpha *= glm::mix(glm::mix(sample(x, y), sample(x + 1, y), fx),
                          glm::mix(sample(x, y + 1), sample(x + 1, y + 1), fx), fy);
    }
    return alpha >= .5f;
}
void RayQueryReflections::Verify()
{
    std::vector<CheckRay> rays(128);
    std::vector<size_t> valid;
    for (size_t i = 0; i < m_Data.size(); ++i)
    {
        auto &t = m_Data[i];
        auto n = glm::cross(glm::vec3(t.b - t.a), glm::vec3(t.c - t.a));
        if (glm::dot(n, n) > 1e-15f)
            valid.push_back(i);
    }
    if (valid.empty())
        throw std::runtime_error("No finite triangles for ray verification");
    for (size_t i = 0; i < rays.size(); ++i)
    {
        const auto &t = m_Data[valid[(i * 73) % valid.size()]];
        glm::vec3 center = glm::vec3(t.a * .2f + t.b * .3f + t.c * .5f),
                  normal = glm::normalize(glm::cross(glm::vec3(t.b - t.a), glm::vec3(t.c - t.a)));
        rays[i].origin = glm::vec4(i % 4 ? center + normal * .7f : glm::vec3(0, 5, 0), .001f);
        rays[i].direction = glm::vec4(i % 4 ? -normal : glm::vec3(0, 1, 0), 100.f);
    }
    m_Checks->UploadData(rays.data(), m_Checks->GetSize());
    auto d = m_Device.GetHandle();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    try
    {
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &m_Layout;
        Check(vkCreatePipelineLayout(d, &info, nullptr, &layout));
        auto bytes = ReadFile("build/shaders/ray_query_check.comp.spv");
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = bytes.size();
        shader.pCode = reinterpret_cast<const uint32_t *>(bytes.data());
        Check(vkCreateShaderModule(d, &shader, nullptr, &module));
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.layout = layout;
        compute.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                         nullptr,
                         0,
                         VK_SHADER_STAGE_COMPUTE_BIT,
                         module,
                         "main",
                         nullptr};
        Check(vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &compute, nullptr, &pipeline));
        Submit([&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_Set, 0, nullptr);
            vkCmdDispatch(cmd, 2, 1, 1);
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);
        });
        std::vector<CheckRay> results(128);
        m_Checks->DownloadData(results.data(), m_Checks->GetSize());
        size_t hits = 0, misses = 0;
        float maxError = 0;
        for (size_t i = 0; i < rays.size(); ++i)
        {
            float nearest = rays[i].direction.w;
            uint32_t id = ~0u;
            glm::vec3 origin(rays[i].origin), direction(rays[i].direction);
            for (uint32_t j = 0; j < m_Data.size(); ++j)
            {
                const auto &t = m_Data[j];
                float distance;
                glm::vec2 bary;
                if (RayTriangleReference(origin, direction, glm::vec3(t.a), glm::vec3(t.b), glm::vec3(t.c),
                                         rays[i].origin.w, nearest, distance, bary) &&
                    Accept(t, bary))
                {
                    nearest = distance;
                    id = j;
                }
            }
            const auto &result = results[i];
            if ((id == ~0u) != (result.hit.x == ~0u))
                throw std::runtime_error("Ray Query CPU/GPU hit classification differs");
            if (id == ~0u)
            {
                ++misses;
                continue;
            }
            ++hits;
            float error = std::abs(nearest - result.result.x);
            maxError = std::max(error, maxError);
            if (!std::isfinite(error) || error > 1e-3f || result.hit.x >= m_Data.size())
                throw std::runtime_error("Ray Query nearest distance differs from CPU");
            const auto &hit = m_Data[result.hit.x];
            float u = result.result.y, v = result.result.z;
            auto reconstructed = glm::vec3(hit.a * (1 - u - v) + hit.b * u + hit.c * v);
            float pointError = glm::length(reconstructed - (origin + direction * result.result.x));
            if (!std::isfinite(pointError) || pointError > .001f || u < -.0001f || v < -.0001f ||
                u + v > 1.0001f)
                throw std::runtime_error("Ray Query barycentrics disagree with ray hit position");
            if (glm::length(glm::vec3(m_Data[id].color - m_Data[result.hit.x].color)) > .001f)
                throw std::runtime_error("Ray Query hit material differs from CPU");
        }
        if (hits < 16 || misses < 16)
            throw std::runtime_error("Ray Query verification lacks hit/miss coverage");
        std::cout << "[Ray Query Verify] rays=128 hits=" << hits << " misses=" << misses
                  << " max_distance_error=" << maxError << " material=OK\n";
        vkDestroyPipeline(d, pipeline, nullptr);
        vkDestroyShaderModule(d, module, nullptr);
        vkDestroyPipelineLayout(d, layout, nullptr);
    }
    catch (...)
    {
        if (pipeline)
            vkDestroyPipeline(d, pipeline, nullptr);
        if (module)
            vkDestroyShaderModule(d, module, nullptr);
        if (layout)
            vkDestroyPipelineLayout(d, layout, nullptr);
        throw;
    }
}
