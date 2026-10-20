#include "Core/AoBlurMath.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        Check(AoHalfSize(1280) == 640 && AoHalfSize(961) == 481 && AoHalfSize(541) == 271 &&
                  AoHalfSize(1) == 1,
              "half resolution must preserve NPOT edge cells");
        Check(AoLowCoordinate(0) == -.25f && AoLowCoordinate(1) == .25f && AoLowCoordinate(960) == 479.75f,
              "full pixel centers map to wrong low coordinates");
        Check(int(std::floor(AoLowCoordinate(960))) + 1 < int(AoHalfSize(961)),
              "NPOT final pixel lost its candidate cell");
        const glm::vec3 p(0, 0, -2), n = glm::normalize(glm::vec3(0, 1, 1));
        const glm::vec3 q = p + glm::vec3(0, .5f, -.5f);
        Check(std::abs(q.z - p.z) > .49f, "test must exercise a large camera-depth slope");
        Check(AoPlaneSeparation(p, n, q, n) < 1e-6f, "coplanar slope was mistaken for an edge");
        const glm::vec3 front(0, 0, 1), layer = p + glm::vec3(0, 0, .3f);
        Check(std::abs(AoPlaneSeparation(p, front, layer, front) - .3f) < 1e-6f,
              "parallel depth layers blended");
        const glm::vec3 up(0, 1, 0), other = p + glm::vec3(0, 0, .2f);
        Check(std::abs(AoPlaneSeparation(p, up, other, front) - .2f) < 1e-6f, "neighbor plane was ignored");
        Check(AoPlaneSeparation(p, up, other, front) == AoPlaneSeparation(other, front, p, up),
              "metric must be symmetric");
        auto transform = glm::translate(glm::mat4(1), glm::vec3(2, -3, 4));
        transform = glm::rotate(transform, .73f, glm::normalize(glm::vec3(1, 2, 3)));
        const auto point = [&](glm::vec3 v) { return glm::vec3(transform * glm::vec4(v, 1)); };
        const auto direction = [&](glm::vec3 v) { return glm::vec3(transform * glm::vec4(v, 0)); };
        Check(std::abs(AoPlaneSeparation(point(p), direction(up), point(other), direction(front)) - .2f) <
                  1e-5f,
              "separation must be invariant under rigid camera transforms");
        Check(AoPlaneSeparation(p, up, p, front) == 0, "center weight must have zero separation");
        std::cout << "AO plane separation geometry tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
