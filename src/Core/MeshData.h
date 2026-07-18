#pragma once

#include "Vertex.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// =========================================================
// A contiguous index range rendered with one material.
// =========================================================

struct SubMesh
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int materialId = -1;
};

// =========================================================
// CPU-side material description.
//
// diffuseColor is retained because the OBJ loader currently
// imports the MTL diffuse colour into this field.
// =========================================================

struct MaterialData
{
    float diffuseColor[3] =
    {
        1.0f,
        1.0f,
        1.0f
    };

    float metallic = 0.0f;
    float roughness = 0.5f;
    float ambientOcclusion = 1.0f;

    bool hasDiffuseTexture = false;
};

// =========================================================
// Per-draw material data sent through Vulkan push constants.
//
// GLSL layout:
//
// layout(push_constant) uniform MaterialPushConstant
// {
//     ivec4 flags;
//     vec4 baseColor;
//     vec4 properties;
// };
//
// flags.x       = use diffuse texture
//
// properties.x  = metallic
// properties.y  = roughness
// properties.z  = ambient occlusion
// properties.w  = reserved
// =========================================================

struct alignas(16) MaterialPushConstants
{
    alignas(16) glm::ivec4 flags =
        glm::ivec4(0);

    alignas(16) glm::vec4 baseColor =
        glm::vec4(1.0f);

    alignas(16) glm::vec4 properties =
        glm::vec4(
            0.0f,
            0.5f,
            1.0f,
            0.0f
        );
};

static_assert(
    sizeof(MaterialPushConstants) == 48,
    "MaterialPushConstants must be exactly 48 bytes."
    );

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::vector<SubMesh> subMeshes;
    std::vector<MaterialData> materials;
};