#pragma once

#include <glm/glm.hpp>

struct PointLight
{
    glm::vec4 position{ 0.0f };
    glm::vec4 color{ 1.0f };
};

struct UniformBufferObject
{
    glm::mat4 model{ 1.0f };
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::mat4 lightViewProj{ 1.0f };

    alignas(16) glm::ivec4 shadowSettings{ 3, 0, 0, 0 };

    PointLight pointLight;
};