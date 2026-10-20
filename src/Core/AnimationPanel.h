#pragma once
#include "AnimationControls.h"
#include "Buffer.h"
#include <glm/glm.hpp>
#include <memory>

class AnimationPanel
{
  public:
    AnimationPanel(const PhysicalDevice &, const LogicalDevice &, VkRenderPass, VkExtent2D);
    ~AnimationPanel();
    void Update(const AnimationControls &, int windowWidth, int windowHeight);
    void Record(VkCommandBuffer) const;
    static float Scale(int width, int height);

  private:
    struct Vertex
    {
        glm::vec2 position;
        glm::vec4 color;
    };
    const LogicalDevice &m_Device;
    std::unique_ptr<Buffer> m_Vertices, m_Draw;
    VkPipelineLayout m_Layout = VK_NULL_HANDLE;
    VkPipeline m_Pipeline = VK_NULL_HANDLE;
    std::vector<Vertex> m_Data;
    float m_Width = 1, m_Height = 1, m_Scale = 1;
    void Rect(float x, float y, float w, float h, glm::vec4 color);
    void Text(float x, float y, const std::string &, glm::vec4 color);
    void Release();
};
