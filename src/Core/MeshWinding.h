#pragma once
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// Conservative repair classification: every nondegenerate triangle must have
// an authored average normal pointing strictly against its geometric normal.
// Mixed winding, missing normals and near-tangent normals are left untouched.
template <typename VertexType>
bool HasUniformlyReversedNormals(const std::vector<VertexType> &vertices,
                                 const std::vector<uint32_t> &indices)
{
    if (indices.empty() || indices.size() % 3 != 0)
        return false;
    bool found = false;
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        const auto &a = vertices.at(indices[i]);
        const auto &b = vertices.at(indices[i + 1]);
        const auto &c = vertices.at(indices[i + 2]);
        const glm::vec3 face = glm::cross(b.position - a.position, c.position - a.position);
        const glm::vec3 normal = a.normal + b.normal + c.normal;
        const float area2 = glm::dot(face, face);
        if (area2 <= 1e-20f)
            continue;
        const float length2 = glm::dot(normal, normal);
        if (length2 <= 1e-20f || !(glm::dot(face, normal) < -1e-4f * std::sqrt(area2 * length2)))
            return false;
        found = true;
    }
    return found;
}
