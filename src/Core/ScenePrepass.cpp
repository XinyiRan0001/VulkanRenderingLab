#include "ScenePrepass.h"
#include "AoBlurMath.h"
#include "GpuSkinning.h"
#include "MotionMath.h"
#include "ShaderUtils.h"
#include "Vertex.h"
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/packing.hpp>
#include <iomanip>
#include <iostream>
#include <stdexcept>
namespace
{
void Check(VkResult r)
{
    if (r != VK_SUCCESS)
        throw std::runtime_error("Cannot create scene prepass resource");
}
VkShaderModule Load(VkDevice device, const char *path)
{
    auto code = ReadFile(path);
    VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    i.codeSize = code.size();
    i.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule module;
    Check(vkCreateShaderModule(device, &i, nullptr, &module));
    return module;
}
} // namespace
ScenePrepass::ScenePrepass(const PhysicalDevice &physical, const LogicalDevice &device, VkRenderPass mainPass,
                           VkExtent2D extent, const Buffer &camera, const Buffer &instances,
                           const std::vector<SubMesh> &subMeshes, bool verify, bool halfResolution,
                           const Buffer *deformedVertices, uint32_t previousOffset,
                           const GpuSkinning *verifier)
    : m_Instances(instances), m_Device(device), m_Extent(extent), m_Verify(verify), m_SubMeshes(subMeshes)
{
    m_PreviousVertexOffset = previousOffset;
    m_SkinningVerifier = verifier;
    m_HalfResolution = halfResolution;
    m_AoExtent = halfResolution ? VkExtent2D{AoHalfSize(extent.width), AoHalfSize(extent.height)} : extent;
    const auto d = device.GetHandle();
    const auto supports = [&](VkFormat f, VkFormatFeatureFlags needed) {
        VkFormatProperties p{};
        vkGetPhysicalDeviceFormatProperties(physical.GetHandle(), f, &p);
        return (p.optimalTilingFeatures & needed) == needed;
    };
    const auto colorFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                               VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    for (auto f : {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT})
        if (supports(f, colorFeatures))
        {
            m_NormalFormat = f;
            m_NormalBytes = f == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 16;
            break;
        }
    if (m_NormalFormat == VK_FORMAT_UNDEFINED ||
        !supports(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        throw std::runtime_error("Prepass requires sampled/attachment/transfer normal and D32 formats");
    if (!supports(VK_FORMAT_R32_SFLOAT, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        throw std::runtime_error("Raw AO requires sampled/storage/transfer R32F");
    if (!supports(VK_FORMAT_R32_UINT, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        throw std::runtime_error("AO representative map requires sampled/storage/transfer R32_UINT");
    if (!supports(VK_FORMAT_R32G32B32A32_SFLOAT, colorFeatures))
        throw std::runtime_error("Motion output requires RGBA32F sampled/color/transfer support");
    // A fixed progressive hemisphere kernel. No frame-dependent random seed.
    for (int i = 0; i < 64; ++i)
    {
        const float z = std::fmod((float(i) + .5f) * .754877666f, 1.f);
        const float angle = float(i) * 2.39996323f;
        const float radial = std::sqrt(1 - z * z);
        const float scale = .1f + .9f * std::pow(std::fmod((float(i) + .5f) * .569840296f, 1.f), 2.f);
        m_Current.kernel[i] =
            glm::vec4(glm::vec3(std::cos(angle) * radial, std::sin(angle) * radial, z) * scale, 0);
    }
    try
    {
        m_Blur = std::make_unique<Image>(physical, device, m_AoExtent.width, m_AoExtent.height,
                                         VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_Ao = std::make_unique<Image>(physical, device, m_AoExtent.width, m_AoExtent.height,
                                       VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        const auto computeUsage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        m_Guide = std::make_unique<Image>(physical, device, m_AoExtent.width, m_AoExtent.height,
                                          VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, computeUsage,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_Resolved = std::make_unique<Image>(physical, device, extent.width, extent.height,
                                             VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, computeUsage,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_Normal = std::make_unique<Image>(physical, device, extent.width, extent.height, m_NormalFormat,
                                           VK_IMAGE_TILING_OPTIMAL,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_Depth = std::make_unique<Image>(physical, device, extent.width, extent.height, VK_FORMAT_D32_SFLOAT,
                                          VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        m_Motion = std::make_unique<Image>(physical, device, extent.width, extent.height,
                                           VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_MotionUniform = std::make_unique<Buffer>(
            physical, device, sizeof(MotionState), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_PreviousInstanceBuffer = std::make_unique<Buffer>(
            physical, device, instances.GetSize(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_CurrentInstances.resize(size_t(instances.GetSize()) / sizeof(InstanceData));
        m_PreviewUniform = std::make_unique<Buffer>(
            physical, device, sizeof(Preview), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (verify)
            m_Readback = std::make_unique<Buffer>(
                physical, device,
                VkDeviceSize(extent.width) * extent.height * (m_NormalBytes + 28) +
                    VkDeviceSize(m_AoExtent.width) * m_AoExtent.height * 12 + 16,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::array<VkAttachmentDescription, 3> attachments{};
        for (auto &a : attachments)
        {
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        attachments[0].format = m_NormalFormat;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[2].format = VK_FORMAT_D32_SFLOAT;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference colors[] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                          {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkAttachmentReference depth{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 2;
        sub.pColorAttachments = colors;
        sub.pDepthStencilAttachment = &depth;
        const auto attachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        const auto attachmentWrites =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkSubpassDependency deps[] = {
            {VK_SUBPASS_EXTERNAL, 0,
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
             attachmentStages, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, attachmentWrites, 0},
            {0, VK_SUBPASS_EXTERNAL, attachmentStages,
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
             attachmentWrites, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, 0}};
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 3;
        pass.pAttachments = attachments.data();
        pass.subpassCount = 1;
        pass.pSubpasses = &sub;
        pass.dependencyCount = 2;
        pass.pDependencies = deps;
        Check(vkCreateRenderPass(d, &pass, nullptr, &m_Pass));
        VkImageView views[] = {m_Normal->GetView(), m_Motion->GetView(), m_Depth->GetView()};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = m_Pass;
        fb.attachmentCount = 3;
        fb.pAttachments = views;
        fb.width = extent.width;
        fb.height = extent.height;
        fb.layers = 1;
        Check(vkCreateFramebuffer(d, &fb, nullptr, &m_Framebuffer));
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Check(vkCreateSampler(d, &sampler, nullptr, &m_Sampler));
        m_Temporal = std::make_unique<TemporalAo>(physical, device, extent, *m_Resolved, *m_Normal, *m_Depth,
                                                  *m_Motion, m_Sampler, verify);
        const VkDescriptorSetLayoutBinding sceneBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
        const auto readers = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        const VkDescriptorSetLayoutBinding viewBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, readers, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, readers, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, readers, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, readers, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        sl.bindingCount = 5;
        sl.pBindings = sceneBindings;
        Check(vkCreateDescriptorSetLayout(d, &sl, nullptr, &m_SceneSetLayout));
        sl.bindingCount = 14;
        sl.pBindings = viewBindings;
        Check(vkCreateDescriptorSetLayout(d, &sl, nullptr, &m_ViewSetLayout));
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 2;
        pool.poolSizeCount = 4;
        pool.pPoolSizes = sizes;
        Check(vkCreateDescriptorPool(d, &pool, nullptr, &m_Pool));
        VkDescriptorSetLayout layouts[] = {m_SceneSetLayout, m_ViewSetLayout};
        VkDescriptorSet sets[2];
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_Pool;
        alloc.descriptorSetCount = 2;
        alloc.pSetLayouts = layouts;
        Check(vkAllocateDescriptorSets(d, &alloc, sets));
        m_SceneSet = sets[0];
        m_ViewSet = sets[1];
        VkDescriptorBufferInfo bufferInfo[] = {
            {camera.GetHandle(), 0, sizeof(UniformBufferObject)},
            {instances.GetHandle(), 0, instances.GetSize()},
            {m_PreviewUniform->GetHandle(), 0, sizeof(Preview)},
            {m_MotionUniform->GetHandle(), 0, sizeof(MotionState)},
            {m_PreviousInstanceBuffer->GetHandle(), 0, m_PreviousInstanceBuffer->GetSize()},
            {deformedVertices ? deformedVertices->GetHandle() : instances.GetHandle(), 0,
             deformedVertices ? deformedVertices->GetSize() : instances.GetSize()}};
        VkDescriptorImageInfo images[] = {
            {m_Sampler, m_Normal->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_Sampler, m_Depth->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, m_Ao->GetView(), VK_IMAGE_LAYOUT_GENERAL},
            {m_Sampler, m_Ao->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, m_Blur->GetView(), VK_IMAGE_LAYOUT_GENERAL},
            {m_Sampler, m_Blur->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, m_Guide->GetView(), VK_IMAGE_LAYOUT_GENERAL},
            {m_Sampler, m_Guide->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, m_Resolved->GetView(), VK_IMAGE_LAYOUT_GENERAL},
            {m_Sampler, m_Resolved->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_Sampler, m_Motion->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_Sampler, m_Temporal->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_Sampler, m_Temporal->DebugView(), VK_IMAGE_LAYOUT_GENERAL}};
        std::vector<VkWriteDescriptorSet> writes;
        const auto addBuffer = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                                   const VkDescriptorBufferInfo *info) {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.dstBinding = binding;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pBufferInfo = info;
            writes.push_back(w);
        };
        addBuffer(m_SceneSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo[0]);
        addBuffer(m_SceneSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufferInfo[1]);
        addBuffer(m_SceneSet, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo[3]);
        addBuffer(m_SceneSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufferInfo[4]);
        addBuffer(m_SceneSet, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufferInfo[5]);
        addBuffer(m_ViewSet, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfo[2]);
        const uint32_t imageBindings[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
        for (size_t i = 0; i < 13; ++i)
        {
            const auto binding = imageBindings[i];
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = m_ViewSet;
            w.dstBinding = binding;
            w.descriptorCount = 1;
            w.descriptorType = (binding == 3 || binding == 5 || binding == 7 || binding == 9)
                                   ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                   : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &images[i];
            writes.push_back(w);
        }
        vkUpdateDescriptorSets(d, uint32_t(writes.size()), writes.data(), 0, nullptr);
        for (int preview = 0; preview < 3; ++preview)
        {
            auto &pipelineLayout = preview ? m_ViewLayout : m_SceneLayout;
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layout.setLayoutCount = 1;
            layout.pSetLayouts = &layouts[preview ? 1 : 0];
            if (preview < 2)
                Check(vkCreatePipelineLayout(d, &layout, nullptr, &pipelineLayout));
            std::array<VkShaderModule, 2> modules{};
            try
            {
                modules[0] = Load(d, preview ? "build/shaders/prepass_view.vert.spv"
                                             : "build/shaders/prepass.vert.spv");
                modules[1] = Load(d, preview == 2 ? "build/shaders/ssao_view.frag.spv"
                                     : preview    ? "build/shaders/prepass_view.frag.spv"
                                                  : "build/shaders/prepass.frag.spv");
                VkPipelineShaderStageCreateInfo stages[2]{};
                for (auto &s : stages)
                {
                    s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                    s.pName = "main";
                }
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = modules[0];
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = modules[1];
                const auto binding = Vertex::GetBindingDescription();
                const auto all = Vertex::GetAttributeDescriptions();
                const VkVertexInputAttributeDescription attributes[] = {all[0], all[2]};
                VkPipelineVertexInputStateCreateInfo vertex{
                    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
                if (!preview)
                {
                    vertex.vertexBindingDescriptionCount = 1;
                    vertex.pVertexBindingDescriptions = &binding;
                    vertex.vertexAttributeDescriptionCount = 2;
                    vertex.pVertexAttributeDescriptions = attributes;
                }
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
                raster.cullMode = preview ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
                raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                raster.lineWidth = 1;
                VkPipelineMultisampleStateCreateInfo samples{
                    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
                samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                VkPipelineDepthStencilStateCreateInfo test{
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
                test.depthTestEnable = test.depthWriteEnable = preview ? VK_FALSE : VK_TRUE;
                test.depthCompareOp = VK_COMPARE_OP_LESS;
                VkPipelineColorBlendAttachmentState attachment{};
                attachment.colorWriteMask = 15;
                VkPipelineColorBlendStateCreateInfo blend{
                    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
                const VkPipelineColorBlendAttachmentState blendAttachments[] = {attachment, attachment};
                blend.attachmentCount = preview ? 1 : 2;
                blend.pAttachments = blendAttachments;
                VkGraphicsPipelineCreateInfo graphics{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
                graphics.stageCount = 2;
                graphics.pStages = stages;
                graphics.layout = pipelineLayout;
                graphics.renderPass = preview ? mainPass : m_Pass;
                graphics.pVertexInputState = &vertex;
                graphics.pInputAssemblyState = &assembly;
                graphics.pViewportState = &view;
                graphics.pRasterizationState = &raster;
                graphics.pMultisampleState = &samples;
                graphics.pDepthStencilState = &test;
                graphics.pColorBlendState = &blend;
                auto result = vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &graphics, nullptr,
                                                        preview == 2 ? &m_AoViewPipeline
                                                        : preview    ? &m_ViewPipeline
                                                                     : &m_ScenePipeline);
                Check(result);
                if (preview == 0)
                {
                    raster.cullMode = VK_CULL_MODE_NONE;
                    Check(vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &graphics, nullptr,
                                                    &m_DoublePipeline));
                }
            }
            catch (...)
            {
                for (auto module : modules)
                    if (module)
                        vkDestroyShaderModule(d, module, nullptr);
                throw;
            }
            for (auto module : modules)
                vkDestroyShaderModule(d, module, nullptr);
        }
        for (int blur = 0; blur < 3; ++blur)
        {
            auto module = Load(d, blur == 2 ? "build/shaders/ssao_resolve.comp.spv"
                                  : blur    ? "build/shaders/ssao_blur.comp.spv"
                                            : "build/shaders/ssao.comp.spv");
            VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            compute.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            compute.stage.module = module;
            compute.stage.pName = "main";
            compute.layout = m_ViewLayout;
            auto result = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &compute, nullptr,
                                                   blur == 2 ? &m_ResolvePipeline
                                                   : blur    ? &m_BlurPipeline
                                                             : &m_AoPipeline);
            vkDestroyShaderModule(d, module, nullptr);
            Check(result);
        }
        std::cout << "[Scene Prepass] " << extent.width << 'x' << extent.height << " AO " << m_AoExtent.width
                  << 'x' << m_AoExtent.height << " normal bytes " << m_NormalBytes << " | one frame in flight"
                  << std::endl;
    }
    catch (...)
    {
        Release();
        throw;
    }
}
ScenePrepass::~ScenePrepass()
{
    Release();
}
void ScenePrepass::Release()
{
    auto d = m_Device.GetHandle();
    m_Temporal.reset();
    if (m_AoViewPipeline)
        vkDestroyPipeline(d, m_AoViewPipeline, nullptr);
    if (m_ResolvePipeline)
        vkDestroyPipeline(d, m_ResolvePipeline, nullptr);
    if (m_BlurPipeline)
        vkDestroyPipeline(d, m_BlurPipeline, nullptr);
    if (m_AoPipeline)
        vkDestroyPipeline(d, m_AoPipeline, nullptr);
    if (m_ViewPipeline)
        vkDestroyPipeline(d, m_ViewPipeline, nullptr);
    if (m_DoublePipeline)
        vkDestroyPipeline(d, m_DoublePipeline, nullptr);
    if (m_ScenePipeline)
        vkDestroyPipeline(d, m_ScenePipeline, nullptr);
    if (m_ViewLayout)
        vkDestroyPipelineLayout(d, m_ViewLayout, nullptr);
    if (m_SceneLayout)
        vkDestroyPipelineLayout(d, m_SceneLayout, nullptr);
    if (m_Pool)
        vkDestroyDescriptorPool(d, m_Pool, nullptr);
    if (m_ViewSetLayout)
        vkDestroyDescriptorSetLayout(d, m_ViewSetLayout, nullptr);
    if (m_SceneSetLayout)
        vkDestroyDescriptorSetLayout(d, m_SceneSetLayout, nullptr);
    if (m_Framebuffer)
        vkDestroyFramebuffer(d, m_Framebuffer, nullptr);
    if (m_Pass)
        vkDestroyRenderPass(d, m_Pass, nullptr);
    if (m_Sampler)
        vkDestroySampler(d, m_Sampler, nullptr);
}
void ScenePrepass::SetMode(int mode)
{
    if (mode < 0 || mode > 8)
        throw std::runtime_error("Invalid prepass mode");
    if (mode == 0 || m_Mode == 0)
        m_HistoryValid = false;
    if (mode != m_Mode)
        m_Temporal->Reset();
    m_Mode = mode;
}
void ScenePrepass::SetDepthRange(float value)
{
    if (!std::isfinite(value) || value <= 0)
        throw std::runtime_error("Depth display range must be positive");
    m_Range = value;
}
void ScenePrepass::SetAo(float radius, float bias, int samples)
{
    if (!std::isfinite(radius) || radius <= 0 || radius > 10 || !std::isfinite(bias) || bias < 0 ||
        bias >= radius || samples < 1 || samples > 64)
        throw std::runtime_error("AO requires radius in (0,10], bias in [0,radius), samples 1..64");
    if (radius != m_AoRadius || bias != m_AoBias || samples != m_AoSamples)
        m_Temporal->Reset();
    m_AoRadius = radius;
    m_AoBias = bias;
    m_AoSamples = samples;
}
void ScenePrepass::Update(const UniformBufferObject &camera, bool compareMain)
{
    ReadCompleted();
    m_Compare = compareMain;
    m_CurrentCamera = camera;
    if (Enabled())
    {
        m_Instances.DownloadData(m_CurrentInstances.data(), m_Instances.GetSize());
        const auto &previous = m_HistoryValid ? m_PreviousInstances : m_CurrentInstances;
        m_PreviousInstanceBuffer->UploadData(previous.data(), m_Instances.GetSize());
        m_MotionCurrent.previousViewProjection =
            m_HistoryValid ? m_PreviousCamera.proj * m_PreviousCamera.view : camera.proj * camera.view;
        m_MotionCurrent.previousGlobalModel = m_HistoryValid ? m_PreviousCamera.model : camera.model;
        m_MotionCurrent.settings =
            glm::ivec4(m_HistoryValid ? 1 : 0, m_Extent.width, m_Extent.height, m_PreviousVertexOffset);
        m_MotionUniform->UploadData(&m_MotionCurrent, sizeof(m_MotionCurrent));
    }
    m_Current.inverseProjection = glm::inverse(camera.proj);
    m_Current.inverseView = glm::inverse(camera.view);
    m_Current.settings = glm::ivec4(m_Mode, m_Extent.width, m_Extent.height, m_AoSamples);
    m_Current.parameters = glm::vec4(m_Range, m_AoRadius, m_AoBias, m_PlaneAwareBlur ? 1.f : 0.f);
    m_Current.projection = camera.proj;
    m_Current.aoSettings =
        glm::ivec4(m_AoExtent.width, m_AoExtent.height, m_HalfResolution ? 1 : 0, TemporalEnabled() ? 1 : 0);
    m_Current.sampling =
        glm::ivec4(m_VaryingSampling ? m_SamplePhase : 0, m_VaryingSampling ? 1 : 0, m_TemporalDebug, 0);
    m_PreviewUniform->UploadData(&m_Current, sizeof(m_Current));
    m_Temporal->Update(camera, m_PreviousCamera, m_HistoryValid && AoEnabled());
}
void ScenePrepass::Record(VkCommandBuffer cmd, const Buffer &vertices, const Buffer &indices) const
{
    VkClearValue clear[3]{};
    clear[2].depthStencil = {1, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_Pass;
    begin.framebuffer = m_Framebuffer;
    begin.renderArea.extent = m_Extent;
    begin.clearValueCount = 3;
    begin.pClearValues = clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ScenePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SceneLayout, 0, 1, &m_SceneSet, 0,
                            nullptr);
    VkBuffer buffer = vertices.GetHandle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
    vkCmdBindIndexBuffer(cmd, indices.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
    for (const auto &sub : m_SubMeshes)
    {
        if (sub.transparent)
            continue;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          sub.doubleSided ? m_DoublePipeline : m_ScenePipeline);
        vkCmdDrawIndexed(cmd, sub.indexCount, sub.instanceCount, sub.firstIndex, 0, sub.firstInstance);
    }
    vkCmdEndRenderPass(cmd);
}
void ScenePrepass::RecordAo(VkCommandBuffer cmd) const
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = m_Ao->GetHandle();
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    auto guide = b;
    guide.image = m_Guide->GetHandle();
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &guide);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_AoPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ViewLayout, 0, 1, &m_ViewSet, 0, nullptr);
    vkCmdDispatch(cmd, (m_AoExtent.width + 7) / 8, (m_AoExtent.height + 7) / 8, 1);
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    guide.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    guide.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    guide.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    guide.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &guide);
}
void ScenePrepass::RecordBlur(VkCommandBuffer cmd) const
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = m_Blur->GetHandle();
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BlurPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ViewLayout, 0, 1, &m_ViewSet, 0, nullptr);
    vkCmdDispatch(cmd, (m_AoExtent.width + 7) / 8, (m_AoExtent.height + 7) / 8, 1);
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
}
void ScenePrepass::RecordResolve(VkCommandBuffer cmd) const
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = m_Resolved->GetHandle();
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ResolvePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ViewLayout, 0, 1, &m_ViewSet, 0, nullptr);
    vkCmdDispatch(cmd, (m_Extent.width + 7) / 8, (m_Extent.height + 7) / 8, 1);
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
}
void ScenePrepass::RecordPreview(VkCommandBuffer cmd) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      (m_Mode == 5 || m_Mode == 6) ? m_AoViewPipeline : m_ViewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ViewLayout, 0, 1, &m_ViewSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
void ScenePrepass::RecordReadback(VkCommandBuffer cmd, VkImage mainDepth) const
{
    if (!m_Verify)
        return;
    const VkImage images[] = {m_Normal->GetHandle(),   m_Depth->GetHandle(), mainDepth,
                              m_Ao->GetHandle(),       m_Blur->GetHandle(),  m_Guide->GetHandle(),
                              m_Resolved->GetHandle(), m_Motion->GetHandle()};
    const VkImageLayout layouts[] = {
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDeviceSize offset = 0;
    const VkDeviceSize motionOffset = (VkDeviceSize(m_Extent.width) * m_Extent.height * (m_NormalBytes + 12) +
                                       VkDeviceSize(m_AoExtent.width) * m_AoExtent.height * 12 + 15) &
                                      ~VkDeviceSize(15);
    for (int i = 0; i < 8; ++i)
    {
        if (i >= 3 && i <= 6 && !AoEnabled())
            continue;
        if (i == 7)
            offset = motionOffset;
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image = images[i];
        b.oldLayout = layouts[i];
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {
            VkImageAspectFlags((i == 0 || i >= 3) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT), 0,
            1, 0, 1};
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy copy{};
        copy.bufferOffset = offset;
        copy.imageSubresource = {b.subresourceRange.aspectMask, 0, 0, 1};
        const auto e = (i >= 3 && i <= 5) ? m_AoExtent : m_Extent;
        copy.imageExtent = {e.width, e.height, 1};
        vkCmdCopyImageToBuffer(cmd, images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_Readback->GetHandle(),
                               1, &copy);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = layouts[i];
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = i == 2 ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
        offset += VkDeviceSize(e.width) * e.height * (i == 0 ? m_NormalBytes : i == 7 ? 16 : 4);
    }
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                         nullptr, 0, nullptr);
}
void ScenePrepass::MarkSubmitted()
{
    m_Temporal->MarkSubmitted(AoEnabled());
    if (AoEnabled())
        m_SamplePhase = (m_SamplePhase + 1) % 64;
    if (!Enabled())
    {
        m_HistoryValid = false;
        return;
    }
    if (m_Verify)
    {
        m_Pending = true;
        m_Last = m_Current;
        m_LastCompare = m_Compare;
        m_MotionLast = m_MotionCurrent;
        m_LastCamera = m_CurrentCamera;
        m_LastCurrentInstances = m_CurrentInstances;
        m_LastPreviousInstances = m_HistoryValid ? m_PreviousInstances : m_CurrentInstances;
    }
    m_PreviousCamera = m_CurrentCamera;
    m_PreviousInstances = m_CurrentInstances;
    m_HistoryValid = true;
}
void ScenePrepass::ReadCompleted()
{
    m_Temporal->ReadCompleted();
    if (!m_Pending)
        return;
    m_Pending = false;
    const size_t pixels = size_t(m_Extent.width) * m_Extent.height;
    const size_t lowPixels = size_t(m_AoExtent.width) * m_AoExtent.height;
    std::vector<unsigned char> data(size_t(m_Readback->GetSize()));
    m_Readback->DownloadData(data.data(), data.size());
    const size_t depthOffset = pixels * m_NormalBytes, mainOffset = depthOffset + pixels * 4;
    const size_t rawOffset = mainOffset + pixels * 4, blurOffset = rawOffset + lowPixels * 4;
    const size_t guideOffset = blurOffset + lowPixels * 4, resolveOffset = guideOffset + lowPixels * 4;
    const auto value = [&](size_t offset, size_t i) {
        float f;
        std::memcpy(&f, data.data() + offset + i * 4, 4);
        return f;
    };
    const auto guideAt = [&](size_t i) {
        uint32_t q;
        std::memcpy(&q, data.data() + guideOffset + i * 4, 4);
        return size_t(q);
    };
    const auto normalAt = [&](size_t i) {
        glm::vec4 n;
        if (m_NormalBytes == 8)
        {
            glm::uint16 h[4];
            std::memcpy(h, data.data() + i * 8, 8);
            for (int j = 0; j < 4; ++j)
                n[j] = glm::unpackHalf1x16(h[j]);
        }
        else
            std::memcpy(&n, data.data() + i * 16, 16);
        return n;
    };
    const auto positionAt = [&](size_t i) {
        glm::vec2 uv((float(i % m_Extent.width) + .5f) / m_Extent.width,
                     (float(i / m_Extent.width) + .5f) / m_Extent.height);
        auto p = m_Last.inverseProjection * glm::vec4(uv * 2.f - 1.f, value(depthOffset, i), 1);
        return glm::vec3(p) / p.w;
    };
    const auto validAo = [](float ao, bool background) {
        return std::isfinite(ao) && ao >= 0 && ao <= 1 && (!background || ao == 1.f);
    };
    const size_t motionOffset = (resolveOffset + pixels * 4 + 15) & ~size_t(15);
    std::vector<glm::mat4> previousFromWorld(m_LastCurrentInstances.size());
    for (size_t i = 0; i < previousFromWorld.size(); ++i)
        previousFromWorld[i] = m_MotionLast.previousViewProjection * m_MotionLast.previousGlobalModel *
                               m_LastPreviousInstances[i].model *
                               glm::inverse(m_LastCamera.model * m_LastCurrentInstances[i].model);
    size_t motionCandidates = 0, motionInvalid = 0;
    float maxMotion = 0, maxMotionError = 0;
    std::vector<GpuSkinning::MotionSample> skinSamples;
    size_t covered = 0;
    float maxDepthError = 0;
    for (size_t i = 0; i < pixels; ++i)
    {
        const auto normal = normalAt(i);
        const float depth = value(depthOffset, i), main = value(mainOffset, i);
        if (!std::isfinite(depth) || depth < 0 || depth > 1)
            throw std::runtime_error("Prepass device depth invalid");
        const bool geometry = normal.w > 0;
        if (geometry != (depth < 1))
            throw std::runtime_error("Prepass normal/depth coverage mismatch");
        if (m_LastCompare && ((depth < 1) != (main < 1) || std::abs(depth - main) > 1e-5f))
            throw std::runtime_error("Prepass/main depth mismatch");
        maxDepthError = std::max(maxDepthError, std::abs(depth - main));
        if ((m_Last.settings.x >= 5 && m_Last.settings.x <= 7) &&
            !validAo(value(resolveOffset, i), !geometry))
            throw std::runtime_error("Resolved AO invalid/background not white");
        glm::vec4 motion;
        std::memcpy(&motion, data.data() + motionOffset + i * 16, 16);
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(motion[c]))
                throw std::runtime_error("Motion output not finite");
        if (!geometry)
        {
            if (motion != glm::vec4(0))
                throw std::runtime_error("Motion background not zero");
        }
        else
        {
            const float identity = std::abs(motion.w);
            if (identity < 1 || identity > float(previousFromWorld.size()) ||
                std::floor(identity) != identity)
                throw std::runtime_error("Motion instance identity invalid");
            const size_t id = size_t(identity) - 1;
            const auto world = m_Last.inverseView * glm::vec4(positionAt(i), 1);
            const glm::vec2 uv((float(i % m_Extent.width) + .5f) / m_Extent.width,
                               (float(i / m_Extent.width) + .5f) / m_Extent.height);
            float error = 0;
            bool referenceCandidate = false;
            if (!m_PreviousVertexOffset)
            {
                const auto expected =
                    ProjectPreviousMotion(uv, previousFromWorld[id] * world, m_MotionLast.settings.x != 0);
                const auto pixelsError = glm::abs((glm::vec2(motion) - expected.delta) *
                                                  glm::vec2(m_Extent.width, m_Extent.height));
                error = std::max(pixelsError.x, pixelsError.y);
                referenceCandidate = expected.candidate;
                if ((expected.candidate && motion.w > 0 &&
                     (error > .05f || std::abs(motion.z - expected.previousDepth) > .0005f)))
                    throw std::runtime_error("GPU motion disagrees with previous-transform CPU projection");
                const auto previousUv = uv + expected.delta;
                const float border = std::min(std::min(std::abs(previousUv.x), std::abs(1 - previousUv.x)),
                                              std::min(std::abs(previousUv.y), std::abs(1 - previousUv.y)));
                if ((motion.w > 0) != expected.candidate && border > 1e-4f &&
                    std::abs(expected.previousDepth) > 1e-5f && std::abs(1 - expected.previousDepth) > 1e-5f)
                    throw std::runtime_error("Motion projection validity mismatch");
            }
            if (!m_MotionLast.settings.x &&
                (glm::vec2(motion) != glm::vec2(0) || motion.z != -1 || motion.w >= 0))
                throw std::runtime_error("Motion history reset not respected");
            if (motion.w > 0)
                ++motionCandidates;
            else
                ++motionInvalid;
            const auto movement = glm::abs(glm::vec2(motion) * glm::vec2(m_Extent.width, m_Extent.height));
            maxMotion = std::max(maxMotion, std::max(movement.x, movement.y));
            if (referenceCandidate && motion.w > 0)
                maxMotionError = std::max(maxMotionError, error);
        }
        if (geometry && m_SkinningVerifier && i % m_Extent.width % 23 == 11 && i / m_Extent.width % 19 == 9)
            skinSamples.push_back({glm::vec2((float(i % m_Extent.width) + .5f) / m_Extent.width,
                                             (float(i / m_Extent.width) + .5f) / m_Extent.height),
                                   depth, motion});
        if (!geometry)
            continue;
        ++covered;
        const float length = glm::length(glm::vec3(normal)), distance = -positionAt(i).z;
        if (!std::isfinite(length) || std::abs(length - 1) > .003f)
            throw std::runtime_error("Prepass normal not unit length");
        if (!std::isfinite(distance) || distance <= 0 ||
            std::abs(distance - normal.w) > std::max(.002f, distance * .004f))
            throw std::runtime_error("Prepass position reconstruction invalid");
    }
    if (m_SkinningVerifier)
        maxMotionError = m_SkinningVerifier->VerifyMotion(
            m_LastCamera.proj * m_LastCamera.view * m_LastCamera.model,
            m_MotionLast.previousViewProjection * m_MotionLast.previousGlobalModel,
            m_MotionLast.settings.x != 0, glm::vec2(m_Extent.width, m_Extent.height), skinSamples);
    if ((m_Last.settings.x >= 5 && m_Last.settings.x <= 7))
    {
        size_t lowCovered = 0;
        float rawMin = 1, rawMax = 0, blurMin = 1, blurMax = 0;
        double rawSum = 0, blurSum = 0;
        for (size_t i = 0; i < lowPixels; ++i)
        {
            const uint32_t x = uint32_t(i % m_AoExtent.width), y = uint32_t(i / m_AoExtent.width);
            const uint32_t factor = m_HalfResolution ? 2 : 1;
            size_t expected = size_t(y * factor) * m_Extent.width + x * factor;
            float nearest = 1;
            for (uint32_t dy = 0; dy < factor; ++dy)
                for (uint32_t dx = 0; dx < factor; ++dx)
                {
                    const uint32_t qx = x * factor + dx, qy = y * factor + dy;
                    if (qx >= m_Extent.width || qy >= m_Extent.height)
                        continue;
                    const size_t q = size_t(qy) * m_Extent.width + qx;
                    const float d = value(depthOffset, q);
                    if (d < nearest)
                    {
                        nearest = d;
                        expected = q;
                    }
                }
            const size_t guide = guideAt(i);
            if (guide >= pixels || guide != expected)
                throw std::runtime_error("AO representative map incorrect (including NPOT edge)");
            const bool background = normalAt(guide).w <= 0;
            const float raw = value(rawOffset, i), blur = value(blurOffset, i);
            if (!validAo(raw, background) || !validAo(blur, background))
                throw std::runtime_error("Raw/blurred AO invalid or background not white");
            if (background)
                continue;
            ++lowCovered;
            rawMin = std::min(rawMin, raw);
            rawMax = std::max(rawMax, raw);
            rawSum += raw;
            blurMin = std::min(blurMin, blur);
            blurMax = std::max(blurMax, blur);
            blurSum += blur;
        }
        float blurError = 0, resolveError = 0;
        size_t blurChecks = 0, resolveChecks = 0, fallbacks = 0;
        const double sigma = std::max(.002f, m_Last.parameters.y * .1f);
        for (int y = 0; y < int(m_AoExtent.height); y += 29)
            for (int x = 0; x < int(m_AoExtent.width); x += 37)
            {
                const size_t i = size_t(y) * m_AoExtent.width + x, g = guideAt(i);
                auto normal = normalAt(g);
                if (normal.w <= 0)
                    continue;
                const auto p = positionAt(g), n = glm::normalize(glm::vec3(normal));
                double sum = 0, weights = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int qx = x + dx, qy = y + dy;
                        if (qx < 0 || qy < 0 || qx >= int(m_AoExtent.width) || qy >= int(m_AoExtent.height))
                            continue;
                        const size_t q = size_t(qy) * m_AoExtent.width + qx, qg = guideAt(q);
                        auto other = normalAt(qg);
                        if (other.w <= 0)
                            continue;
                        const auto position = positionAt(qg), m = glm::normalize(glm::vec3(other));
                        if (m_Last.parameters.w > .5f && glm::distance(p, position) > m_Last.parameters.y)
                            continue;
                        const double dz = m_Last.parameters.w > .5f ? AoPlaneSeparation(p, n, position, m)
                                                                    : std::abs(position.z - p.z);
                        const double w = std::exp(-double(dx * dx + dy * dy) / 4.5) *
                                         std::exp(-.5 * dz * dz / (sigma * sigma)) *
                                         std::pow(std::max(glm::dot(n, m), 0.f), 32);
                        sum += value(rawOffset, q) * w;
                        weights += w;
                    }
                const float error = std::abs(float(sum / weights) - value(blurOffset, i));
                if (!std::isfinite(error) || error > .0003f)
                    throw std::runtime_error("GPU bilateral AO disagrees with CPU reference");
                blurError = std::max(blurError, error);
                ++blurChecks;
            }
        for (int y = 0; y < int(m_Extent.height); y += 29)
            for (int x = 0; x < int(m_Extent.width); x += 37)
            {
                const size_t i = size_t(y) * m_Extent.width + x;
                float expected = 1;
                auto normal = normalAt(i);
                if (!m_HalfResolution)
                    expected = value(blurOffset, i);
                else if (normal.w > 0)
                {
                    const auto p = positionAt(i), n = glm::normalize(glm::vec3(normal));
                    const glm::vec2 low(AoLowCoordinate(unsigned(x)), AoLowCoordinate(unsigned(y)));
                    const auto base = glm::ivec2(glm::floor(low));
                    double sum = 0, weights = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                        {
                            const glm::ivec2 q = base + glm::ivec2(dx, dy);
                            if (q.x < 0 || q.y < 0 || q.x >= int(m_AoExtent.width) ||
                                q.y >= int(m_AoExtent.height))
                                continue;
                            const size_t index = size_t(q.y) * m_AoExtent.width + q.x, g = guideAt(index);
                            auto other = normalAt(g);
                            if (other.w <= 0)
                                continue;
                            const auto position = positionAt(g), m = glm::normalize(glm::vec3(other));
                            const float dot = glm::dot(n, m), sep = AoPlaneSeparation(p, n, position, m);
                            if (dot < .8f || sep > 3 * sigma ||
                                glm::distance(p, position) > m_Last.parameters.y)
                                continue;
                            auto spatial =
                                glm::max(glm::vec2(0), glm::vec2(1) - glm::abs(low - glm::vec2(q)));
                            const double w = spatial.x * spatial.y *
                                             std::exp(-.5 * sep * sep / (sigma * sigma)) *
                                             std::pow(std::max(dot, 0.f), 32);
                            sum += value(blurOffset, index) * w;
                            weights += w;
                        }
                    if (weights > 1e-6)
                        expected = glm::clamp(float(sum / weights), 0.f, 1.f);
                    else
                        ++fallbacks;
                }
                const float error = std::abs(expected - value(resolveOffset, i));
                if (!std::isfinite(error) || error > .001f)
                    throw std::runtime_error("GPU AO resolve disagrees with CPU reference");
                resolveError = std::max(resolveError, error);
                ++resolveChecks;
            }
        double rawGradient = 0, blurGradient = 0;
        size_t pairs = 0;
        for (size_t i = 0; i < lowPixels; ++i)
        {
            if (i % m_AoExtent.width + 1 >= m_AoExtent.width)
                continue;
            const size_t g = guideAt(i), h = guideAt(i + 1);
            const auto a = normalAt(g), b = normalAt(h);
            if (a.w <= 0 || b.w <= 0)
                continue;
            const auto n = glm::normalize(glm::vec3(a)), m = glm::normalize(glm::vec3(b));
            const auto p = positionAt(g), q = positionAt(h);
            if (glm::dot(n, m) < .98f || AoPlaneSeparation(p, n, q, m) > .002f ||
                glm::distance(p, q) > m_Last.parameters.y)
                continue;
            rawGradient += std::abs(value(rawOffset, i) - value(rawOffset, i + 1));
            blurGradient += std::abs(value(blurOffset, i) - value(blurOffset, i + 1));
            ++pairs;
        }
        std::cout << std::defaultfloat << std::setprecision(8) << "[Blur Reference] " << blurChecks
                  << " pixels max error " << blurError << " guidance "
                  << (m_Last.parameters.w > .5f ? "plane" : "depth") << " | AO-grid neighbor differences raw "
                  << (pairs ? rawGradient / pairs : 0) << " blurred " << (pairs ? blurGradient / pairs : 0)
                  << " pairs " << pairs << std::endl;
        std::cout << "[Resolve Reference] " << resolveChecks << " pixels max error " << resolveError
                  << " sparse unmatched " << fallbacks << " | AO " << m_AoExtent.width << 'x'
                  << m_AoExtent.height << std::endl;
        std::cout << "[SSAO Verify] finite [0,1], background white | covered " << lowCovered << " min "
                  << rawMin << " max " << rawMax << " mean " << (lowCovered ? rawSum / lowCovered : 1)
                  << std::endl;
        std::cout << "[Blur Verify] finite [0,1], background white | min " << blurMin << " max " << blurMax
                  << " mean " << (lowCovered ? blurSum / lowCovered : 1) << std::endl;
    }
    std::cout << std::defaultfloat << std::setprecision(8)
              << "[Motion Verify] history=" << m_MotionLast.settings.x << " candidates=" << motionCandidates
              << " invalid=" << motionInvalid << " max_px=" << maxMotion << " max_error_px=" << maxMotionError
              << " reference=" << (m_PreviousVertexOffset ? "triangle samples" : "rigid pixels") << std::endl;
    std::cout << "[Prepass Verify] " << m_Extent.width << 'x' << m_Extent.height << " covered " << covered
              << " unit view normals + linear reconstruction OK | main depth "
              << (m_LastCompare ? "matched" : "skipped (frozen culling)") << " max error " << maxDepthError
              << std::endl;
}
