#pragma once
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <stdexcept>

using FrustumPlanes = std::array<glm::vec4, 6>;
inline FrustumPlanes ExtractFrustum(const glm::mat4 &clipFromScene)
{
    const auto rows = glm::transpose(clipFromScene);
    // Vulkan clip depth is 0 <= z <= w: near is row 2, not row 3 + row 2.
    FrustumPlanes planes{rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
                         rows[3] - rows[1], rows[2],           rows[3] - rows[2]};
    for (auto &plane : planes)
    {
        const float length = glm::length(glm::vec3(plane));
        if (length < 1e-12f)
            throw std::runtime_error("Degenerate camera frustum");
        plane /= length;
    }
    return planes;
}
inline glm::vec4 TransformSphere(const glm::mat4 &model, glm::vec4 sphere)
{
    const auto center = model * glm::vec4(glm::vec3(sphere), 1.0f);
    const glm::mat3 a = glm::mat3(model);
    // sqrt(||A||_1 * ||A||_infinity) bounds the spectral norm, including shear.
    float norm1 = 0, normInf = 0;
    for (int i = 0; i < 3; ++i)
    {
        float column = 0, row = 0;
        for (int j = 0; j < 3; ++j)
        {
            column += std::abs(a[i][j]);
            row += std::abs(a[j][i]);
        }
        norm1 = glm::max(norm1, column);
        normInf = glm::max(normInf, row);
    }
    return glm::vec4(glm::vec3(center), sphere.w * std::sqrt(norm1 * normInf));
}
inline bool SphereVisible(const FrustumPlanes &planes, glm::vec4 sphere)
{
    for (const auto &plane : planes)
        if (glm::dot(glm::vec3(plane), glm::vec3(sphere)) + plane.w < -sphere.w - 1e-4f)
            return false;
    return true;
}
