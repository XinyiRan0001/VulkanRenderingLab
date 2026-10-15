#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>

struct Vertex
{
    float position[3];
    float color[3];
    float normal[3];
    float texCoord[2];
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};

    static VkVertexInputBindingDescription GetBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};

        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5>
        GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 5>
            attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format =
            VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset =
            offsetof(Vertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format =
            VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset =
            offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format =
            VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[2].offset =
            offsetof(Vertex, normal);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format =
            VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset =
            offsetof(Vertex, texCoord);

        attributeDescriptions[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)};
        return attributeDescriptions;
    }
};