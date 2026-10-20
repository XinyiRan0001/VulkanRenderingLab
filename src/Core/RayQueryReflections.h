#pragma once
#include "Buffer.h"
#include "MeshData.h"
#include "TextureImage.h"
#include <functional>
#include <memory>
class RayQueryReflections
{
  public:
    RayQueryReflections(const PhysicalDevice &, const LogicalDevice &);
    ~RayQueryReflections();
    void Build(const MeshData &, bool verify, const Buffer *deformed = nullptr);
    void Update();
    void RecordUpdates(VkCommandBuffer) const;
    void MarkSubmitted()
    {
        if (m_Dynamic && m_Verify)
            m_Pending = true;
    }
    void ReadCompleted(const std::vector<float> *vertices = nullptr);
    bool Dynamic() const
    {
        return m_Dynamic;
    }
    void SetEnabled(bool enabled)
    {
        m_Enabled = enabled;
    }
    bool Enabled() const
    {
        return m_Enabled;
    }
    void SetDebug(int debug)
    {
        m_Debug = debug;
    }
    int Debug() const
    {
        return m_Debug;
    }
    VkDescriptorSetLayout Layout() const
    {
        return m_Layout;
    }
    VkDescriptorSet Set() const
    {
        return m_Set;
    }

  private:
    struct Triangle
    {
        glm::vec4 a, b, c, na, nb, nc, color, properties, uv01, uv2;
    };
    struct CheckRay
    {
        glm::vec4 origin, direction, result;
        glm::uvec4 hit;
    };
    static_assert(sizeof(Triangle) == 160 && sizeof(CheckRay) == 64, "Ray Query std430 layout mismatch");
    const PhysicalDevice &m_Physical;
    const LogicalDevice &m_Device;
    VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    VkAccelerationStructureKHR m_Blas = VK_NULL_HANDLE, m_Tlas = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> m_BlasBuffer, m_TlasBuffer, m_Geometry, m_Triangles, m_Settings, m_Checks;
    std::vector<Triangle> m_Data;
    std::vector<glm::uvec4> m_Map;
    std::unique_ptr<TextureImage> m_Atlas;
    TextureData m_AtlasPixels;
    std::unique_ptr<Buffer> m_Indices, m_Metadata, m_Instances, m_BlasScratch, m_TlasScratch;
    VkDeviceAddress m_BlasScratchAddress = 0, m_TlasScratchAddress = 0;
    VkPipeline m_Gather = VK_NULL_HANDLE;
    VkPipelineLayout m_GatherLayout = VK_NULL_HANDLE;
    bool m_Dynamic = false, m_Verify = false, m_Pending = false;
    bool m_HasAlpha=false;
    VkAccelerationStructureGeometryKHR Geometry() const;
    VkAccelerationStructureGeometryKHR Instances() const;
    bool Accept(const Triangle &, glm::vec2 bary) const;
    bool m_Enabled = true;
    int m_Debug = 0;
    PFN_vkCreateAccelerationStructureKHR m_Create = nullptr;
    PFN_vkDestroyAccelerationStructureKHR m_Destroy = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR m_Sizes = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR m_Build = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR m_Address = nullptr;
    void Submit(const std::function<void(VkCommandBuffer)> &);
    void Verify();
    void Release();
};
