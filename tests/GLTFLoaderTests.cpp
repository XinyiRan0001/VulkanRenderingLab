#include "GLTFLoader.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "fixture directory required");
        const std::string root = std::string(argv[1]) + "/";
        for (const char* name : {"scene.gltf", "scene.glb"}) {
            auto mesh = GLTFLoader::Load(root + name);
            Check(mesh.subMeshes.size() == 3 && mesh.indices.size() == 108, "scene instances were lost");
            Check(mesh.subMeshes[2].materialId == 1 && mesh.materials[1].textures[0] == -1, "per-instance material selection incorrect");
            Check(mesh.images.size() == 5, "material images were lost");
            Check(std::abs(mesh.vertices[0].position[0] + 1.6f) < 1e-5f, "child translation incorrect");
            Check(std::abs(mesh.vertices[0].position[1] - 0.3f) < 1e-5f, "parent transform incorrect");
            Check(mesh.vertices[72].tangent[3] == -1, "mirror tangent handedness incorrect");
            Check(mesh.indices[73] == 74 && mesh.indices[74] == 73, "mirror winding incorrect");
            Check(mesh.materials[0].textures[1] == 1 && std::abs(mesh.materials[0].normalScale-0.7f)<1e-5f, "normal material incorrect");
            Check(std::abs(mesh.materials[0].occlusionStrength-0.6f)<1e-5f, "AO strength incorrect");
            for (size_t i = 0; i < mesh.indices.size(); i+=3) {
                const auto& a = mesh.vertices[mesh.indices[i]];
                const auto& b = mesh.vertices[mesh.indices[i+1]];
                const auto& c = mesh.vertices[mesh.indices[i+2]];
                auto face = glm::cross(glm::make_vec3(b.position)-glm::make_vec3(a.position),glm::make_vec3(c.position)-glm::make_vec3(a.position));
                Check(glm::dot(face,glm::make_vec3(a.normal)) > 0, "normal disagrees with transformed winding");
            }
        }
        for (const char* name : {"unsupported-alpha", "missing-tangents", "singular-transform", "invalid-index"}) {
            bool rejected = false;
            try { GLTFLoader::Load(root + name + ".gltf"); } catch (const std::exception&) { rejected = true; }
            Check(rejected, "unsupported or corrupt fixture was accepted");
        }
        std::cout << "glTF/GLB transforms, textures, winding and rejection checks passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
