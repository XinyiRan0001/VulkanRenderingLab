#include "ScenePrepass.h"
#include "ShaderUtils.h"
#include "Vertex.h"
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/packing.hpp>
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
                           const std::vector<SubMesh> &subMeshes, bool verify)
    : m_Device(device), m_Extent(extent), m_Verify(verify), m_SubMeshes(subMeshes)
{
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
        m_Blur = std::make_unique<Image>(
            physical, device, extent.width, extent.height, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m_Ao = std::make_unique<Image>(
            physical, device, extent.width, extent.height, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
        m_PreviewUniform = std::make_unique<Buffer>(
            physical, device, sizeof(Preview), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (verify)
            m_Readback = std::make_unique<Buffer>(
                physical, device, VkDeviceSize(extent.width) * extent.height * (m_NormalBytes + 16),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::array<VkAttachmentDescription, 2> attachments{};
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
        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &color;
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
        pass.attachmentCount = 2;
        pass.pAttachments = attachments.data();
        pass.subpassCount = 1;
        pass.pSubpasses = &sub;
        pass.dependencyCount = 2;
        pass.pDependencies = deps;
        Check(vkCreateRenderPass(d, &pass, nullptr, &m_Pass));
        VkImageView views[] = {m_Normal->GetView(), m_Depth->GetView()};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = m_Pass;
        fb.attachmentCount = 2;
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
        const VkDescriptorSetLayoutBinding sceneBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
        const auto readers = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        const VkDescriptorSetLayoutBinding viewBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, readers, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, readers, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, readers, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, readers, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        sl.bindingCount = 2;
        sl.pBindings = sceneBindings;
        Check(vkCreateDescriptorSetLayout(d, &sl, nullptr, &m_SceneSetLayout));
        sl.bindingCount = 7;
        sl.pBindings = viewBindings;
        Check(vkCreateDescriptorSetLayout(d, &sl, nullptr, &m_ViewSetLayout));
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
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
        VkDescriptorBufferInfo bufferInfo[] = {{camera.GetHandle(), 0, sizeof(UniformBufferObject)},
                                               {instances.GetHandle(), 0, instances.GetSize()},
                                               {m_PreviewUniform->GetHandle(), 0, sizeof(Preview)}};
        VkDescriptorImageInfo images[] = {
            {m_Sampler, m_Normal->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {m_Sampler, m_Depth->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, m_Ao->GetView(), VK_IMAGE_LAYOUT_GENERAL},
            {m_Sampler, m_Ao->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, m_Blur->GetView(), VK_IMAGE_LAYOUT_GENERAL},
            {m_Sampler, m_Blur->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        std::array<VkWriteDescriptorSet, 9> writes{};
        for (uint32_t i = 0; i < 9; ++i)
        {
            auto &w = writes[i];
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = i < 2 ? m_SceneSet : m_ViewSet;
            w.dstBinding = i < 2 ? i : i - 2;
            w.descriptorCount = 1;
            if (i == 2 || i == 3 || i >= 5)
            {
                w.descriptorType = (i == 5 || i == 7) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                      : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &images[i >= 5 ? i - 3 : i - 2];
            }
            else
            {
                w.descriptorType =
                    i == 1 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &bufferInfo[i == 4 ? 2 : i];
            }
        }
        vkUpdateDescriptorSets(d, 9, writes.data(), 0, nullptr);
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
                blend.attachmentCount = 1;
                blend.pAttachments = &attachment;
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
        for (int blur = 0; blur < 2; ++blur)
        {
            auto module = Load(d, blur ? "build/shaders/ssao_blur.comp.spv" : "build/shaders/ssao.comp.spv");
            VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            compute.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            compute.stage.module = module;
            compute.stage.pName = "main";
            compute.layout = m_ViewLayout;
            auto result = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &compute, nullptr,
                                                   blur ? &m_BlurPipeline : &m_AoPipeline);
            vkDestroyShaderModule(d, module, nullptr);
            Check(result);
        }
        std::cout << "[Scene Prepass] " << extent.width << 'x' << extent.height << " normal bytes "
                  << m_NormalBytes << " | one frame in flight" << std::endl;
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
    if (m_AoViewPipeline)
        vkDestroyPipeline(d, m_AoViewPipeline, nullptr);
    if (m_BlurPipeline)
        vkDestroyPipeline(d, m_BlurPipeline, nullptr);
    if (m_AoPipeline)
        vkDestroyPipeline(d, m_AoPipeline, nullptr);
    if (m_ViewPipeline)
        vkDestroyPipeline(d, m_ViewPipeline, nullptr);
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
    if (mode < 0 || mode > 7)
        throw std::runtime_error("Invalid prepass mode");
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
    m_AoRadius = radius;
    m_AoBias = bias;
    m_AoSamples = samples;
}
void ScenePrepass::Update(const UniformBufferObject &camera, bool compareMain)
{
    ReadCompleted();
    m_Compare = compareMain;
    m_Current.inverseProjection = glm::inverse(camera.proj);
    m_Current.inverseView = glm::inverse(camera.view);
    m_Current.settings = glm::ivec4(m_Mode, m_Extent.width, m_Extent.height, m_AoSamples);
    m_Current.parameters = glm::vec4(m_Range, m_AoRadius, m_AoBias, 0);
    m_Current.projection = camera.proj;
    m_PreviewUniform->UploadData(&m_Current, sizeof(m_Current));
}
void ScenePrepass::Record(VkCommandBuffer cmd, const Buffer &vertices, const Buffer &indices) const
{
    VkClearValue clear[2]{};
    clear[1].depthStencil = {1, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_Pass;
    begin.framebuffer = m_Framebuffer;
    begin.renderArea.extent = m_Extent;
    begin.clearValueCount = 2;
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
        vkCmdDrawIndexed(cmd, sub.indexCount, sub.instanceCount, sub.firstIndex, 0, sub.firstInstance);
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_AoPipeline);
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
    const VkImage images[] = {m_Normal->GetHandle(), m_Depth->GetHandle(), mainDepth, m_Ao->GetHandle(),
                              m_Blur->GetHandle()};
    const VkImageLayout layouts[] = {
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDeviceSize offset = 0;
    for (int i = 0; i < (m_Mode >= 5 ? 5 : 3); ++i)
    {
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
        copy.imageExtent = {m_Extent.width, m_Extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_Readback->GetHandle(),
                               1, &copy);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = layouts[i];
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = i == 2 ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
        offset += VkDeviceSize(m_Extent.width) * m_Extent.height * (i == 0 ? m_NormalBytes : 4);
    }
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                         nullptr, 0, nullptr);
}
void ScenePrepass::MarkSubmitted()
{
    if (Enabled() && m_Verify)
    {
        m_Pending = true;
        m_Last = m_Current;
        m_LastCompare = m_Compare;
    }
}
void ScenePrepass::ReadCompleted()
{
    if (!m_Pending)
        return;
    m_Pending = false;
    const size_t pixels = size_t(m_Extent.width) * m_Extent.height;
    std::vector<unsigned char> data(size_t(m_Readback->GetSize()));
    m_Readback->DownloadData(data.data(), data.size());
    size_t covered = 0;
    float aoMin = 1, aoMax = 0, blurMin = 1, blurMax = 0;
    double aoSum = 0, blurSum = 0;
    float maxError = 0;
    for (size_t i = 0; i < pixels; ++i)
    {
        glm::vec4 normal;
        if (m_NormalBytes == 8)
        {
            glm::uint16 packed[4];
            std::memcpy(packed, data.data() + i * 8, 8);
            for (int j = 0; j < 4; ++j)
                normal[j] = glm::unpackHalf1x16(packed[j]);
        }
        else
            std::memcpy(&normal, data.data() + i * 16, 16);
        float depth, main;
        std::memcpy(&depth, data.data() + pixels * m_NormalBytes + i * 4, 4);
        std::memcpy(&main, data.data() + pixels * (m_NormalBytes + 4) + i * 4, 4);
        if (!std::isfinite(depth) || depth < 0 || depth > 1)
            throw std::runtime_error("Prepass device depth invalid");
        const bool geometry = normal.w > 0;
        if (m_Last.settings.x >= 5)
        {
            float ao;
            std::memcpy(&ao, data.data() + pixels * (m_NormalBytes + 8) + i * 4, 4);
            if (!std::isfinite(ao) || ao < 0 || ao > 1 || (!geometry && ao != 1.f))
                throw std::runtime_error("Raw AO invalid or background not white");
            float blurred;
            std::memcpy(&blurred, data.data() + pixels * (m_NormalBytes + 12) + i * 4, 4);
            if (!std::isfinite(blurred) || blurred < 0 || blurred > 1 || (!geometry && blurred != 1.f))
                throw std::runtime_error("Blurred AO invalid or background not white");
            if (geometry)
            {
                blurMin = std::min(blurMin, blurred);
                blurMax = std::max(blurMax, blurred);
                blurSum += blurred;
                aoMin = std::min(aoMin, ao);
                aoMax = std::max(aoMax, ao);
                aoSum += ao;
            }
        }

        if (geometry != (depth < 1))
            throw std::runtime_error("Prepass normal coverage differs from depth coverage");
        if (m_LastCompare && ((depth < 1) != (main < 1) || std::abs(depth - main) > 1e-5f))
            throw std::runtime_error("Prepass/main depth mismatch at pixel " + std::to_string(i) +
                                     " pre=" + std::to_string(depth) + " main=" + std::to_string(main));
        maxError = std::max(maxError, std::abs(depth - main));
        if (!geometry)
            continue;
        ++covered;
        const float length = glm::length(glm::vec3(normal));
        if (!std::isfinite(length) || std::abs(length - 1) > 0.003f)
            throw std::runtime_error("Prepass normal not unit length");
        const glm::vec2 uv((float(i % m_Extent.width) + .5f) / m_Extent.width,
                           (float(i / m_Extent.width) + .5f) / m_Extent.height);
        auto view = m_Last.inverseProjection * glm::vec4(uv * 2.f - 1.f, depth, 1);
        const float distance = -view.z / view.w;
        if (!std::isfinite(distance) || distance <= 0 ||
            std::abs(distance - normal.w) > std::max(.002f, distance * .004f))
            throw std::runtime_error("Reconstructed linear depth differs from view-space distance");
    }
    if (m_Last.settings.x >= 5)
    {
        // Independent CPU evaluation of the bilateral filter over actual GPU inputs.
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
        const auto valueAt = [&](size_t offset, size_t i) {
            float f;
            std::memcpy(&f, data.data() + pixels * offset + i * 4, 4);
            return f;
        };
        const auto linearAt = [&](int x, int y) {
            glm::vec2 uv((x + .5f) / m_Extent.width, (y + .5f) / m_Extent.height);
            auto v = m_Last.inverseProjection *
                     glm::vec4(uv * 2.f - 1.f, valueAt(m_NormalBytes, size_t(y) * m_Extent.width + x), 1);
            return -v.z / v.w;
        };
        float referenceError = 0;
        size_t checked = 0;
        const float sigma = std::max(.002f, m_Last.parameters.y * .1f);
        for (int y = 0; y < int(m_Extent.height); y += 29)
            for (int x = 0; x < int(m_Extent.width); x += 37)
            {
                const size_t i = size_t(y) * m_Extent.width + x;
                const auto center = normalAt(i);
                if (center.w <= 0)
                    continue;
                const auto n = glm::normalize(glm::vec3(center));
                const float z = linearAt(x, y);
                double sum = 0, weights = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const int qx = x + dx, qy = y + dy;
                        if (qx < 0 || qy < 0 || qx >= int(m_Extent.width) || qy >= int(m_Extent.height))
                            continue;
                        const size_t q = size_t(qy) * m_Extent.width + qx;
                        const auto other = normalAt(q);
                        if (other.w <= 0)
                            continue;
                        const double dz = linearAt(qx, qy) - z;
                        const double dot = std::max(0.f, glm::dot(n, glm::normalize(glm::vec3(other))));
                        const double w = std::exp(-double(dx * dx + dy * dy) / 4.5) *
                                         std::exp(-.5 * dz * dz / (sigma * sigma)) * std::pow(dot, 32);
                        sum += valueAt(m_NormalBytes + 8, q) * w;
                        weights += w;
                    }
                const float error = std::abs(float(sum / weights) - valueAt(m_NormalBytes + 12, i));
                if (!std::isfinite(error) || error > .0003f)
                    throw std::runtime_error("GPU bilateral AO disagrees with CPU reference");
                referenceError = std::max(referenceError, error);
                ++checked;
            }
        double rawGradient = 0, blurGradient = 0;
        size_t pairs = 0;
        for (size_t i = 0; i < pixels; ++i)
        {
            if (i % m_Extent.width + 1 >= m_Extent.width)
                continue;
            const auto a = normalAt(i), b = normalAt(i + 1);
            if (a.w <= 0 || b.w <= 0 || std::abs(a.w - b.w) > .01f ||
                glm::dot(glm::normalize(glm::vec3(a)), glm::normalize(glm::vec3(b))) < .98f)
                continue;
            rawGradient += std::abs(valueAt(m_NormalBytes + 8, i) - valueAt(m_NormalBytes + 8, i + 1));
            blurGradient += std::abs(valueAt(m_NormalBytes + 12, i) - valueAt(m_NormalBytes + 12, i + 1));
            ++pairs;
        }
        std::cout << "[Blur Reference] " << checked << " pixels max error " << referenceError
                  << " | smooth-surface neighbor differences raw " << (pairs ? rawGradient / pairs : 0)
                  << " blurred " << (pairs ? blurGradient / pairs : 0) << std::endl;
    }
    if (m_Last.settings.x >= 5)
        std::cout << "[SSAO Verify] finite [0,1], background white | covered " << covered << " min " << aoMin
                  << " max " << aoMax << " mean " << (covered ? aoSum / covered : 1) << std::endl;
    if (m_Last.settings.x >= 5)
        std::cout << "[Blur Verify] finite [0,1], background white | min " << blurMin << " max " << blurMax
                  << " mean " << (covered ? blurSum / covered : 1) << std::endl;
    std::cout << "[Prepass Verify] " << m_Extent.width << 'x' << m_Extent.height << " covered " << covered
              << " unit view normals + linear reconstruction OK | main depth "
              << (m_LastCompare ? "matched" : "skipped (frozen culling)") << " max error " << maxError
              << std::endl;
}
