#pragma once

#include "Vertex.h"

#include <cstdint>
#include <vector>

struct SubMesh
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int materialId = -1;
};

struct MaterialData
{
    float diffuseColor[3] =
    {
        1.0f,
        1.0f,
        1.0f
    };

    bool hasDiffuseTexture = false;
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::vector<SubMesh> subMeshes;
    std::vector<MaterialData> materials;
};