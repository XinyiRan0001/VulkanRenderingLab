#pragma once
#include "GpuCulling.h"
#include "HiZ.h"
#include <string>

// Independent single-object query: CPU reference footprint, GPU pyramid sampling.
// It never changes the culling result or reads the full pyramid in normal use.
class HiZInspector
{
  public:
    HiZInspector(const PhysicalDevice &, const LogicalDevice &, VkRenderPass, VkExtent2D, HiZ &, GpuCulling &,
                 const MeshData &);
    ~HiZInspector();
    void SetEnabled(bool value)
    {
        m_Enabled = value;
    }
    bool Enabled() const
    {
        return m_Enabled;
    }
    void SetMip(int mip);
    void StepMip(int delta);
    void Select(int offset);
    void StepSelection(int delta);
    glm::vec4 SelectedSphere() const
    {
        return m_Spheres.at(m_Selection);
    }
    void Update(const UniformBufferObject &culling, bool frustum, bool occlusion, bool verify);
    void ReadCompleted();
    void MarkSubmitted();
    void RecordQuery(VkCommandBuffer) const;
    void RecordPanel(VkCommandBuffer) const;
    std::string Status() const;

  private:
    struct alignas(16) Settings
    {
        glm::ivec4 region{}; // inclusive selected-mip cells
        glm::ivec4 query{};  // query mip, valid, rejected by frustum, occlusion enabled
        glm::vec4 depths{};  // nearest raw depth, projection A/B, epsilon
        glm::ivec4 view{};   // enabled, preview mip (-1 raw depth), viewport width/height
        glm::vec4 pixels{};
        glm::ivec4 selection{}; // zero-based sphere offset, actual instance ID  // expanded pixel rectangle,
                                // inclusive
    };
    static_assert(sizeof(Settings) == 96, "Inspector std140 mismatch");
    const LogicalDevice &m_Device;
    HiZ &m_HiZ;
    GpuCulling &m_Culling;
    std::vector<glm::vec4> m_Spheres;
    uint32_t m_First = 0;
    int m_Selection = 0, m_Mip = 0;
    bool m_Enabled = false, m_Pending = false, m_Verify = false, m_HaveResult = false;
    uint32_t m_SubmittedId = 0, m_ResultId = 0;
    Settings m_Current{}, m_Submitted{}, m_ResultSettings{};
    HiZFootprint m_Footprint{}, m_SubmittedFootprint{};
    glm::vec4 m_Result{};
    std::unique_ptr<Buffer> m_Uniform, m_Output, m_Readback;
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Query = VK_NULL_HANDLE, m_Panel = VK_NULL_HANDLE;
    void Release();
};
