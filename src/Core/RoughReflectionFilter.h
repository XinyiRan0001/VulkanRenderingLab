#pragma once
#include "Image.h"
#include "Buffer.h"
#include "UniformBufferObject.h"
#include "ReflectionHistoryState.h"
#include <vector>
#include <memory>
#include <string>

// Full-resolution raw reflection and receiver-guided, three-scale spatial filtering.
// Owned per VulkanContext; the renderer fence protects the single output set.
class RoughReflectionFilter {
public:
    RoughReflectionFilter(const PhysicalDevice&,const LogicalDevice&,VkExtent2D,
                          VkDescriptorSetLayout cameraLayout,VkDescriptorSetLayout rayLayout,
                          VkImageView normal,VkImageView depth,VkSampler,bool verify=false);
    ~RoughReflectionFilter();
    void Update(const UniformBufferObject&,glm::vec4 pose,glm::ivec4 renderState);
    void SetTemporal(bool enabled){m_History.SetEnabled(enabled);}
    bool TemporalEnabled() const {return m_History.Enabled();}
    uint32_t HistoryFrames() const {return m_History.Frames();}
    void SetSamples(int samples);
    int Samples() const {return m_Samples;}
    void SetFurnace(bool value){m_Furnace=value;}
    bool Furnace() const {return m_Furnace;}
    void SetDumpPath(const std::string& path){m_DumpPath=path;}
    void SetHitGuidance(bool enabled){m_HitGuidance=enabled;}
    bool HitGuidance() const {return m_HitGuidance;}
    void ReadCompleted();
    void MarkSubmitted(){
        m_LastTemporal=m_History.Current();m_History.Submit();m_Pending=bool(m_Readback);
        m_LastSamples=m_Samples;m_LastFurnace=m_Furnace;m_LastHitGuidance=m_HitGuidance;
    }
    void Record(VkCommandBuffer,VkDescriptorSet camera,VkDescriptorSet rays,VkQueryPool) const;
    VkImageView RawView() const { return m_Raw->GetView(); }
    VkImageView FilteredView() const { return m_Filtered->GetView(); }
private:
    const LogicalDevice& m_Device;
    VkExtent2D m_Extent;
    std::unique_ptr<Image> m_Raw,m_Ping,m_Filtered,m_HitGuide,m_Accumulated,m_HistoryImage;
    std::unique_ptr<Buffer> m_Camera,m_Readback;
    bool m_Pending=false,m_HitGuidance=true;
    int m_Samples=8,m_LastSamples=8;
    bool m_Furnace=false,m_LastFurnace=false,m_LastHitGuidance=true;
    UniformBufferObject m_LastCamera{};
    std::string m_DumpPath;
    VkDescriptorSetLayout m_SetLayout=VK_NULL_HANDLE;
    VkDescriptorPool m_Pool=VK_NULL_HANDLE;
    VkDescriptorSet m_Set=VK_NULL_HANDLE;
    VkPipelineLayout m_TraceLayout=VK_NULL_HANDLE,m_FilterLayout=VK_NULL_HANDLE;
    VkPipeline m_Trace=VK_NULL_HANDLE,m_Filter=VK_NULL_HANDLE;
    VkDescriptorSetLayout m_TemporalSetLayout=VK_NULL_HANDLE;
    VkDescriptorPool m_TemporalPool=VK_NULL_HANDLE;
    VkDescriptorSet m_TemporalSet=VK_NULL_HANDLE;
    VkPipelineLayout m_TemporalLayout=VK_NULL_HANDLE;
    VkPipeline m_Temporal=VK_NULL_HANDLE;
    ReflectionHistoryState m_History;
    ReflectionHistoryState::Frame m_LastTemporal{};
    std::vector<glm::vec4> m_PreviousMean;
    void InitializeHistory(const PhysicalDevice&);
    void Release();
};
