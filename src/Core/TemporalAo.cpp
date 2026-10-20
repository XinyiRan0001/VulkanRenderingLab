#include "TemporalAo.h"
#include "ShaderUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/packing.hpp>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Check(VkResult result)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error("Temporal AO Vulkan resource/command failed");
}
VkImageMemoryBarrier Barrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkAccessFlags source, VkAccessFlags destination)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = image;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = source;
    b.dstAccessMask = destination;
    return b;
}
void Apply(VkCommandBuffer cmd, const VkImageMemoryBarrier &b)
{
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);
}
} // namespace

TemporalAo::TemporalAo(const PhysicalDevice &physical, const LogicalDevice &device, VkExtent2D extent,
                       const Image &ao, const Image &normal, const Image &depth, const Image &motion,
                       VkSampler sampler, bool verify)
    : m_Device(device), m_Extent(extent), m_Inputs{&ao, &normal, &depth, &motion}
{
    m_NormalBytes = normal.GetFormat() == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 16;
    VkFormatProperties properties{};
    const auto features = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    for (auto format : {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT})
    {
        vkGetPhysicalDeviceFormatProperties(physical.GetHandle(), format, &properties);
        if ((properties.optimalTilingFeatures & features) != features)
            throw std::runtime_error("Temporal AO requires R32F/RGBA32F sampled/storage/transfer support");
    }
    auto d = device.GetHandle();
    try
    {
        const auto make = [&](VkFormat f) {
            return std::make_unique<Image>(
                physical, device, extent.width, extent.height, f, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        };
        m_Output = make(VK_FORMAT_R32_SFLOAT);
        m_HistoryAo = make(VK_FORMAT_R32G32B32A32_SFLOAT);
        m_HistoryGeometry = make(VK_FORMAT_R32G32B32A32_SFLOAT);
        m_NextAo = make(VK_FORMAT_R32G32B32A32_SFLOAT);
        m_NextGeometry = make(VK_FORMAT_R32G32B32A32_SFLOAT);
        m_Uniform = std::make_unique<Buffer>(
            physical, device, sizeof(State), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (verify)
            m_Readback = std::make_unique<Buffer>(
                physical, device, VkDeviceSize(extent.width) * extent.height * (88 + m_NormalBytes) + 128,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::array<VkDescriptorSetLayoutBinding, 10> bindings{};
        for (uint32_t i = 0; i < 10; ++i)
        {
            bindings[i] = {i,
                           i == 9   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                           : i >= 6 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                    : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        sl.bindingCount = uint32_t(bindings.size());
        sl.pBindings = bindings.data();
        Check(vkCreateDescriptorSetLayout(d, &sl, nullptr, &m_SetLayout));
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
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
        const Image *images[] = {&ao,
                                 &normal,
                                 &depth,
                                 &motion,
                                 m_HistoryAo.get(),
                                 m_HistoryGeometry.get(),
                                 m_Output.get(),
                                 m_NextAo.get(),
                                 m_NextGeometry.get()};
        VkDescriptorImageInfo infos[9]{};
        VkWriteDescriptorSet writes[10]{};
        for (uint32_t i = 0; i < 10; ++i)
        {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = m_Set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = bindings[i].descriptorType;
            if (i < 9)
            {
                infos[i] = {i < 6 ? sampler : VK_NULL_HANDLE, images[i]->GetView(),
                            i >= 4   ? VK_IMAGE_LAYOUT_GENERAL
                            : i == 2 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                writes[i].pImageInfo = &infos[i];
            }
        }
        VkDescriptorBufferInfo uniform{m_Uniform->GetHandle(), 0, sizeof(State)};
        writes[9].pBufferInfo = &uniform;
        vkUpdateDescriptorSets(d, 10, writes, 0, nullptr);
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &m_SetLayout;
        Check(vkCreatePipelineLayout(d, &layout, nullptr, &m_Layout));
        const auto code = ReadFile("build/shaders/ssao_temporal.comp.spv");
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
        VkShaderModule module;
        Check(vkCreateShaderModule(d, &moduleInfo, nullptr, &module));
        VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline.stage.module = module;
        pipeline.stage.pName = "main";
        pipeline.layout = m_Layout;
        const auto result = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_Pipeline);
        vkDestroyShaderModule(d, module, nullptr);
        Check(result);
        InitializeHistory(physical);
    }
    catch (...)
    {
        Release();
        throw;
    }
}
TemporalAo::~TemporalAo()
{
    Release();
}
void TemporalAo::Release()
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
void TemporalAo::InitializeHistory(const PhysicalDevice &physical)
{
    auto d = m_Device.GetHandle();
    VkCommandPool pool = VK_NULL_HANDLE;
    try
    {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.queueFamilyIndex = physical.FindQueueFamilies().graphicsFamily.value();
        Check(vkCreateCommandPool(d, &info, nullptr, &pool));
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd;
        Check(vkAllocateCommandBuffers(d, &alloc, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(cmd, &begin));
        for (auto image : {m_HistoryAo.get(), m_HistoryGeometry.get()})
        {
            Apply(cmd, Barrier(image->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
                               VK_ACCESS_TRANSFER_WRITE_BIT));
            VkClearColorValue clear{};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, image->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
            Apply(cmd, Barrier(image->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
        }
        Check(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        Check(vkQueueSubmit(m_Device.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE));
        Check(vkQueueWaitIdle(m_Device.GetGraphicsQueue()));
    }
    catch (...)
    {
        if (pool)
            vkDestroyCommandPool(d, pool, nullptr);
        throw;
    }
    vkDestroyCommandPool(d, pool, nullptr);
}
void TemporalAo::SetEnabled(bool value)
{
    if (m_Enabled != value)
    {
        m_Enabled = value;
        Reset();
    }
}
void TemporalAo::Update(const UniformBufferObject &camera, const UniformBufferObject &previous,
                        bool motionValid)
{
    ReadCompleted();
    m_Current.inverseView = glm::inverse(camera.view);
    m_Current.previousInverseProjection = glm::inverse(previous.proj);
    m_Current.settings = {m_Extent.width, m_Extent.height, m_Valid && motionValid ? 1 : 0,
                          (m_Enabled ? 1 : 0) | (m_Adaptive ? 2 : 0)};
    m_Uniform->UploadData(&m_Current, sizeof(m_Current));
}
void TemporalAo::Record(VkCommandBuffer cmd) const
{
    for (auto image : {m_Output.get(), m_NextAo.get(), m_NextGeometry.get()})
        Apply(cmd,
              Barrier(image->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT));
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Layout, 0, 1, &m_Set, 0, nullptr);
    vkCmdDispatch(cmd, (m_Extent.width + 7) / 8, (m_Extent.height + 7) / 8, 1);
    if (m_Readback)
    {
        const Image *images[] = {
            m_Inputs[0],    m_Inputs[1],    m_Inputs[3],         m_HistoryAo.get(), m_HistoryGeometry.get(),
            m_Output.get(), m_NextAo.get(), m_NextGeometry.get()};
        VkDeviceSize offset = 0;
        for (int i = 0; i < 8; ++i)
        {
            const auto layout = i < 3 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
            Apply(cmd, Barrier(images[i]->GetHandle(), layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                               VK_ACCESS_TRANSFER_READ_BIT));
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {m_Extent.width, m_Extent.height, 1};
            vkCmdCopyImageToBuffer(cmd, images[i]->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   m_Readback->GetHandle(), 1, &copy);
            Apply(cmd, Barrier(images[i]->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout,
                               VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT));
            const uint32_t bytes = i == 0 || i == 5 ? 4 : i == 1 ? m_NormalBytes : 16;
            offset =
                (offset + VkDeviceSize(m_Extent.width) * m_Extent.height * bytes + 15) & ~VkDeviceSize(15);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                             nullptr, 0, nullptr);
    }
    // A separate copy phase avoids read/write races between neighboring pixels.
    const Image *source[] = {m_NextAo.get(), m_NextGeometry.get()};
    const Image *destination[] = {m_HistoryAo.get(), m_HistoryGeometry.get()};
    for (int i = 0; i < 2; ++i)
    {
        Apply(cmd,
              Barrier(source[i]->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT));
        Apply(cmd,
              Barrier(destination[i]->GetHandle(), VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT));
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {m_Extent.width, m_Extent.height, 1};
        vkCmdCopyImage(cmd, source[i]->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destination[i]->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Apply(cmd,
              Barrier(source[i]->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                      VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT));
        Apply(cmd, Barrier(destination[i]->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
    }
    Apply(cmd,
          Barrier(m_Output->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT));
}
void TemporalAo::MarkSubmitted(bool aoRan)
{
    m_Valid = aoRan;
    if (aoRan && m_Readback)
    {
        m_Last = m_Current;
        m_Pending = true;
    }
}
void TemporalAo::ReadCompleted()
{
    if (!m_Pending)
        return;
    m_Pending = false;
    const size_t count = size_t(m_Extent.width) * m_Extent.height;
    std::vector<unsigned char> data(size_t(m_Readback->GetSize()));
    m_Readback->DownloadData(data.data(), data.size());
    size_t offsets[8]{}, offset = 0;
    for (int i = 0; i < 8; ++i)
    {
        offsets[i] = offset;
        offset = (offset + count * (i == 0 || i == 5 ? 4 : i == 1 ? m_NormalBytes : 16) + 15) & ~size_t(15);
    }
    const auto scalar = [&](int image, size_t p) {
        float f;
        std::memcpy(&f, data.data() + offsets[image] + p * 4, 4);
        return f;
    };
    const auto vector = [&](int image, size_t p) {
        glm::vec4 f;
        std::memcpy(&f, data.data() + offsets[image] + p * 16, 16);
        return f;
    };
    const auto normal = [&](size_t p) {
        if (m_NormalBytes == 16)
            return vector(1, p);
        uint16_t h[4];
        std::memcpy(h, data.data() + offsets[1] + p * 8, 8);
        return glm::vec4(glm::detail::toFloat32(h[0]), glm::detail::toFloat32(h[1]),
                         glm::detail::toFloat32(h[2]), glm::detail::toFloat32(h[3]));
    };
    size_t accepted = 0, rejected = 0;
    size_t receiver = 0, revealed = 0;
    double receiverDelta = 0, receiverDarkening = 0;
    float revealWeight = 0, maxDarkening = 0;
    float maxError = 0, maxChange = 0;
    for (size_t p = 0; p < count; ++p)
    {
        const auto n = normal(p), motion = vector(2, p), next = vector(6, p), geometry = vector(7, p);
        const float current = scalar(0, p), output = scalar(5, p);
        for (int component = 0; component < 4; ++component)
            if (!std::isfinite(next[component]) || !std::isfinite(geometry[component]))
                throw std::runtime_error("Temporal AO history contains nonfinite metadata");
        if (next.z < 0 || next.z > .85001f || next.w < 0 || next.w > 5 || next.w != std::floor(next.w) ||
            std::abs(next.y - std::abs(motion.w)) > 1e-5f)
            throw std::runtime_error("Temporal AO history weight/identity/reason is invalid");
        if (!std::isfinite(output) || output < 0 || output > 1 || std::abs(next.x - output) > 1e-6f)
            throw std::runtime_error("Temporal AO output is invalid or differs from stored history");
        float expected = n.a > 0 ? current : 1.f;
        float low = current, high = current;
        const int px = int(p % m_Extent.width), py = int(p / m_Extent.width);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                const size_t q = size_t(std::clamp(py + y, 0, int(m_Extent.height) - 1)) * m_Extent.width +
                                 std::clamp(px + x, 0, int(m_Extent.width) - 1);
                low = std::min(low, scalar(0, q));
                high = std::max(high, scalar(0, q));
            }
        if (n.a > 0)
        {
            const glm::vec3 worldNormal = glm::normalize(glm::mat3(m_Last.inverseView) * glm::vec3(n));
            if (glm::length(worldNormal - glm::vec3(geometry)) > 1e-4f || std::abs(n.a - geometry.a) > 1e-6f)
                throw std::runtime_error("Temporal AO history geometry is incorrect");
            if (m_Last.settings.z && (m_Last.settings.w & 1) && motion.w > 0)
            {
                const glm::vec2 uv = (glm::vec2(px, py) + .5f) / glm::vec2(m_Extent.width, m_Extent.height) +
                                     glm::vec2(motion);
                const auto previous =
                    m_Last.previousInverseProjection * glm::vec4(uv * 2.f - 1.f, motion.z, 1);
                const float distance = -previous.z / previous.w;
                const glm::vec2 coord = uv * glm::vec2(m_Extent.width, m_Extent.height) - .5f;
                const glm::ivec2 base(glm::floor(coord));
                float sum = 0, weights = 0;
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 2; ++x)
                    {
                        const auto q = base + glm::ivec2(x, y);
                        if (q.x < 0 || q.y < 0 || q.x >= int(m_Extent.width) || q.y >= int(m_Extent.height))
                            continue;
                        const auto index = size_t(q.y) * m_Extent.width + q.x;
                        const auto old = vector(3, index), g = vector(4, index);
                        if (g.a <= 0 || std::abs(old.y - motion.w) > .25f ||
                            glm::dot(worldNormal, glm::vec3(g)) < .9f ||
                            std::abs(g.a - distance) > std::max(.01f, .005f * distance))
                            continue;
                        const auto w = glm::max(glm::vec2(0), glm::vec2(1) - glm::abs(coord - glm::vec2(q)));
                        sum += old.x * w.x * w.y;
                        weights += w.x * w.y;
                    }
                if (weights > 1e-5f)
                {
                    float w = .85f * std::min(weights, 1.f) *
                              std::exp(-glm::length(glm::vec2(motion) *
                                                    glm::vec2(m_Extent.width, m_Extent.height)) /
                                       32.f);
                    const float history = std::clamp(sum / weights, low, high);
                    if (m_Last.settings.w & 2)
                        w *= 1 - glm::smoothstep(.005f, .05f, std::abs(current - history));
                    expected = glm::mix(current, history, w);
                }
            }
            if (next.z > 0)
            {
                ++accepted;
                if (output < low - 1e-5f || output > high + 1e-5f)
                    throw std::runtime_error("Temporal AO exceeded current neighborhood clamp");
            }
            else
                ++rejected;
        }
        maxError = std::max(maxError, std::abs(output - expected));
        maxChange = std::max(maxChange, std::abs(output - current));
        // Diagnostic fixture reserves ID 1 for its fixed receiver. Other scenes
        // may also use ID 1; these statistics do not infer object semantics there.
        if (m_Last.settings.z && motion.w == 1 &&
            glm::length(glm::vec2(motion) * glm::vec2(m_Extent.width, m_Extent.height)) < .02f)
        {
            const auto old = vector(3, p);
            if (old.y == 2)
            {
                ++revealed;
                revealWeight = std::max(revealWeight, next.z);
            }
            {
                ++receiver;
                receiverDelta += std::abs(output - current);
                receiverDarkening += std::max(current - output, 0.f);
                maxDarkening = std::max(maxDarkening, current - output);
            }
        }
        if (!m_Last.settings.z || !(m_Last.settings.w & 1))
        {
            if (next.z != 0 || std::abs(output - (n.a > 0 ? current : 1.f)) > 1e-6f)
                throw std::runtime_error("Temporal AO did not reset to current AO");
        }
    }
    if (maxError > .003f)
        throw std::runtime_error("Temporal AO differs from CPU reprojection/clamp reference: " +
                                 std::to_string(maxError));
    std::cout << "[Temporal AO Verify] history=" << m_Last.settings.z
              << " enabled=" << (m_Last.settings.w & 1) << " accepted=" << accepted
              << " rejected=" << rejected << " max_change=" << maxChange << " max_error=" << maxError
              << std::endl;
    std::cout << std::setprecision(8) << "[AO Receiver Verify] stable=" << receiver
              << " newly_revealed=" << revealed << " max_reveal_weight=" << revealWeight
              << " mean_abs_delta=" << (receiver ? receiverDelta / receiver : 0)
              << " mean_darkening=" << (receiver ? receiverDarkening / receiver : 0)
              << " max_darkening=" << maxDarkening << std::endl;
}
