#pragma once
#include "Buffer.h"
#include "CullingBatches.h"
#include "Frustum.h"
#include "HiZ.h"
#include "MeshData.h"
#include "UniformBufferObject.h"
#include <memory>

// Multiple disjoint instance groups; each prototype may have several material ranges.
// GPU-owned per-group counts drive main draws; shadows stay unculled.
class GpuCulling
{
  public:
    GpuCulling(const PhysicalDevice &, const LogicalDevice &, const Buffer &instances, const MeshData &,
               bool verifyReadback = false, HiZ *hiZ = nullptr, VkDescriptorImageInfo fallback = {});
    ~GpuCulling();
    GpuCulling(const GpuCulling &) = delete;
    GpuCulling &operator=(const GpuCulling &) = delete;
    const Buffer &VisibleBuffer() const
    {
        return *m_Visible;
    }
    const Buffer &StatisticsBuffer() const
    {
        return *m_Stats;
    }
    const Buffer &IndirectBuffer() const
    {
        return *m_Indirect;
    }
    struct BatchCounts
    {
        uint32_t visible = 0, frustum = 0, occluded = 0;
    };
    const CullingBatchLayout &Layout() const
    {
        return m_Batches;
    }
    const std::vector<BatchCounts> &BatchStatistics() const
    {
        return m_BatchCounts;
    }
    const CullingDrawAddress &DrawAddress(uint32_t ordinal) const
    {
        return m_Batches.draws.at(ordinal);
    }
    void Update(const UniformBufferObject &, bool enabled, bool verify, bool occlusion = true);
    void Record(VkCommandBuffer) const;
    void SetHiZSampling(bool enabled); // caller waits for all GPU use first
    void MarkSubmitted()
    {
        m_Pending = true;
    }
    bool VerifiedVisible(uint32_t id) const;
    void ReadCompleted(); // caller must have waited for the render fence
    uint32_t VisibleCount() const
    {
        return m_VisibleCount;
    }
    uint32_t FrustumCount() const
    {
        return m_FrustumCount;
    }
    uint32_t OccludedCount() const
    {
        return m_OccludedCount;
    }
    uint32_t TotalCount() const
    {
        return m_Count;
    }

  private:
    struct alignas(16) CullUniform
    {
        FrustumPlanes planes;
        glm::uvec4 settings; // reserved, total instance count, group count, frustum enabled
        glm::mat4 clipFromScene;
        glm::uvec4 hiZ; // viewport width/height, pyramid levels, enabled
    };
    static_assert(sizeof(CullUniform) == 192, "std140 cull uniform layout mismatch");
    const LogicalDevice &m_Device;
    HiZ *m_HiZ = nullptr;
    VkDescriptorImageInfo m_Fallback{};
    uint32_t m_FrustumCount = 0, m_OccludedCount = 0;
    CullUniform m_LastUniform{};
    uint32_t m_Count = 0, m_VisibleCount = 0;
    CullingBatchLayout m_Batches;
    std::vector<BatchCounts> m_BatchCounts;
    std::vector<InstanceData> m_Instances;
    std::vector<VkDrawIndexedIndirectCommand> m_Templates;
    std::vector<std::vector<uint32_t>> m_Expected;
    std::vector<uint32_t> m_VerifiedIds;
    bool m_Pending = false, m_Verify = false;
    bool m_CaptureVisible = false;
    std::unique_ptr<Buffer> m_Visible, m_Indirect, m_Uniform, m_Readback, m_Stats, m_GroupBuffer;
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    void Release();
};
