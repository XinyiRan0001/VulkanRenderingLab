#include "Core/InstancedScene.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    try {
        const auto mesh = CreateStressSphere();
        // More instances must not grow vertex/index/material storage.
        for (uint32_t count : {1u, 10u, 10000u}) {
            MeshData scene;
            scene.vertices.resize(3); scene.indices = {0,1,2};
            scene.materials.push_back(MaterialData{});
            auto instances = CreateMaterialInstances(count);
            AppendInstancedMesh(scene, mesh, instances);
            Check(scene.vertices.size() == mesh.vertices.size() + 3, "geometry duplicated per instance");
            Check(scene.indices.size() == mesh.indices.size() + 3, "indices duplicated per instance");
            Check(scene.materials.size() == mesh.materials.size() + 1, "materials duplicated per instance");
            Check(scene.instances.size() == count + 1, "identity/instance records missing");
            Check(scene.instances[0].model == glm::mat4(1.0f), "identity instance modified");
            const auto& draw = scene.subMeshes[0];
            Check(draw.firstIndex == 3 && draw.materialId == 1, "mesh offsets incorrect");
            Check(draw.firstInstance == 1 && draw.instanceCount == count, "draw instance range incorrect");
            for (size_t i = 0; i < mesh.indices.size(); ++i)
                Check(scene.indices[3+i] == mesh.indices[i]+3, "index rebasing incorrect");
        }
        const auto grid = CreateMaterialInstances();
        Check(grid.size() == 10, "quality grid count changed");
        for (size_t i = 0; i < grid.size(); ++i) {
            const auto p = grid[i].model * glm::vec4(0,0,0,1);
            Check(std::abs(p.x - (1.9f + float(i%5)*1.5f)) < 1e-5f, "quality grid X changed");
            Check(std::abs(p.y - (2.55f - float(i/5)*1.7f)) < 1e-5f, "quality grid Y changed");
            Check(grid[i].materialProperties.x == float(i/5), "metallic row changed");
            Check(grid[i].materialProperties.w == 1, "material override missing");
        }
        // Catch incorrect front-face winding on the stress mesh.
        for (size_t i = 0; i < mesh.indices.size(); i+=3) {
            auto a = glm::make_vec3(mesh.vertices[mesh.indices[i]].position);
            auto b = glm::make_vec3(mesh.vertices[mesh.indices[i+1]].position);
            auto c = glm::make_vec3(mesh.vertices[mesh.indices[i+2]].position);
            Check(glm::dot(glm::cross(b-a,c-a), a+b+c) > 0, "stress sphere winding is reversed");
        }
        MeshData wallScene;
        AppendOcclusionWall(wallScene);
        Check(wallScene.vertices.size()==24 && wallScene.indices.size()==36,"occlusion wall geometry incorrect");
        Check(wallScene.instances.size()==1 && wallScene.subMeshes[0].firstInstance==0 && wallScene.subMeshes[0].occluder,
            "static occluder must use the identity direct-draw path");
        bool rejected = false;
        auto corrupt = mesh; corrupt.indices[0] = static_cast<uint32_t>(corrupt.vertices.size());
        try { MeshData scene; AppendInstancedMesh(scene, corrupt, grid); }
        catch (const std::runtime_error&) { rejected = true; }
        Check(rejected, "invalid input geometry was accepted");
        std::cout << "Instance addressing, material variation, constant geometry size and stress winding passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
