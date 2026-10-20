#pragma once
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace SkinningMath
{
struct DualQuaternion
{
    glm::quat real{0, 0, 0, 0}, dual{0, 0, 0, 0};
};
inline DualQuaternion FromRigid(const glm::mat4 &m)
{
    const glm::mat3 r(m), gram = glm::transpose(r) * r;
    float error = std::abs(glm::determinant(r) - 1.f);
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            error = glm::max(error, std::abs(gram[c][row] - (c == row ? 1.f : 0.f)));
    // Nonrigid transforms retain matrix skinning, including normal inverse transpose.
    if (!std::isfinite(error) || error > 1e-4f)
        return {};
    auto q = glm::normalize(glm::quat_cast(r));
    return {q, .5f * (glm::quat(0, m[3].x, m[3].y, m[3].z) * q)};
}
inline bool Blend(const std::array<DualQuaternion, 4> &bones, glm::vec4 weights, DualQuaternion &result)
{
    int reference = 0;
    for (int k = 1; k < 4; ++k)
        if (weights[k] > weights[reference])
            reference = k;
    result = {};
    for (int k = 0; k < 4; ++k)
    {
        if (weights[k] <= 0)
            continue;
        if (glm::dot(bones[k].real, bones[k].real) < .5f)
            return false;
        float w = weights[k] * (glm::dot(bones[reference].real, bones[k].real) < 0 ? -1.f : 1.f);
        result.real += bones[k].real * w;
        result.dual += bones[k].dual * w;
    }
    float length = glm::length(result.real);
    if (length < 1e-8f)
        return false;
    result.real /= length;
    result.dual /= length;
    result.dual -= result.real * glm::dot(result.real, result.dual);
    return true;
}
inline glm::vec3 Transform(const DualQuaternion &q, glm::vec3 p)
{
    auto translation = 2.f * (q.dual * glm::conjugate(q.real));
    return q.real * p + glm::vec3(translation.x, translation.y, translation.z);
}
inline glm::vec4 Pack(glm::quat q)
{
    return {q.x, q.y, q.z, q.w};
}
inline glm::quat Unpack(glm::vec4 q)
{
    return {q.w, q.x, q.y, q.z};
}
} // namespace SkinningMath
