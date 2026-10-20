#pragma once
#include <glm/glm.hpp>
struct MotionProjection
{
    glm::vec2 delta{0};
    float previousDepth = -1;
    bool candidate = false;
};
inline MotionProjection ProjectPreviousMotion(glm::vec2 currentUv, glm::vec4 previousClip, bool historyValid)
{
    MotionProjection result;
    if (!historyValid || previousClip.w <= 1e-6f)
        return result;
    const auto ndc = glm::vec3(previousClip) / previousClip.w;
    const auto previousUv = glm::vec2(ndc) * .5f + .5f;
    result.delta = previousUv - currentUv;
    result.previousDepth = ndc.z;
    result.candidate = previousUv.x >= 0 && previousUv.y >= 0 && previousUv.x < 1 && previousUv.y < 1 &&
                       ndc.z >= 0 && ndc.z <= 1;
    return result;
}
