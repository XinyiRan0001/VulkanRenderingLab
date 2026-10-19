#pragma once
#include "Buffer.h"
#include "Image.h"
#include "MeshData.h"
#include "UniformBufferObject.h"
#include <memory>

// One frame in flight: attachments/UBOs are reused only after the renderer fence.
class ScenePrepass
{
  public:
    ScenePrepass(const PhysicalDevice &, const LogicalDevice &, VkRenderPass mainPass, VkExtent2D,
                 const Buffer &camera, const Buffer &instances, const std::vector<SubMesh> &, bool verify);
    ~ScenePrepass();
    void SetMode(int mode);
    void SetAo(float radius, float bias, int samples);
    glm::vec3 AoSettings() const
    {
        return {m_AoRadius, m_AoBias, float(m_AoSamples)};
    }
    void RecordAo(VkCommandBuffer) const;
    void RecordBlur(VkCommandBuffer) const;
    VkImageView BlurredView() const
    {
        return m_Blur->GetView();
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
    };
    static_assert(sizeof(Preview) == 1248, "Preview std140 mismatch");
    const LogicalDevice &m_Device;
    VkExtent2D m_Extent;
    VkFormat m_NormalFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_NormalBytes = 0;
    bool m_Verify = false, m_Pending = false, m_Compare = true, m_LastCompare = true;
    int m_Mode = 0;
    float m_Range = 10.f;
    float m_AoRadius = .5f, m_AoBias = .025f;
    int m_AoSamples = 32;
    Preview m_Current{}, m_Last{};
    std::vector<SubMesh> m_SubMeshes;
    std::unique_ptr<Image> m_Normal, m_Depth, m_Ao, m_Blur;
    std::unique_ptr<Buffer> m_PreviewUniform, m_Readback;
    VkRenderPass m_Pass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_SceneSetLayout = VK_NULL_HANDLE, m_ViewSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_SceneSet = VK_NULL_HANDLE, m_ViewSet = VK_NULL_HANDLE;
    VkPipelineLayout m_SceneLayout = VK_NULL_HANDLE, m_ViewLayout = VK_NULL_HANDLE;
    VkPipeline m_AoViewPipeline = VK_NULL_HANDLE;
    VkPipeline m_BlurPipeline = VK_NULL_HANDLE;
    VkPipeline m_AoPipeline = VK_NULL_HANDLE;
    VkPipeline m_ScenePipeline = VK_NULL_HANDLE, m_ViewPipeline = VK_NULL_HANDLE;
    void Release();
};
