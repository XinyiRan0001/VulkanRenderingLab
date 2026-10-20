#pragma once
#include "MeshData.h"
#include <string>
class GLTFLoader {
public:
    static MeshData Load(const std::string& path, bool animationMaterialsOnly=false);
};
