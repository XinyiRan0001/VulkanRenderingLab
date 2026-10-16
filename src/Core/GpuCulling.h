#pragma once
#include "Buffer.h"
#include "Frustum.h"
#include "HiZ.h"
#include "MeshData.h"
#include "UniformBufferObject.h"
#include <memory>

// One shared-mesh instance group, potentially containing several material ranges.
// All ranges consume the same compacted visible-ID list. Shadows stay unculled.
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
    const Buffer &IndirectBuffer() const
    {
        return *m_Indirect;
    }
    void Update(const UniformBufferObject &, bool enabled, bool verify, bool occlusion = true);
    void Record(VkCommandBuffer) const;
    void MarkSubmitted()
    {
        m_Pending = true;
    }
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
        glm::vec4 localSphere;
        glm::uvec4 settings; // first instance, count, material draw count, enabled
        glm::mat4 clipFromScene;
        glm::uvec4 hiZ; // viewport width/height, pyramid levels, enabled
    };
    static_assert(sizeof(CullUniform) == 208, "std140 cull uniform layout mismatch");
    const LogicalDevice &m_Device;
    HiZ *m_HiZ = nullptr;
    uint32_t m_FrustumCount = 0, m_OccludedCount = 0;
    CullUniform m_LastUniform{};
    uint32_t m_First = 0, m_Count = 0, m_VisibleCount = 0;
    glm::vec4 m_LocalSphere{};
    std::vector<InstanceData> m_Instances;
    std::vector<VkDrawIndexedIndirectCommand> m_Templates;
    std::vector<uint32_t> m_Expected;
    bool m_Pending = false, m_Verify = false;
    bool m_CaptureVisible = false;
    std::unique_ptr<Buffer> m_Visible, m_Indirect, m_Uniform, m_Readback, m_Stats;
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    void Release();
};
