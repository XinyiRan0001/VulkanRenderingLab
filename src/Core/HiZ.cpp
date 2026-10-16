#include "HiZ.h"
#include "ShaderUtils.h"
#include "Vertex.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace
{
void Check(VkResult result, const char *message)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(message);
}
VkShaderModule Shader(VkDevice device, const char *path)
{
    const auto code = ReadFile(path);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule module;
    Check(vkCreateShaderModule(device, &info, nullptr, &module), "Cannot load Hi-Z shader");
    return module;
}
} // namespace
HiZ::HiZ(const PhysicalDevice &physical, const LogicalDevice &device, VkExtent2D extent, const MeshData &mesh,
         bool verify)
    : m_Device(device), m_Extent(extent), m_Verify(verify)
{
    for (const auto &sub : mesh.subMeshes)
        if (sub.occluder)
        {
            if (sub.firstInstance != 0 || sub.instanceCount != 1)
                throw std::runtime_error(
                    "Hi-Z occluders must use baked static geometry and identity instance 0");
            m_Occluders.push_back(sub);
        }
    if (m_Occluders.empty())
        throw std::runtime_error("Hi-Z requires explicit opaque occluders");
    m_Padded = {HiZPowerOfTwo(extent.width), HiZPowerOfTwo(extent.height)};
    for (uint32_t n = std::max(m_Padded.width, m_Padded.height); n; n /= 2)
        ++m_Levels;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical.GetHandle(), VK_FORMAT_R32_SFLOAT, &properties);
    const auto features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & features) != features)
        throw std::runtime_error("R32F sampled/storage image is required for Hi-Z");
    vkGetPhysicalDeviceFormatProperties(physical.GetHandle(), VK_FORMAT_D32_SFLOAT, &properties);
    if ((properties.optimalTilingFeatures &
         (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) !=
        (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        throw std::runtime_error("Sampled D32 depth is required for Hi-Z");
    try
    {
        m_Depth = std::make_unique<Image>(
            physical, device, extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        m_Pyramid = std::make_unique<Image>(
            physical, device, m_Padded.width, m_Padded.height, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_Levels);
        m_Camera = std::make_unique<Buffer>(
            physical, device, sizeof(glm::mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceSize total = 0;
        for (uint32_t level = 0; level < m_Levels; ++level)
        {
            VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            info.image = m_Pyramid->GetHandle();
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = VK_FORMAT_R32_SFLOAT;
            info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
            VkImageView view;
            Check(vkCreateImageView(device.GetHandle(), &info, nullptr, &view),
                  "Cannot create Hi-Z mip view");
            m_MipViews.push_back(view);
            m_Offsets.push_back(total);
            total += VkDeviceSize(std::max(1u, m_Padded.width >> level)) *
                     std::max(1u, m_Padded.height >> level) * 4;
        }
        if (verify)
            m_Readback = std::make_unique<Buffer>(physical, device, total, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = float(m_Levels - 1);
        Check(vkCreateSampler(device.GetHandle(), &sampler, nullptr, &m_Sampler),
              "Cannot create Hi-Z sampler");

        VkAttachmentDescription depth{};
        depth.format = VK_FORMAT_D32_SFLOAT;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.pDepthStencilAttachment = &ref;
        VkSubpassDependency dependencies[2]{};
        dependencies[0] = {VK_SUBPASS_EXTERNAL,
                           0,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           0};
        dependencies[1] = {0,
                           VK_SUBPASS_EXTERNAL,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           0};
        VkRenderPassCreateInfo renderPass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPass.attachmentCount = 1;
        renderPass.pAttachments = &depth;
        renderPass.subpassCount = 1;
        renderPass.pSubpasses = &sub;
        renderPass.dependencyCount = 2;
        renderPass.pDependencies = dependencies;
        Check(vkCreateRenderPass(device.GetHandle(), &renderPass, nullptr, &m_RenderPass),
              "Cannot create occluder depth pass");
        auto depthView = m_Depth->GetView();
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = m_RenderPass;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &depthView;
        framebuffer.width = extent.width;
        framebuffer.height = extent.height;
        framebuffer.layers = 1;
        Check(vkCreateFramebuffer(device.GetHandle(), &framebuffer, nullptr, &m_Framebuffer),
              "Cannot create occluder framebuffer");

        VkDescriptorSetLayoutBinding camera{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                            VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = 1;
        setInfo.pBindings = &camera;
        Check(vkCreateDescriptorSetLayout(device.GetHandle(), &setInfo, nullptr, &m_DepthSetLayout),
              "Cannot create depth descriptor layout");
        const VkDescriptorSetLayoutBinding reduceBindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        setInfo.bindingCount = 2;
        setInfo.pBindings = reduceBindings;
        Check(vkCreateDescriptorSetLayout(device.GetHandle(), &setInfo, nullptr, &m_ReduceSetLayout),
              "Cannot create Hi-Z reduction layout");
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_Levels},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_Levels}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = m_Levels + 1;
        pool.poolSizeCount = 3;
        pool.pPoolSizes = sizes;
        Check(vkCreateDescriptorPool(device.GetHandle(), &pool, nullptr, &m_Pool),
              "Cannot create Hi-Z descriptor pool");
        std::vector<VkDescriptorSetLayout> layouts(m_Levels + 1, m_ReduceSetLayout);
        layouts[0] = m_DepthSetLayout;
        std::vector<VkDescriptorSet> sets(layouts.size());
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = m_Pool;
        allocate.descriptorSetCount = uint32_t(layouts.size());
        allocate.pSetLayouts = layouts.data();
        Check(vkAllocateDescriptorSets(device.GetHandle(), &allocate, sets.data()),
              "Cannot allocate Hi-Z descriptors");
        m_DepthSet = sets[0];
        m_ReduceSets.assign(sets.begin() + 1, sets.end());
        VkDescriptorBufferInfo cameraInfo{m_Camera->GetHandle(), 0, sizeof(glm::mat4)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_DepthSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &cameraInfo;
        vkUpdateDescriptorSets(device.GetHandle(), 1, &write, 0, nullptr);
        for (uint32_t level = 0; level < m_Levels; ++level)
        {
            VkDescriptorImageInfo source{m_Sampler, level ? m_MipViews[level - 1] : m_Depth->GetView(),
                                         level ? VK_IMAGE_LAYOUT_GENERAL
                                               : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            VkDescriptorImageInfo target{VK_NULL_HANDLE, m_MipViews[level], VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet writes[2]{};
            for (uint32_t j = 0; j < 2; ++j)
            {
                writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[j].dstSet = m_ReduceSets[level];
                writes[j].dstBinding = j;
                writes[j].descriptorCount = 1;
            }
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &source;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &target;
            vkUpdateDescriptorSets(device.GetHandle(), 2, writes, 0, nullptr);
        }
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &m_DepthSetLayout;
        Check(vkCreatePipelineLayout(device.GetHandle(), &pipelineLayout, nullptr, &m_DepthLayout),
              "Cannot create occluder pipeline layout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
        pipelineLayout.pSetLayouts = &m_ReduceSetLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &range;
        Check(vkCreatePipelineLayout(device.GetHandle(), &pipelineLayout, nullptr, &m_ReduceLayout),
              "Cannot create reduction pipeline layout");

        auto module = Shader(device.GetHandle(), "build/shaders/hiz_reduce.comp.spv");
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.layout = m_ReduceLayout;
        compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module = module;
        compute.stage.pName = "main";
        auto result = vkCreateComputePipelines(device.GetHandle(), VK_NULL_HANDLE, 1, &compute, nullptr,
                                               &m_ReducePipeline);
        vkDestroyShaderModule(device.GetHandle(), module, nullptr);
        Check(result, "Cannot create Hi-Z reduction pipeline");
        module = Shader(device.GetHandle(), "build/shaders/occluder_depth.vert.spv");
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = module;
        stage.pName = "main";
        const auto binding = Vertex::GetBindingDescription();
        const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                                          offsetof(Vertex, position)};
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = 1;
        vertex.pVertexAttributeDescriptions = &attribute;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo test{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        test.depthTestEnable = test.depthWriteEnable = VK_TRUE;
        test.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo graphics{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        graphics.stageCount = 1;
        graphics.pStages = &stage;
        graphics.layout = m_DepthLayout;
        graphics.renderPass = m_RenderPass;
        graphics.pVertexInputState = &vertex;
        graphics.pInputAssemblyState = &assembly;
        graphics.pViewportState = &viewport;
        graphics.pRasterizationState = &raster;
        graphics.pMultisampleState = &samples;
        graphics.pDepthStencilState = &test;
        graphics.pColorBlendState = &blend;
        graphics.pDynamicState = &dynamic;
        result = vkCreateGraphicsPipelines(device.GetHandle(), VK_NULL_HANDLE, 1, &graphics, nullptr,
                                           &m_DepthPipeline);
        vkDestroyShaderModule(device.GetHandle(), module, nullptr);
        Check(result, "Cannot create occluder depth pipeline");
    }
    catch (...)
    {
        Release();
        throw;
    }
}
HiZ::~HiZ()
{
    Release();
}
void HiZ::Release()
{
    const auto device = m_Device.GetHandle();
    if (m_DepthPipeline)
        vkDestroyPipeline(device, m_DepthPipeline, nullptr);
    if (m_ReducePipeline)
        vkDestroyPipeline(device, m_ReducePipeline, nullptr);
    if (m_DepthLayout)
        vkDestroyPipelineLayout(device, m_DepthLayout, nullptr);
    if (m_ReduceLayout)
        vkDestroyPipelineLayout(device, m_ReduceLayout, nullptr);
    if (m_Framebuffer)
        vkDestroyFramebuffer(device, m_Framebuffer, nullptr);
    if (m_RenderPass)
        vkDestroyRenderPass(device, m_RenderPass, nullptr);
    if (m_Pool)
        vkDestroyDescriptorPool(device, m_Pool, nullptr);
    if (m_DepthSetLayout)
        vkDestroyDescriptorSetLayout(device, m_DepthSetLayout, nullptr);
    if (m_ReduceSetLayout)
        vkDestroyDescriptorSetLayout(device, m_ReduceSetLayout, nullptr);
    for (auto view : m_MipViews)
        vkDestroyImageView(device, view, nullptr);
    if (m_Sampler)
        vkDestroySampler(device, m_Sampler, nullptr);
}
void HiZ::Update(const UniformBufferObject &ubo)
{
    auto matrix = ubo.proj * ubo.view * ubo.model;
    m_Camera->UploadData(&matrix, sizeof(matrix));
}
void HiZ::Record(VkCommandBuffer cmd, const Buffer &vertices, const Buffer &indices) const
{
    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = m_RenderPass;
    begin.framebuffer = m_Framebuffer;
    begin.renderArea.extent = m_Extent;
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, float(m_Extent.width), float(m_Extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, m_Extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DepthPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DepthLayout, 0, 1, &m_DepthSet, 0,
                            nullptr);
    auto buffer = vertices.GetHandle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
    vkCmdBindIndexBuffer(cmd, indices.GetHandle(), 0, VK_INDEX_TYPE_UINT32);
    for (const auto &sub : m_Occluders)
        vkCmdDrawIndexed(cmd, sub.indexCount, 1, sub.firstIndex, 0, 0);
    vkCmdEndRenderPass(cmd);
    VkImageMemoryBarrier init{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    init.image = m_Pyramid->GetHandle();
    init.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    init.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    init.srcQueueFamilyIndex = init.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    init.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, m_Levels, 0, 1};
    init.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    init.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &init);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ReducePipeline);
    for (uint32_t level = 0; level < m_Levels; ++level)
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ReduceLayout, 0, 1,
                                &m_ReduceSets[level], 0, nullptr);
        glm::ivec4 settings(level == 0 ? 1 : 0, 0, 0, 0);
        vkCmdPushConstants(cmd, m_ReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &settings);
        vkCmdDispatch(cmd, (std::max(1u, m_Padded.width >> level) + 7) / 8,
                      (std::max(1u, m_Padded.height >> level) + 7) / 8, 1);
        VkImageMemoryBarrier ready = init;
        ready.oldLayout = ready.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        ready.subresourceRange.baseMipLevel = level;
        ready.subresourceRange.levelCount = 1;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &ready);
    }
    if (m_Verify)
    {
        std::vector<VkBufferImageCopy> copies;
        for (uint32_t level = 0; level < m_Levels; ++level)
        {
            VkBufferImageCopy copy{};
            copy.bufferOffset = m_Offsets[level];
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            copy.imageExtent = {std::max(1u, m_Padded.width >> level), std::max(1u, m_Padded.height >> level),
                                1};
            copies.push_back(copy);
        }
        vkCmdCopyImageToBuffer(cmd, m_Pyramid->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, m_Readback->GetHandle(),
                               m_Levels, copies.data());
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                             nullptr, 0, nullptr);
    }
}
void HiZ::ValidatePyramid()
{
    if (!m_Verify)
        return;
    std::vector<float> data(size_t(m_Readback->GetSize() / 4));
    m_Readback->DownloadData(data.data(), m_Readback->GetSize());
    m_CPU.resize(m_Levels);
    for (uint32_t level = 0; level < m_Levels; ++level)
    {
        uint32_t width = std::max(1u, m_Padded.width >> level),
                 height = std::max(1u, m_Padded.height >> level);
        m_CPU[level].assign(data.begin() + m_Offsets[level] / 4,
                            data.begin() + m_Offsets[level] / 4 + size_t(width) * height);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                const float value = m_CPU[level][size_t(y) * width + x];
                if (!std::isfinite(value) || value < 0 || value > 1)
                    throw std::runtime_error("Invalid Hi-Z depth value");
                if (level == 0)
                {
                    if ((x >= m_Extent.width || y >= m_Extent.height) && value != 1)
                        throw std::runtime_error("Hi-Z padding is not far depth");
                }
                else
                {
                    const uint32_t previousWidth = std::max(1u, m_Padded.width >> (level - 1)),
                                   previousHeight = std::max(1u, m_Padded.height >> (level - 1));
                    float expected = 0;
                    for (uint32_t dy = 0; dy < 2; ++dy)
                        for (uint32_t dx = 0; dx < 2; ++dx)
                            expected = std::max(
                                expected, m_CPU[level - 1][size_t(std::min(y * 2 + dy, previousHeight - 1)) *
                                                               previousWidth +
                                                           std::min(x * 2 + dx, previousWidth - 1)]);
                    if (value != expected)
                        throw std::runtime_error("Hi-Z max-reduction mismatch");
                }
            }
    }
}
bool HiZ::OccludedCPU(glm::vec4 sphere, const glm::mat4 &clip) const
{
    const auto footprint = ProjectHiZSphere(sphere, clip, m_Extent.width, m_Extent.height, m_Levels);
    return footprint.valid && QueryHiZOccluded(footprint, m_CPU.at(footprint.mip),
                                               std::max(1u, m_Padded.width >> footprint.mip));
}
