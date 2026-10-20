#pragma once
#include "AnimationRig.h"
#include <array>
#include <cmath>
#include <map>

// Weld only UV splits whose source normal, skin weights and every morph delta
// agree. Hard normal edges and differently deforming coincident vertices stay split.
inline std::vector<std::vector<glm::uvec3>> BuildSurfaceAdjacency(const RigPrimitive &p, uint32_t base,
                                                                  bool reverse)
{
    std::map<std::vector<int64_t>, size_t> keys;
    std::vector<size_t> group(p.vertices.size());
    std::vector<std::vector<glm::uvec3>> faces;
    for (size_t i = 0; i < p.vertices.size(); ++i)
    {
        const auto &v = p.vertices[i];
        std::vector<int64_t> key;
        const auto append = [&](glm::vec3 a) {
            for (int k = 0; k < 3; ++k)
                key.push_back(std::llround(a[k] * 100000.0));
        };
        append(v.position);
        append(v.normal);
        for (int k = 0; k < 4; ++k)
        {
            key.push_back(v.joints[k]);
            key.push_back(std::llround(v.weights[k] * 100000.0));
        }
        for (const auto &m : p.morphs)
        {
            append(m.positions.empty() ? glm::vec3(0) : m.positions[i]);
            append(m.normals.empty() ? glm::vec3(0) : m.normals[i]);
        }
        auto [it, inserted] = keys.emplace(std::move(key), faces.size());
        if (inserted)
            faces.emplace_back();
        group[i] = it->second;
    }
    for (size_t i = 0; i < p.indices.size(); i += 3)
    {
        glm::uvec3 local(p.indices[i], p.indices[i + (reverse ? 2 : 1)], p.indices[i + (reverse ? 1 : 2)]);
        for (int k = 0; k < 3; ++k)
        {
            bool duplicate = false;
            for (int j = 0; j < k; ++j)
                duplicate |= group[local[k]] == group[local[j]];
            if (!duplicate)
                faces[group[local[k]]].push_back(local + glm::uvec3(base));
        }
    }
    std::vector<std::vector<glm::uvec3>> result;
    for (auto g : group)
        result.push_back(faces[g]);
    return result;
}

inline glm::vec3 RebuildSurfaceNormal(const std::vector<glm::vec3> &positions,
                                      const std::vector<glm::uvec3> &faces, glm::vec3 fallback)
{
    glm::vec3 sum(0);
    for (auto f : faces)
        sum += glm::cross(positions[f.y] - positions[f.x], positions[f.z] - positions[f.x]);
    return glm::dot(sum, sum) > 1e-16f ? glm::normalize(sum) : fallback;
}
