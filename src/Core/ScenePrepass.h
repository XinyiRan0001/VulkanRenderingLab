#pragma once
#include "Buffer.h"
#include "Image.h"
#include "MeshData.h"
#include "TemporalAo.h"
#include "UniformBufferObject.h"
#include <memory>

class GpuSkinning;

// One frame in flight: attachments/UBOs are reused only after the renderer fence.
class ScenePrepass
{
  public:
    ScenePrepass(const PhysicalDevice &, const LogicalDevice &, VkRenderPass mainPass, VkExtent2D,
                 const Buffer &camera, const Buffer &instances, const std::vector<SubMesh> &, bool verify,
                 bool halfResolution = false, const Buffer *deformedVertices = nullptr,
                 uint32_t previousOffset = 0, const GpuSkinning *verifier = nullptr);
    ~ScenePrepass();
    void SetMode(int mode);
    bool AoEnabled() const
    {
        return m_Mode >= 5 && m_Mode <= 7;
    }
    void ResetMotionHistory()
    {
        m_HistoryValid = false;
        m_Temporal->Reset();
        m_SamplePhase = 0;
    }
    void SetVaryingSampling(bool enabled)
    {
        if (m_VaryingSampling != enabled)
        {
            m_VaryingSampling = enabled;
            m_SamplePhase = 0;
            m_Temporal->Reset();
        }
    }
    bool VaryingSampling() const
    {
        return m_VaryingSampling;
    }
    void SetAo(float radius, float bias, int samples);
    void SetPlaneAwareBlur(bool enabled)
    {
        if (m_PlaneAwareBlur != enabled)
            m_Temporal->Reset();
        m_PlaneAwareBlur = enabled;
    }
    bool PlaneAwareBlur() const
    {
        return m_PlaneAwareBlur;
    }
    glm::vec3 AoSettings() const
    {
        return {m_AoRadius, m_AoBias, float(m_AoSamples)};
    }
    void RecordAo(VkCommandBuffer) const;
    void RecordBlur(VkCommandBuffer) const;
    void RecordResolve(VkCommandBuffer) const;
    void RecordTemporal(VkCommandBuffer cmd) const
    {
        m_Temporal->Record(cmd);
    }
    void SetTemporalAo(bool enabled)
    {
        m_Temporal->SetEnabled(enabled);
    }
    bool TemporalEnabled() const
    {
        return m_Temporal->Enabled();
    }
    void SetAdaptiveAo(bool enabled)
    {
        m_Temporal->SetAdaptive(enabled);
    }
    bool AdaptiveAo() const
    {
        return m_Temporal->Adaptive();
    }
    void SetTemporalDebug(int mode)
    {
        m_TemporalDebug = mode;
    }
    int TemporalDebug() const
    {
        return m_TemporalDebug;
    }
    VkImageView BlurredView() const
    {
        return m_Temporal->View();
    }
    VkSampler Sampler() const
    {
        return m_Sampler;
    }
    int Mode() const
    {
        return m_Mode;
    }
    bool Enabled() const
    {
        return m_Mode != 0;
    }
    void Update(const UniformBufferObject &, bool compareMain = true);
    void Record(VkCommandBuffer, const Buffer &vertices, const Buffer &indices) const;
    void RecordPreview(VkCommandBuffer) const;
    void RecordReadback(VkCommandBuffer, VkImage mainDepth) const;
    void MarkSubmitted();
    void ReadCompleted();
    void SetDepthRange(float value);
    float DepthRange() const
    {
        return m_Range;
    }

  private:
    struct alignas(16) Preview
    {
        glm::mat4 inverseProjection{1}, inverseView{1};
        glm::ivec4 settings{};
        glm::vec4 parameters{};
        glm::mat4 projection{1};
        glm::vec4 kernel[64]{};
        glm::ivec4 aoSettings{};
        glm::ivec4 sampling{}; // deterministic phase 0..63, varying flag, reserved
    };
    static_assert(sizeof(Preview) == 1280, "Preview std140 mismatch");
    struct alignas(16) MotionState
    {
        glm::mat4 previousViewProjection{1}, previousGlobalModel{1};
        glm::ivec4 settings{};
    };
    static_assert(sizeof(MotionState) == 144, "Motion std140 mismatch");
    const Buffer &m_Instances;
    std::vector<InstanceData> m_CurrentInstances, m_PreviousInstances, m_LastCurrentInstances,
        m_LastPreviousInstances;
    UniformBufferObject m_CurrentCamera{}, m_PreviousCamera{}, m_LastCamera{};
    MotionState m_MotionCurrent{}, m_MotionLast{};
    bool m_HistoryValid = false;
    const LogicalDevice &m_Device;
    VkExtent2D m_Extent, m_AoExtent;
    bool m_HalfResolution = false;
    VkFormat m_NormalFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_NormalBytes = 0;
    bool m_Verify = false, m_Pending = false, m_Compare = true, m_LastCompare = true;
    int m_Mode = 0;
    int m_TemporalDebug = 0;
    float m_Range = 10.f;
    float m_AoRadius = .5f, m_AoBias = .025f;
    int m_AoSamples = 32;
    bool m_PlaneAwareBlur = true;
    bool m_VaryingSampling = false;
    uint32_t m_SamplePhase = 0;
    Preview m_Current{}, m_Last{};
    std::vector<SubMesh> m_SubMeshes;
    std::unique_ptr<Image> m_Normal, m_Depth, m_Ao, m_Blur, m_Guide, m_Resolved, m_Motion;
    std::unique_ptr<TemporalAo> m_Temporal;
    std::unique_ptr<Buffer> m_PreviewUniform, m_Readback, m_MotionUniform, m_PreviousInstanceBuffer;
    VkRenderPass m_Pass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_SceneSetLayout = VK_NULL_HANDLE, m_ViewSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_SceneSet = VK_NULL_HANDLE, m_ViewSet = VK_NULL_HANDLE;
    VkPipelineLayout m_SceneLayout = VK_NULL_HANDLE, m_ViewLayout = VK_NULL_HANDLE;
    VkPipeline m_AoViewPipeline = VK_NULL_HANDLE;
    VkPipeline m_ResolvePipeline = VK_NULL_HANDLE;
    VkPipeline m_BlurPipeline = VK_NULL_HANDLE;
    VkPipeline m_AoPipeline = VK_NULL_HANDLE;
    uint32_t m_PreviousVertexOffset = 0;
    const GpuSkinning *m_SkinningVerifier = nullptr;
    VkPipeline m_DoublePipeline = VK_NULL_HANDLE;
    VkPipeline m_ScenePipeline = VK_NULL_HANDLE, m_ViewPipeline = VK_NULL_HANDLE;
    void Release();
};
