#include "Core/MotionMath.h"
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
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
        auto m = ProjectPreviousMotion({.4f, .6f}, {.2f, -.4f, .5f, 1}, true);
        Check(m.candidate && glm::length(m.delta - glm::vec2(.2f, -.3f)) < 1e-6f,
              "UV direction or NDC conversion wrong");
        Check(m.previousDepth == .5f, "Vulkan depth must remain in [0,1]");
        auto reset = ProjectPreviousMotion({.4f, .6f}, {.2f, -.4f, .5f, 1}, false);
        Check(!reset.candidate && reset.delta == glm::vec2(0) && reset.previousDepth == -1,
              "first/reset frame not invalidated");
        Check(!ProjectPreviousMotion({.5f, .5f}, {0, 0, .5f, -1}, true).candidate,
              "behind-camera sample accepted");
        Check(!ProjectPreviousMotion({.5f, .5f}, {3, 0, .5f, 1}, true).candidate,
              "offscreen sample accepted");
        Check(!ProjectPreviousMotion({.5f, .5f}, {0, 0, -.1f, 1}, true).candidate,
              "near-clipped sample accepted");
        auto projection = glm::perspectiveRH_ZO(glm::radians(60.f), 1.5f, .1f, 100.f);
        projection[1][1] *= -1;
        glm::vec4 point(.2f, .3f, -4, 1), clip = projection * point;
        glm::vec2 uv = glm::vec2(clip) / clip.w * .5f + .5f;
        Check(glm::length(ProjectPreviousMotion(uv, clip, true).delta) < 1e-7f,
              "stationary point has velocity");
        auto previousView = glm::translate(glm::mat4(1), glm::vec3(-.1f, 0, 0));
        auto camera = ProjectPreviousMotion(uv, projection * previousView * point, true);
        Check(camera.delta.x < 0 && std::abs(camera.delta.y) < 1e-7f, "camera translation sign wrong");
        auto currentModel = glm::translate(glm::mat4(1), glm::vec3(.1f, 0, 0));
        auto currentClip = projection * currentModel * point;
        auto object = ProjectPreviousMotion(glm::vec2(currentClip) / currentClip.w * .5f + .5f,
                                            projection * point, true);
        Check(object.delta.x < 0, "object moving right must point left into previous frame");
        Check(glm::length((glm::vec2(currentClip) / currentClip.w * .5f + .5f + object.delta) - uv) < 1e-6f,
              "motion UV round trip failed");
        std::cout << "Motion projection tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
