#pragma once
#include "MeshData.h"
#include <cmath>
inline MeshData CreateRayQueryScene(bool character = false)
{
    MeshData mesh;
    const auto material = [&](glm::vec3 color, float metal, float rough, bool mirror = false) {
        MaterialData m;
        for (int k = 0; k < 3; ++k)
            m.diffuseColor[k] = color[k];
        m.metallic = metal;
        m.roughness = rough;
        m.rayMirror = mirror;
        mesh.materials.push_back(m);
        return int(mesh.materials.size() - 1);
    };
    int mirror = material({.88f, .9f, .95f}, 1, .03f, true), wall = material({.22f, .25f, .3f}, 0, .85f),
        red = material({.65f, .035f, .02f}, 0, .32f), gold = material({.8f, .48f, .12f}, .75f, .18f),
        blue = material({.025f, .35f, .55f}, 0, .24f), pink = material({.7f, .025f, .3f}, 0, .4f);
    const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, int mat) {
        uint32_t first = uint32_t(mesh.vertices.size()), index = uint32_t(mesh.indices.size());
        for (auto p : {a, b, c, d})
        {
            Vertex v{};
            for (int k = 0; k < 3; ++k)
            {
                v.position[k] = p[k];
                v.normal[k] = n[k];
                v.color[k] = 1;
            }
            mesh.vertices.push_back(v);
        }
        for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
            mesh.indices.push_back(first + k);
        mesh.subMeshes.push_back({index, 6, mat});
    };
    quad({-4, 0, -3}, {-4, 0, 5}, {4, 0, 5}, {4, 0, -3}, {0, 1, 0}, mirror);
    quad({-4, 0, -3.05f}, {4, 0, -3.05f}, {4, 4, -3.05f}, {-4, 4, -3.05f}, {0, 0, 1}, wall);
    quad({-4, 0, 5}, {-4, 0, -3.05f}, {-4, 4, -3.05f}, {-4, 4, 5}, {1, 0, 0}, wall);
    quad({-2.7f, .3f, -3}, {2.7f, .3f, -3}, {2.7f, 3.5f, -3}, {-2.7f, 3.5f, -3}, {0, 0, 1}, mirror);
    const auto sphere = [&](glm::vec3 center, float radius, int mat) {
        uint32_t first = uint32_t(mesh.vertices.size()), index = uint32_t(mesh.indices.size());
        constexpr int rows = 24, cols = 48;
        for (int y = 0; y <= rows; ++y)
            for (int x = 0; x <= cols; ++x)
            {
                float theta = 3.14159265f * y / rows, phi = 6.2831853f * x / cols;
                glm::vec3 n(std::sin(theta) * std::cos(phi), std::cos(theta),
                            std::sin(theta) * std::sin(phi));
                auto p = center + radius * n;
                Vertex v{};
                for (int k = 0; k < 3; ++k)
                {
                    v.position[k] = p[k];
                    v.normal[k] = n[k];
                    v.color[k] = 1;
                }
                mesh.vertices.push_back(v);
            }
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x)
            {
                uint32_t a = first + y * (cols + 1) + x, b = a + cols + 1;
                for (uint32_t k : {a, a + 1, b, a + 1, b + 1, b})
                    mesh.indices.push_back(k);
            }
        mesh.subMeshes.push_back({index, uint32_t(mesh.indices.size()) - index, mat});
    };
    sphere(character ? glm::vec3(-1.9f, .7f, .4f) : glm::vec3(-1.35f, .7f, .1f), .7f, red);
    sphere(character ? glm::vec3(1.9f, .85f, -.7f) : glm::vec3(1.15f, .85f, -.7f), .85f, gold);
    sphere(character ? glm::vec3(2.8f, .38f, 1.5f) : glm::vec3(.15f, .38f, 1.5f), .38f, blue);
    // Behind the default camera: absent from its raster image, still in the TLAS.
    sphere({0, 1.5f, 7}, .8f, pink);
    return mesh;
}
