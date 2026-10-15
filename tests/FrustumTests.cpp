#include "Core/Frustum.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

void Check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        const auto clip = ExtractFrustum(glm::mat4(1.0f));
        Check(SphereVisible(clip, {0, 0, 0.5f, 0.1f}), "inside sphere rejected");
        for (auto sphere :
             {glm::vec4(-2, 0, .5f, .1f), glm::vec4(2, 0, .5f, .1f), glm::vec4(0, -2, .5f, .1f),
              glm::vec4(0, 2, .5f, .1f), glm::vec4(0, 0, -.2f, .1f), glm::vec4(0, 0, 1.2f, .1f)})
            Check(!SphereVisible(clip, sphere), "sphere beyond a frustum plane accepted");
        Check(SphereVisible(clip, {1.05f, 0, .5f, .1f}), "boundary intersecting sphere falsely culled");
        Check(SphereVisible(clip, {0, 0, -.05f, .1f}), "near intersecting sphere falsely culled");
        Check(!SphereVisible(clip, {0, 0, -.5f, .1f}), "OpenGL near-plane rule incorrectly used for Vulkan");
        const auto perspective = ExtractFrustum(glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, .1f, 10.0f));
        Check(SphereVisible(perspective, {0, 0, -2, .2f}), "perspective visible sphere rejected");
        Check(!SphereVisible(perspective, {0, 0, 2, .2f}), "behind-camera sphere accepted");
        Check(!SphereVisible(perspective, {0, 0, -11, .2f}), "far sphere accepted");
        // Independent homogeneous-clip comparison checks extraction, matrix order
        // and the global scene transform without reusing the sphere-plane test.
        auto view = glm::lookAtRH(glm::vec3(2, 3, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        auto camera = glm::perspectiveRH_ZO(glm::radians(55.0f), 1.7f, .1f, 30.0f) * view *
                      glm::scale(glm::mat4(1), glm::vec3(.35f));
        auto cameraPlanes = ExtractFrustum(camera);
        for (int i = 0; i < 1000; ++i)
        {
            glm::vec3 p(float((i * 17) % 101) - 50.0f, float((i * 37) % 53) - 26.0f,
                        float((i * 11) % 79) - 39.0f);
            auto c = camera * glm::vec4(p, 1);
            bool clipped =
                c.w > 0 && c.x >= -c.w && c.x <= c.w && c.y >= -c.w && c.y <= c.w && c.z >= 0 && c.z <= c.w;
            Check(SphereVisible(cameraPlanes, glm::vec4(p, 0)) == clipped,
                  "extracted planes disagree with homogeneous clipping");
        }
        glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1), {2, 3, 4}), {1, 2, 3});
        auto transformed = TransformSphere(transform, {0, 0, 0, 1});
        Check(glm::length(glm::vec3(transformed) - glm::vec3(2, 3, 4)) < 1e-5f &&
                  std::abs(transformed.w - 3) < 1e-5f,
              "nonuniform scale bound incorrect");
        transform[1][0] = 2.0f; // shear: max column length alone need not be conservative
        transformed = TransformSphere(transform, {0, 0, 0, 1});
        for (int y = 0; y <= 20; ++y)
            for (int x = 0; x < 40; ++x)
            {
                float theta = 3.14159265f * y / 20, phi = 6.2831853f * x / 40;
                const glm::vec3 p(std::sin(theta) * std::cos(phi), std::cos(theta),
                                  std::sin(theta) * std::sin(phi));
                const glm::vec3 q = glm::vec3(transform * glm::vec4(p, 1));
                Check(glm::length(q - glm::vec3(transformed)) <= transformed.w + 1e-5f,
                      "sheared sphere not conservatively bounded");
            }
        std::cout << "Vulkan frustum planes, partial intersections, behind/far rejection and transformed "
                     "bounds passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
