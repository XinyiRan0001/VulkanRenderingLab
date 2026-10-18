#pragma once
#include "Buffer.h"
#include "HiZMath.h"
#include "Image.h"
#include "MeshData.h"
#include "UniformBufferObject.h"
#include <memory>

class HiZ
{
  public:
    HiZ(const PhysicalDevice &, const LogicalDevice &, VkExtent2D, const MeshData &, bool verify);
    ~HiZ();
    HiZ(const HiZ &) = delete;
    HiZ &operator=(const HiZ &) = delete;
    void Update(const UniformBufferObject &);
    void Record(VkCommandBuffer, const Buffer &vertices, const Buffer &indices) const;
    VkDescriptorImageInfo SampledPyramid() const
    {
        return {m_Sampler, m_Pyramid->GetView(), VK_IMAGE_LAYOUT_GENERAL};
    }
    VkDescriptorImageInfo SampledDepth() const { return {m_Sampler,m_Depth->GetView(),VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}; }
    uint32_t Levels() const { return m_Levels; }
    float FarthestCPU(const HiZFootprint& footprint) const { return QueryHiZFarthest(footprint,m_CPU.at(footprint.mip),std::max(1u,m_Padded.width >> footprint.mip)); }
    glm::uvec4 Settings(bool enabled) const
    {
        return {m_Extent.width, m_Extent.height, m_Levels, enabled ? 1u : 0u};
    }
    void ValidatePyramid(); // only after the frame fence, verification path
    bool OccludedCPU(glm::vec4 sphere, const glm::mat4 &clip) const;

  private:
    const LogicalDevice &m_Device;
    VkExtent2D m_Extent{}, m_Padded{};
    uint32_t m_Levels = 0;
    bool m_Verify = false;
    std::vector<SubMesh> m_Occluders;
    std::unique_ptr<Image> m_Depth, m_Pyramid;
    std::unique_ptr<Buffer> m_Camera, m_Readback;
    std::vector<VkImageView> m_MipViews;
    std::vector<VkDeviceSize> m_Offsets;
    std::vector<std::vector<float>> m_CPU;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_DepthSetLayout = VK_NULL_HANDLE, m_ReduceSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_DepthSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_ReduceSets;
    VkPipelineLayout m_DepthLayout = VK_NULL_HANDLE, m_ReduceLayout = VK_NULL_HANDLE;
    VkPipeline m_DepthPipeline = VK_NULL_HANDLE, m_ReducePipeline = VK_NULL_HANDLE;
    void Release();
};
