#include "Core/CullingBatches.h"
#include "Core/InstancedScene.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
void Check(bool condition, const char *why)
{
    if (!condition)
        throw std::runtime_error(why);
}
void Reject(const MeshData &mesh)
{
    try
    {
        BuildCullingBatches(mesh);
    }
    catch (const std::runtime_error &)
    {
        return;
    }
    throw std::runtime_error("Malformed culling layout accepted");
}
int main()
{
    try
    {
        for (uint32_t count : {2u, 3u, 1600u, 10000u})
        {
            MeshData mesh;
            AppendMixedInstances(mesh, count);
            auto layout = BuildCullingBatches(mesh);
            Check(layout.batches.size() == 2 && layout.totalInstances == count,
                  "mixed group count incorrect");
            Check(mesh.instances.size() == count + 1, "instances duplicated across material ranges");
            Check(layout.commands.size() == 3 && layout.draws.size() == 3,
                  "two-material cube must have two commands");
            const auto &a = layout.batches[0];
            const auto &b = layout.batches[1];
            Check(a.instances.x == 1 && b.instances.x == 1 + a.instances.y, "input partitions overlap");
            Check(a.instances.z == 0 && b.instances.z == a.instances.y, "visible partitions overlap");
            Check(a.instances.y + b.instances.y == count && a.commands.y == 1 && b.commands.y == 2,
                  "materials changed unique instance capacity");
            Check(b.localSphere.w > a.localSphere.w, "cube must use its own conservative prototype bound");
            Check(layout.draws[1].visibleBase == layout.draws[2].visibleBase &&
                      layout.draws[1].command != layout.draws[2].command,
                  "material ranges must share IDs but keep separate commands");
            for (auto cmd : layout.commands)
                Check(cmd.firstInstance == 0 && cmd.instanceCount == 0,
                      "indirect template requires zero firstInstance/count");
            for (uint32_t id = b.instances.x; id < b.instances.x + b.instances.y; ++id)
                Check(mesh.instances[id].materialProperties.w == 0, "cube material faces were overridden");
        }
        MeshData mesh;
        AppendMixedInstances(mesh, 20);
        // Interleave a second sphere material range among the cube ranges.
        auto second = mesh.subMeshes[0];
        second.firstIndex += 3;
        second.indexCount -= 3;
        mesh.subMeshes.insert(mesh.subMeshes.begin() + 2, second);
        const auto layout = BuildCullingBatches(mesh);
        Check(layout.draws[0].command == 0 && layout.draws[2].command == 1 && layout.draws[1].command == 2 &&
                  layout.draws[3].command == 3,
              "draw traversal must not assume contiguous material groups");
        Check(layout.totalInstances == 20, "shared material range duplicated instances");
        auto corrupt = mesh;
        corrupt.subMeshes[1].firstInstance = 2;
        Reject(corrupt);
        corrupt = mesh;
        corrupt.subMeshes[2].localSphere.w += 1;
        Reject(corrupt);
        corrupt = mesh;
        corrupt.subMeshes[0].instanceCount = 0;
        Reject(corrupt);
        corrupt = mesh;
        corrupt.subMeshes[0].firstInstance = uint32_t(mesh.instances.size());
        Reject(corrupt);
        corrupt = mesh;
        corrupt.subMeshes[0].indexCount = uint32_t(mesh.indices.size() + 1);
        Reject(corrupt);
        corrupt = mesh;
        corrupt.subMeshes[0].localSphere.w = NAN;
        Reject(corrupt);
        Reject(MeshData{});
        corrupt = mesh;
        corrupt.subMeshes.resize(3277, mesh.subMeshes.front());
        Reject(corrupt);
        const auto cube = CreateStressCube();
        Check(cube.vertices.size() == 24 && cube.indices.size() == 36 && cube.materials.size() == 2,
              "cube geometry incorrect");
        for (size_t i = 0; i < cube.indices.size(); i += 3)
        {
            auto point = [&](uint32_t index) {
                auto &p = cube.vertices[index].position;
                return glm::vec3(p[0], p[1], p[2]);
            };
            auto a = point(cube.indices[i]), b = point(cube.indices[i + 1]), c = point(cube.indices[i + 2]);
            Check(glm::dot(glm::cross(b - a, c - a), a + b + c) > 0, "cube winding reversed");
        }
        std::cout << "Culling partitions, interleaved material addresses, bounds and mixed geometry passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
