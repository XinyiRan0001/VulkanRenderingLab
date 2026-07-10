#pragma once

#include "Vertex.h"

#include <cstdint>
#include <vector>

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};