#pragma once
#include <cmath>
#include <glm/glm.hpp>
// Independent two-sided Moller-Trumbore reference; direction need not be unit.
inline bool RayTriangleReference(glm::vec3 origin, glm::vec3 direction, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                                 float tMin, float tMax, float &distance, glm::vec2 &bary)
{
    auto e1 = b - a, e2 = c - a, p = glm::cross(direction, e2);
    float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-8f)
        return false;
    auto s = origin - a;
    float u = glm::dot(s, p) / det;
    if (u < 0 || u > 1)
        return false;
    auto q = glm::cross(s, e1);
    float v = glm::dot(direction, q) / det;
    if (v < 0 || u + v > 1)
        return false;
    float t = glm::dot(e2, q) / det;
    if (t < tMin || t >= tMax)
        return false;
    distance = t;
    bary = {u, v};
    return true;
}
