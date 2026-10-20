#pragma once
#include "MeshData.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

// Original procedural diagnostic: static identity receiver and separate rigid box.
inline MeshData CreateAoMotionScene()
{
    MeshData mesh;
    mesh.instances.resize(2);
    mesh.materials.resize(2);
    mesh.materials[0].roughness = .8f;
    mesh.materials[1].diffuseColor[0] = .25f;
    mesh.materials[1].diffuseColor[1] = .45f;
    mesh.materials[1].diffuseColor[2] = .7f;
    const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
        const uint32_t first = uint32_t(mesh.vertices.size());
        for (auto p : {a, b, c, d})
        {
            Vertex v{};
            for (int i = 0; i < 3; ++i)
            {
                v.position[i] = p[i];
                v.normal[i] = n[i];
                v.color[i] = 1;
            }
            mesh.vertices.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            mesh.indices.push_back(first + i);
    };
    quad({-4, 0, -3}, {-4, 0, 3}, {4, 0, 3}, {4, 0, -3}, {0, 1, 0});
    mesh.subMeshes.push_back({0, 6, 0});
    // Box footprint 0.8 x 0.8, resting exactly on the receiver.
    quad({-.4f, 0, .4f}, {.4f, 0, .4f}, {.4f, 1, .4f}, {-.4f, 1, .4f}, {0, 0, 1});
    quad({.4f, 0, -.4f}, {-.4f, 0, -.4f}, {-.4f, 1, -.4f}, {.4f, 1, -.4f}, {0, 0, -1});
    quad({.4f, 0, .4f}, {.4f, 0, -.4f}, {.4f, 1, -.4f}, {.4f, 1, .4f}, {1, 0, 0});
    quad({-.4f, 0, -.4f}, {-.4f, 0, .4f}, {-.4f, 1, .4f}, {-.4f, 1, -.4f}, {-1, 0, 0});
    quad({-.4f, 1, .4f}, {.4f, 1, .4f}, {.4f, 1, -.4f}, {-.4f, 1, -.4f}, {0, 1, 0});
    quad({-.4f, 0, -.4f}, {.4f, 0, -.4f}, {.4f, 0, .4f}, {-.4f, 0, .4f}, {0, -1, 0});
    SubMesh box{6, 36, 1};
    box.firstInstance = 1;
    box.localSphere = {0, .5f, 0, .76f};
    box.instanceGroupName = "moving AO occluder";
    mesh.subMeshes.push_back(box);
    return mesh;
}

// Seconds-based demo: hold, translate, stop, return, stop. Deterministic tests
// pass frame/60, while interactive runs use wall time (independent of frame rate).
inline float AoBoxOffset(float seconds)
{
    const float t = std::fmod(std::max(seconds, 0.f), 8.f);
    if (t < 1)
        return -.9f;
    if (t < 3)
        return -.9f + (t - 1) * .9f;
    if (t < 4)
        return .9f;
    if (t < 6)
        return .9f - (t - 4) * .9f;
    return -.9f;
}
