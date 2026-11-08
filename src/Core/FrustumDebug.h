#pragma once
#include "Buffer.h"
#include "UniformBufferObject.h"
#include <memory>
#include <vector>

// An always-recorded line draw reads host vertices updated only after the frame fence.
class FrustumDebug
{
  public:
    FrustumDebug(const PhysicalDevice &, const LogicalDevice &, VkRenderPass, VkExtent2D,uint32_t capacity=48);
    ~FrustumDebug();
    void Update(const UniformBufferObject &observer, const UniformBufferObject &culling, bool enabled,
                glm::vec4 sphere = {}, bool inspect = false);
    void Record(VkCommandBuffer) const;
    void UpdateLines(const glm::mat4& viewProjection,const std::vector<glm::vec3>& endpoints);

  private:
    const LogicalDevice &m_Device;
    std::unique_ptr<Buffer> m_Vertices;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    uint32_t m_Capacity=48;
};
