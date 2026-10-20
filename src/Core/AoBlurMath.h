#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Unit geometric normals; positions and normals are in the same space.
// Symmetric distances to both tangent planes avoid camera-Z slope rejection.
inline float AoPlaneSeparation(const glm::vec3 &p, const glm::vec3 &n, const glm::vec3 &q, const glm::vec3 &m)
{
    const auto delta = q - p;
    return std::max(std::abs(glm::dot(n, delta)), std::abs(glm::dot(m, delta)));
}

inline unsigned AoHalfSize(unsigned size)
{
    return (size + 1) / 2;
}
// Two low-cell centers bracketing a full pixel center (2:1 sampling grid).
inline float AoLowCoordinate(unsigned pixel)
{
    return (float(pixel) + .5f) * .5f - .5f;
}
