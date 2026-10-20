#pragma once
#include "Buffer.h"
#include "Image.h"
#include "UniformBufferObject.h"
#include <array>
#include <memory>

// Fixed descriptors and one frame in flight. History is copied only AFTER all
// invocations finish reading it; no descriptor updates invalidate recorded cmds.
class TemporalAo
{
  public:
    TemporalAo(const PhysicalDevice &, const LogicalDevice &, VkExtent2D, const Image &ao,
               const Image &normal, const Image &depth, const Image &motion, VkSampler, bool verify);
    ~TemporalAo();
    void SetEnabled(bool value);
    void SetAdaptive(bool value)
    {
        if (m_Adaptive != value)
        {
            m_Adaptive = value;
            Reset();
        }
    }
    bool Adaptive() const
    {
        return m_Adaptive;
    }
    bool Enabled() const
    {
        return m_Enabled;
    }
    void Reset()
    {
        m_Valid = false;
    }
    void Update(const UniformBufferObject &current, const UniformBufferObject &previous, bool motionValid);
    void Record(VkCommandBuffer) const;
    void MarkSubmitted(bool aoRan);
    void ReadCompleted();
    VkImageView View() const
    {
        return m_Output->GetView();
    }
    VkImageView DebugView() const
    {
        return m_NextAo->GetView();
    }

  private:
    struct alignas(16) State
    {
        glm::mat4 inverseView{1}, previousInverseProjection{1};
        glm::ivec4 settings{}; // width, height, valid history, flags: enable=1, adaptive=2
    };
    static_assert(sizeof(State) == 144, "Temporal AO std140 mismatch");
    const LogicalDevice &m_Device;
    VkExtent2D m_Extent;
    std::array<const Image *, 4> m_Inputs;
    std::unique_ptr<Image> m_Output, m_HistoryAo, m_HistoryGeometry, m_NextAo, m_NextGeometry;
    std::unique_ptr<Buffer> m_Uniform, m_Readback;
    State m_Current{}, m_Last{};
    bool m_Enabled = false, m_Valid = false, m_Pending = false, m_Adaptive = false;
    uint32_t m_NormalBytes = 0;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    void Release();
    void InitializeHistory(const PhysicalDevice &);
};
