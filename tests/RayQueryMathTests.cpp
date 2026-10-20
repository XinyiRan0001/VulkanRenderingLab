#include "Core/RayQueryMath.h"
#include "Core/RayQueryScene.h"
#include <iostream>
#include <stdexcept>
void Require(bool b, const char *text)
{
    if (!b)
        throw std::runtime_error(text);
}
int main()
{
    try
    {
        glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 1, 0);
        float t = 0;
        glm::vec2 bary;
        Require(RayTriangleReference({.25, .25, 2}, {0, 0, -1}, a, b, c, .001f, 10, t, bary),
                "front hit missing");
        Require(std::abs(t - 2) < 1e-6 && glm::length(bary - glm::vec2(.25)) < 1e-6,
                "analytic distance/barycentrics");
        Require(RayTriangleReference({.25, .25, -2}, {0, 0, 1}, a, b, c, .001f, 10, t, bary),
                "back hit missing");
        Require(!RayTriangleReference({2, 2, 2}, {0, 0, -1}, a, b, c, 0, 10, t, bary),
                "outside triangle hit");
        Require(!RayTriangleReference({.25, .25, 2}, {1, 0, 0}, a, b, c, 0, 10, t, bary), "parallel ray hit");
        Require(!RayTriangleReference({.25, .25, 2}, {0, 0, -1}, a, b, c, 0, 1, t, bary), "tMax ignored");
        Require(!RayTriangleReference({.25, .25, 2}, {0, 0, -1}, a, b, c, 3, 10, t, bary), "tMin ignored");
        Require(!RayTriangleReference({.25, .25, 2}, {0, 0, -1}, a, a, a, 0, 10, t, bary),
                "degenerate triangle hit");
        const auto scene = CreateRayQueryScene();
        size_t mirror = 0;
        for (const auto &sub : scene.subMeshes)
        {
            if (scene.materials[sub.materialId].rayMirror)
                ++mirror;
            for (uint32_t i = sub.firstIndex; i < sub.firstIndex + sub.indexCount; i += 3)
            {
                const auto &va = scene.vertices[scene.indices[i]];
                const auto &vb = scene.vertices[scene.indices[i + 1]];
                const auto &vc = scene.vertices[scene.indices[i + 2]];
                glm::vec3 pa(va.position[0], va.position[1], va.position[2]),
                    pb(vb.position[0], vb.position[1], vb.position[2]),
                    pc(vc.position[0], vc.position[1], vc.position[2]);
                auto cross = glm::cross(pb - pa, pc - pa);
                Require(glm::dot(cross, glm::vec3(va.normal[0], va.normal[1], va.normal[2])) > -1e-6,
                        "scene winding opposed to normals");
            }
        }
        Require(mirror == 2, "two mirror receivers required");
        std::cout << "Ray reference: analytic hits, barycentrics, intervals, misses and scene winding OK\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
