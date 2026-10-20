#include "Core/AoMotionScene.h"
#include <iostream>
#include <stdexcept>
int main()
{
    try
    {
        const auto mesh = CreateAoMotionScene();
        if (mesh.instances.size() != 2 || mesh.subMeshes.size() != 2 || mesh.indices.size() != 42)
            throw std::runtime_error("Expected independent receiver and box");
        if (mesh.subMeshes[0].firstInstance != 0 || mesh.subMeshes[1].firstInstance != 1)
            throw std::runtime_error("Receiver must stay on identity instance zero");
        for (const auto &sub : mesh.subMeshes)
            for (uint32_t i = sub.firstIndex; i < sub.firstIndex + sub.indexCount; i += 3)
            {
                glm::vec3 p[3];
                for (int j = 0; j < 3; ++j)
                {
                    const auto &v = mesh.vertices.at(mesh.indices.at(i + j));
                    p[j] = {v.position[0], v.position[1], v.position[2]};
                }
                const auto &v = mesh.vertices.at(mesh.indices.at(i));
                if (glm::dot(glm::cross(p[1] - p[0], p[2] - p[0]),
                             glm::vec3(v.normal[0], v.normal[1], v.normal[2])) <= 0)
                    throw std::runtime_error("Fixture winding disagrees with surface normal");
            }
        if (AoBoxOffset(0) != AoBoxOffset(.5f) || std::abs(AoBoxOffset(3) - .9f) > .0001f ||
            AoBoxOffset(3) != AoBoxOffset(3.5f) || AoBoxOffset(0) != AoBoxOffset(8))
            throw std::runtime_error("Box hold/stop/period behavior failed");
        std::cout << "AO motion receiver, winding and trajectory OK\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
