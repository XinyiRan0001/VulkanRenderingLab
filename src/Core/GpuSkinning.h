#pragma once
#include "AnimationRig.h"
#include "Buffer.h"
#include "MeshData.h"
#include <memory>

// Compute writes packed Vertex data plus a float4 previous-position tail.
// Four storage bindings stay within Vulkan's guaranteed per-stage minimum.
class GpuSkinning
{
  public:
    static MeshData BuildMesh(const AnimationRig &, MeshData materials, bool floor=true, glm::mat4 placement=glm::mat4(1));
    static VkDeviceSize BufferBytes(size_t vertices)
    {
        return vertices * (sizeof(Vertex) + 16);
    }
    GpuSkinning(const PhysicalDevice &, const LogicalDevice &, const AnimationRig &, const MeshData &,
                Buffer &output, bool verify, glm::mat4 placement=glm::mat4(1));
    ~GpuSkinning();
    void Update(size_t clip, float seconds, bool loop=true);
    void SetDualQuaternion(bool enabled);
    void SetSurfaceNormals(bool enabled);
    void Record(VkCommandBuffer) const;
    void MarkSubmitted();
    void ReadCompleted();
    const std::vector<float>& CompletedVertices() const {return m_LastGpuData;}
    uint32_t PreviousOffset() const
    {
        return uint32_t(m_Input.size() * sizeof(Vertex) / 4);
    }
    std::vector<uint32_t> DrawOrder(const glm::mat4 &view) const;
    struct MotionSample
    {
        glm::vec2 uv;
        float depth;
        glm::vec4 motion;
    };
    float VerifyMotion(const glm::mat4 &currentVP, const glm::mat4 &previousVP, bool history,
                       glm::vec2 extent, const std::vector<MotionSample> &) const;

  private:
    struct Input
    {
        glm::vec4 position{}, normal{};
        glm::uvec4 joints{};
        glm::vec4 weights{};
        glm::uvec4 meta{};
        glm::uvec4 surface{};
    };
    struct Delta
    {
        glm::vec4 position{}, normal{};
    };
    static_assert(sizeof(Input) == 96 && sizeof(Delta) == 32 && sizeof(Vertex) == 60,
                  "skinning layout mismatch");
    const LogicalDevice &m_Device;
    const AnimationRig &m_Rig;
    Buffer &m_Output;
    std::vector<Input> m_Input;
    std::vector<Delta> m_Deltas;
    std::vector<std::pair<int, int>> m_Matrices;
    std::vector<uint32_t> m_PrimitiveFirst, m_MorphOffsets;
    std::vector<bool> m_Transparent;
    std::vector<std::vector<glm::uvec3>> m_SurfaceFaces;
    RigPose m_Current, m_Previous;
    std::vector<glm::vec4> m_PoseData, m_LastData;
    std::vector<float> m_LastGpuData;
    std::unique_ptr<Buffer> m_InputBuffer, m_PoseBuffer, m_DeltaBuffer;
    bool m_Valid = false, m_Verify = false, m_Pending = false;
    bool m_DualQuaternion = true;
    bool m_SurfaceNormals = true;
    glm::mat4 m_Placement{1};
    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_Set = VK_NULL_HANDLE;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    std::vector<glm::vec4> PoseData(const RigPose &, const RigPose &) const;
    std::pair<glm::vec3, glm::vec3> Evaluate(size_t, const std::vector<glm::vec4> &, bool previous) const;
    void Release();
};
