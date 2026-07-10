#pragma once

#include "MeshData.h"

#include <string>

class OBJLoader
{
public:
    static MeshData Load(const std::string& filepath);
};